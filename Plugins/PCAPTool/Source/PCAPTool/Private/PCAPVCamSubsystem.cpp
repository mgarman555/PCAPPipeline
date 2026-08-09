#include "PCAPVCamSubsystem.h"
#include "VCamConfig.h"
#include "PCAPVCamActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"            // TActorIterator
#include "Editor.h"                 // GEditor
#include "CineCameraComponent.h"
#include "HAL/PlatformTime.h"       // packet staleness clock

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// Older than this and the last controller packet is stale. WVCAM broadcasts per frame, so a feed
// that stopped is not a standing instruction — no packet must mean no motion. The panel reads the
// same gate (IsReceivingControllerInput), so its "stalled" readout and the camera always agree.
static constexpr double GVCamInputStaleSeconds = 0.5;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void UPCAPVCamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UPCAPVCamSubsystem::Deinitialize()
{
    StopInputListener();
    Super::Deinitialize();
}

// ── Tick ────────────────────────────────────────────────────────────────────

bool UPCAPVCamSubsystem::IsTickable() const
{
    // Idle cheaply until a config is assigned (the panel/record controller sets it).
    return ActiveConfig != nullptr;
}

void UPCAPVCamSubsystem::Tick(float DeltaTime)
{
    if (!ActiveConfig) { return; }

    // The feed endpoint lives on the config and is edited in the VCam Database details view, so
    // watch it here — otherwise an IP/port change does nothing until the config is de-picked and
    // re-picked. Only a *changed* endpoint rebinds: a failed bind must not be retried every frame
    // (RestartControllerFeed is the operator's retry).
    if (ActiveConfig->ControllerFeedIP != RequestedInputIP || ActiveConfig->ControllerFeedPort != RequestedInputPort)
    {
        StartInputListener(ActiveConfig->ControllerFeedIP, ActiveConfig->ControllerFeedPort);
    }

    // Controller input (WVCAM raw broadcast) → intents → apply. Runs before AccumulateNavigate
    // consumes NavigateRate. Edge actions (Zero/Save/lens/world-scale) are already debounced in
    // the input layer; Hold is change-gated in ApplyInputIntents.
    {
        FVCamControllerInput In;
        bool bGot   = false;
        bool bStale = false;
        {
            FScopeLock Lock(&InputMutex);
            if (bHasInput)
            {
                // Without this gate the last packet replays forever: a dead feed with a stick
                // off-centre keeps integrating Navigate and the camera flies away on its own.
                bStale = (FPlatformTime::Seconds() - LastInputPacketTime) > GVCamInputStaleSeconds;
                if (!bStale) { In = LatestInput; bGot = true; }
            }
        }
        if (bGot)
        {
            InputLayer.Layout = (EVCamButtonLayout)FMath::Clamp(ActiveConfig->ActiveButtonLayout, 0, 1);
            ApplyInputIntents(InputLayer.Process(In, DeltaTime));
        }
        else if (bStale)
        {
            // Feed gone: park every integrated rate. The camera holds where the operator left it.
            RuntimeState.NavigateRate         = FVector::ZeroVector;
            RuntimeState.NavigateRotationRate = FVector::ZeroVector;
            RuntimeState.ZoomRate             = 0.f;
        }
    }

    FTransform Raw;
    if (!ReadLiveLinkTransform(Raw))
    {
        SetStreamStatus(EStreamStatus::Disconnected);
        return;
    }
    SetStreamStatus(EStreamStatus::Connected);

    // Accumulate joystick navigation (rate * dt) before processing this frame. Uses last frame's
    // smoothed rotation as the flight-mode basis (changes slowly vs dt). Translation, rotation,
    // and zoom rates all come from the input layer's per-frame speeds.
    FPCAPVCamProcessor::AccumulateNavigate(*ActiveConfig, RuntimeState.NavigateRate,
        RuntimeState.SmoothedRotation, RuntimeState.bFlightMode, DeltaTime);
    FPCAPVCamProcessor::AccumulateNavigateRotation(*ActiveConfig, RuntimeState.NavigateRotationRate, DeltaTime);
    FPCAPVCamProcessor::AccumulateZoom(*ActiveConfig, RuntimeState.ZoomRate, DeltaTime);

    const FTransform Out = FPCAPVCamProcessor::Process(*ActiveConfig, RuntimeState, Raw, DeltaTime);

    if (APCAPVCamActor* Cam = GetOrCreateVCamActor())
    {
        Cam->SetActorTransform(Out);
        if (UCineCameraComponent* CC = Cam->GetCineCameraComponent())
        {
            CC->SetCurrentFocalLength(FPCAPVCamProcessor::GetFocalLength(*ActiveConfig));
        }
    }
}

// ── Live Link read ──────────────────────────────────────────────────────────
// CONFIRM-AT-BUILD: the transform role + frame-data accessor are the 5.7-vs-5.4 risk
// point (the rest of the chain is engine-stable). Log if the subject can't be evaluated.

bool UPCAPVCamSubsystem::ReadLiveLinkTransform(FTransform& OutTransform) const
{
    if (!ActiveConfig) { return false; }

    IModularFeatures& MF = IModularFeatures::Get();
    if (!MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)) { return false; }

    ILiveLinkClient& Client = MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    const FName Subject = ActiveConfig->LiveLinkSubjectName;
    FLiveLinkSubjectFrameData Frame;

    // Vicon/Shogun can publish the camera rigid body under different Live Link roles —
    // Transform, Camera, or (commonly) a single-bone Animation subject. Try each so the
    // read works regardless of how the stage streams it.

    if (Client.EvaluateFrame_AnyThread(Subject, ULiveLinkTransformRole::StaticClass(), Frame))
    {
        if (const FLiveLinkTransformFrameData* T = Frame.FrameData.Cast<FLiveLinkTransformFrameData>())
        {
            OutTransform = T->Transform;
            return true;
        }
    }

    if (Client.EvaluateFrame_AnyThread(Subject, ULiveLinkCameraRole::StaticClass(), Frame))
    {
        if (const FLiveLinkCameraFrameData* C = Frame.FrameData.Cast<FLiveLinkCameraFrameData>())
        {
            OutTransform = C->Transform;
            return true;
        }
    }

    if (Client.EvaluateFrame_AnyThread(Subject, ULiveLinkAnimationRole::StaticClass(), Frame))
    {
        if (const FLiveLinkAnimationFrameData* A = Frame.FrameData.Cast<FLiveLinkAnimationFrameData>())
        {
            if (A->Transforms.Num() > 0)
            {
                // Single-bone rigid body → root transform is the camera pose. (If a stage ever
                // streams the vcam as a multi-bone skeleton, this would need a named-bone lookup.)
                OutTransform = A->Transforms[0];
                return true;
            }
        }
    }

    return false;
}

void UPCAPVCamSubsystem::SetStreamStatus(EStreamStatus NewStatus)
{
    if (StreamStatus == NewStatus) { return; }
    StreamStatus = NewStatus;
    OnStreamStatusChanged.Broadcast(NewStatus);
}

// ── Activation / actor ──────────────────────────────────────────────────────

void UPCAPVCamSubsystem::SetActiveConfig(UPCAPVCamConfig* Config)
{
    ActiveConfig = Config;
    RuntimeState = FPCAPVCamRuntimeState();   // reset session state on (re)activation
    InputLayer   = FVCamInputLayer();         // reset controller-input state too
    bPrevInputHold = false;
    // Config.Platform X/Y is a mirror of the input layer's Sony accumulator, and that accumulator
    // just went back to zero — clear the mirror or a stale offset saved by a previous session
    // silently biases every shot from here on (nothing else ever writes that field).
    ResetPlatformOffset();
    if (Config) { StartInputListener(Config->ControllerFeedIP, Config->ControllerFeedPort); }
    else        { StopInputListener(); }
}

void UPCAPVCamSubsystem::MarkConfigDirty()
{
    if (!ActiveConfig) { return; }
    ActiveConfig->Modify();             // snapshot into the open transaction, if there is one
    ActiveConfig->MarkPackageDirty();   // …and make the editor prompt to save on close
}

APCAPVCamActor* UPCAPVCamSubsystem::GetOrCreateVCamActor()
{
    if (VCamActor.IsValid()) { return VCamActor.Get(); }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) { return nullptr; }

    for (TActorIterator<APCAPVCamActor> It(World); It; ++It)
    {
        VCamActor = *It;
        return VCamActor.Get();
    }

    APCAPVCamActor* Spawned = World->SpawnActor<APCAPVCamActor>();
    VCamActor = Spawned;
    return Spawned;
}

// ── Transform controls ──────────────────────────────────────────────────────

void UPCAPVCamSubsystem::ZeroSpace()
{
    if (!ActiveConfig) { return; }
    FTransform Raw;
    if (ReadLiveLinkTransform(Raw))
    {
        FPCAPVCamProcessor::ZeroSpace(*ActiveConfig, Raw);
        MarkConfigDirty();   // Setup is the zero-origin calibration — it has to survive a restart
    }
}

void UPCAPVCamSubsystem::SetHold(bool bEnabled)
{
    if (!ActiveConfig) { return; }
    FTransform Raw;
    const bool bHaveRaw = ReadLiveLinkTransform(Raw);   // raw is needed only on release

    if (!bEnabled && !bHaveRaw)
    {
        // Releasing hold re-solves Setup against the live pose (Setup = Held * Cur^-1). With the
        // subject down, Cur is a default-constructed identity and the solve collapses to
        // Setup = Held — so the held pose gets applied twice the moment the stream returns, and
        // the only recovery is a Zero Space the operator has no reason to suspect they need.
        // Marker occlusion while parked in Hold is exactly when this happens. Stay held instead.
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] VCam: hold not released — Live Link subject '%s' is unavailable, so Setup cannot be re-solved. Still holding."),
            *ActiveConfig->LiveLinkSubjectName.ToString());
        return;
    }

    FPCAPVCamProcessor::SetHold(*ActiveConfig, RuntimeState, bEnabled, Raw);
    if (!bEnabled) { MarkConfigDirty(); }   // the release rewrote Setup
}

void UPCAPVCamSubsystem::SetFlightMode(bool bEnabled)   { RuntimeState.bFlightMode   = bEnabled; }
void UPCAPVCamSubsystem::SetLockPosition(bool bEnabled) { RuntimeState.bLockPosition = bEnabled; }
void UPCAPVCamSubsystem::SetLockRotation(bool bEnabled) { RuntimeState.bLockRotation = bEnabled; }
void UPCAPVCamSubsystem::SetLockRoll(bool bEnabled)     { RuntimeState.bLockRoll     = bEnabled; }
void UPCAPVCamSubsystem::SetKillRoll(bool bEnabled)     { RuntimeState.bKillRoll     = bEnabled; }

// ── Navigation / saved positions ────────────────────────────────────────────

void UPCAPVCamSubsystem::SaveCurrentPosition()
{
    if (!ActiveConfig) { return; }
    ActiveConfig->SavedPositions.Add(ActiveConfig->Navigate);
    ActiveConfig->ActiveSavedPositionIndex = ActiveConfig->SavedPositions.Num() - 1;
    MarkConfigDirty();
}

void UPCAPVCamSubsystem::GotoSavedPosition(int32 Index)
{
    if (!ActiveConfig) { return; }
    if (ActiveConfig->SavedPositions.IsValidIndex(Index))
    {
        // Not dirtied: this only moves the camera. Navigate is per-frame runtime state that the
        // joystick rewrites on the next tick anyway — dirtying here would bake that junk in.
        ActiveConfig->Navigate = ActiveConfig->SavedPositions[Index];
        ActiveConfig->ActiveSavedPositionIndex = Index;
    }
}

void UPCAPVCamSubsystem::DeleteSavedPosition(int32 Index)
{
    if (!ActiveConfig) { return; }
    if (ActiveConfig->SavedPositions.IsValidIndex(Index))
    {
        ActiveConfig->SavedPositions.RemoveAt(Index);
        ActiveConfig->ActiveSavedPositionIndex = -1;
        MarkConfigDirty();
    }
}

// ── Lens ────────────────────────────────────────────────────────────────────

void UPCAPVCamSubsystem::SetFocalLength(float Millimeters)
{
    if (ActiveConfig) { ActiveConfig->ActiveFocalLength = Millimeters; }
}

void UPCAPVCamSubsystem::CycleFocalLengthUp()
{
    if (!ActiveConfig || ActiveConfig->FocalLengthPresets.Num() == 0) { return; }
    int32 Best = INDEX_NONE;
    for (int32 i = 0; i < ActiveConfig->FocalLengthPresets.Num(); ++i)
    {
        if (ActiveConfig->FocalLengthPresets[i] > ActiveConfig->ActiveFocalLength + KINDA_SMALL_NUMBER)
        {
            Best = i; break;   // presets are authored ascending
        }
    }
    if (Best != INDEX_NONE) { ActiveConfig->ActiveFocalLength = ActiveConfig->FocalLengthPresets[Best]; }
}

void UPCAPVCamSubsystem::CycleFocalLengthDown()
{
    if (!ActiveConfig || ActiveConfig->FocalLengthPresets.Num() == 0) { return; }
    int32 Best = INDEX_NONE;
    for (int32 i = ActiveConfig->FocalLengthPresets.Num() - 1; i >= 0; --i)
    {
        if (ActiveConfig->FocalLengthPresets[i] < ActiveConfig->ActiveFocalLength - KINDA_SMALL_NUMBER)
        {
            Best = i; break;
        }
    }
    if (Best != INDEX_NONE) { ActiveConfig->ActiveFocalLength = ActiveConfig->FocalLengthPresets[Best]; }
}

// ── Gains / scale (stored; consumed by the deferred input layer) ─────────────

void UPCAPVCamSubsystem::SetTranslationGain(float Gain) { RuntimeState.TranslationGain = FMath::Clamp(Gain, 0.01f, 1.0f); }
void UPCAPVCamSubsystem::SetZoomGain(float Gain)        { RuntimeState.ZoomGain        = FMath::Clamp(Gain, 0.01f, 1.0f); }
void UPCAPVCamSubsystem::SetWorldSpaceScale(FVector Scale)
{
    if (!ActiveConfig) { return; }
    ActiveConfig->Scaling.WorldSpaceScale = Scale;
    MarkConfigDirty();
}

void UPCAPVCamSubsystem::SetNavigateRate(FVector Rate)
{
    RuntimeState.NavigateRate = Rate;
}

void UPCAPVCamSubsystem::SetActiveButtonLayout(int32 Layout)
{
    if (!ActiveConfig) { return; }
    const int32 NewLayout = FMath::Clamp(Layout, 0, 1);
    if (NewLayout == ActiveConfig->ActiveButtonLayout) { return; }

    // Only the Sony layout writes Config.Platform X/Y, and the processor applies Platform
    // unconditionally — so leaving Sony would leave the last accumulated offset baked into every
    // later shot with no way to clear it from here. Fold it into Navigate instead: the transform
    // is identical, so the camera does not jump on a layout change (reconciliation §3).
    if (ActiveConfig->ActiveButtonLayout == (int32)EVCamButtonLayout::Sony)
    {
        FoldPlatformXYIntoNavigate();
    }

    ActiveConfig->ActiveButtonLayout = NewLayout;
    MarkConfigDirty();
}

void UPCAPVCamSubsystem::ResetPlatformOffset()
{
    if (!ActiveConfig) { return; }
    // Not dirtied: the Sony XY offset is a live accumulator mirrored onto the config, never a
    // saved setting — activation clears it, so a stale value on disk can never bite.
    ActiveConfig->Platform.Translation.X = 0.f;
    ActiveConfig->Platform.Translation.Y = 0.f;
}

void UPCAPVCamSubsystem::FoldPlatformXYIntoNavigate()
{
    if (!ActiveConfig) { return; }

    // The processor composes …Platform * Navigate * (Setup * Cur) (steps 10 / 10.5), so moving
    // the Platform X/Y into Navigate leaves the product — and therefore the camera — untouched:
    //   PlatformKept * Navigate' == Platform * Navigate,  Navigate' = PlatformKept^-1 * Platform * Navigate.
    // Only X/Y are folded out; Platform's rotation and Z stay put (reserved for the parent/platform
    // path, VCamConfig.h) and are identity today, which makes PlatformKept the identity transform.
    FPCAPVCamAlignOffset Kept = ActiveConfig->Platform;
    Kept.Translation.X = 0.f;
    Kept.Translation.Y = 0.f;

    const FTransform Platform(ActiveConfig->Platform.Rotation.Quaternion(), ActiveConfig->Platform.Translation);
    const FTransform PlatformKept(Kept.Rotation.Quaternion(), Kept.Translation);
    const FTransform Navigate(ActiveConfig->Navigate.Rotation.Quaternion(), ActiveConfig->Navigate.Translation);
    const FTransform Folded = PlatformKept.Inverse() * Platform * Navigate;

    ActiveConfig->Navigate.Translation = Folded.GetLocation();
    ActiveConfig->Navigate.Rotation    = Folded.GetRotation().Rotator();
    ActiveConfig->Platform             = Kept;
}

void UPCAPVCamSubsystem::AddFocalLengthDelta(float DeltaMm)
{
    if (ActiveConfig)
    {
        ActiveConfig->ActiveFocalLength = FMath::Clamp(ActiveConfig->ActiveFocalLength + DeltaMm, 4.f, 1000.f);
    }
}

void UPCAPVCamSubsystem::SelectNextWorldScale()
{
    if (!ActiveConfig || ActiveConfig->WorldScalePresets.Num() == 0) { return; }
    const int32 N = ActiveConfig->WorldScalePresets.Num();
    ActiveConfig->ActiveWorldScaleIndex = (ActiveConfig->ActiveWorldScaleIndex + 1) % N;
    ActiveConfig->Scaling.WorldSpaceScale = ActiveConfig->WorldScalePresets[ActiveConfig->ActiveWorldScaleIndex];
    MarkConfigDirty();   // a debounced button press, not per-frame integration
}

void UPCAPVCamSubsystem::SelectPreviousWorldScale()
{
    if (!ActiveConfig || ActiveConfig->WorldScalePresets.Num() == 0) { return; }
    const int32 N = ActiveConfig->WorldScalePresets.Num();
    ActiveConfig->ActiveWorldScaleIndex = (ActiveConfig->ActiveWorldScaleIndex - 1 + N) % N;
    ActiveConfig->Scaling.WorldSpaceScale = ActiveConfig->WorldScalePresets[ActiveConfig->ActiveWorldScaleIndex];
    MarkConfigDirty();   // a debounced button press, not per-frame integration
}

// ── Readouts ────────────────────────────────────────────────────────────────

FTransform UPCAPVCamSubsystem::GetCurrentTransform() const
{
    return FTransform(RuntimeState.SmoothedRotation, RuntimeState.SmoothedPosition);
}

float UPCAPVCamSubsystem::GetCurrentFocalLength() const
{
    return ActiveConfig ? ActiveConfig->ActiveFocalLength : 0.f;
}

// ── Controller input: UDP listener (WVCAM raw broadcast) ─────────────────────
// CONFIRM-AT-BUILD: the FUdpSocketBuilder / FUdpSocketReceiver API is stable across 5.x.
// OnInputPacket runs on the receiver thread — it only parses + stores under the mutex.

void UPCAPVCamSubsystem::StartInputListener(const FString& BindIP, int32 Port)
{
    // Record what we were asked for even when the bind fails — the tick compares this to the
    // config to spot an endpoint edit, so a failure is attempted once, not once per frame.
    RequestedInputIP   = BindIP;
    RequestedInputPort = Port;

    // Already listening on this exact interface+port? Leave it.
    if (InputSocket && ActiveInputPort == Port && ActiveInputIP == BindIP) { return; }
    StopInputListener();
    if (Port <= 0)
    {
        InputListenerError = TEXT("no controller feed port set on the config");
        return;
    }

    // BindIP is the local interface to listen on ("0.0.0.0"/empty = all interfaces).
    FIPv4Address BindAddr = FIPv4Address::Any;
    if (!BindIP.IsEmpty() && BindIP != TEXT("0.0.0.0"))
    {
        if (!FIPv4Address::Parse(BindIP, BindAddr))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] VCam input: '%s' is not a valid IPv4 address; binding all interfaces."), *BindIP);
            BindAddr = FIPv4Address::Any;
        }
    }

    const FIPv4Endpoint Endpoint(BindAddr, (uint16)Port);
    InputSocket = FUdpSocketBuilder(TEXT("PCAPVCamInput"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .WithReceiveBufferSize(256 * 1024);

    if (!InputSocket)
    {
        // Kept on the subsystem, not just in the log: the panel has to be able to say "I could not
        // open the socket" instead of the same "waiting for WVCAM…" it shows for "nobody is sending".
        // A second editor instance or a leftover WVCAM process is the usual cause.
        InputListenerError = FString::Printf(TEXT("could not bind UDP %s:%d — port already in use?"), *BindAddr.ToString(), Port);
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] VCam input: %s"), *InputListenerError);
        return;
    }

    const FTimespan WaitTime = FTimespan::FromMilliseconds(100);
    InputReceiver = new FUdpSocketReceiver(InputSocket, WaitTime, TEXT("PCAPVCamInputRecv"));
    InputReceiver->OnDataReceived().BindUObject(this, &UPCAPVCamSubsystem::OnInputPacket);
    InputReceiver->Start();
    ActiveInputPort = Port;
    ActiveInputIP   = BindIP;
    UE_LOG(LogTemp, Log, TEXT("[PCAP] VCam input: listening for controller feed on UDP %s:%d."), *BindAddr.ToString(), Port);
}

void UPCAPVCamSubsystem::StopInputListener()
{
    if (InputReceiver) { delete InputReceiver; InputReceiver = nullptr; }   // joins the thread
    if (InputSocket)
    {
        InputSocket->Close();
        if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
        {
            SS->DestroySocket(InputSocket);
        }
        InputSocket = nullptr;
    }
    ActiveInputPort = 0;
    ActiveInputIP.Reset();
    InputListenerError.Reset();
    FScopeLock Lock(&InputMutex);
    bHasInput = false;
}

void UPCAPVCamSubsystem::RestartControllerFeed()
{
    if (!ActiveConfig) { return; }
    StopInputListener();   // clears the bound endpoint so the "already listening" gate can't skip
    StartInputListener(ActiveConfig->ControllerFeedIP, ActiveConfig->ControllerFeedPort);
}

FString UPCAPVCamSubsystem::GetControllerFeedEndpoint() const
{
    const FString IP = RequestedInputIP.IsEmpty() ? TEXT("0.0.0.0") : RequestedInputIP;
    return FString::Printf(TEXT("%s:%d"), *IP, RequestedInputPort);
}

void UPCAPVCamSubsystem::OnInputPacket(const FArrayReaderPtr& Data, const FIPv4Endpoint& /*From*/)
{
    if (!Data.IsValid() || Data->Num() <= 0) { return; }

    TArray<uint8> Bytes;
    Bytes.Append(Data->GetData(), Data->Num());
    Bytes.Add(0);   // null-terminate for UTF8_TO_TCHAR
    const FString Json = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));

    TSharedPtr<FJsonObject> Obj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) { return; }

    auto Axis = [&Obj](const TCHAR* Key) -> float { double V = 0.0; Obj->TryGetNumberField(Key, V); return (float)V; };
    auto Btn  = [&Obj](const TCHAR* Key) -> bool  { double V = 0.0; Obj->TryGetNumberField(Key, V); return V != 0.0; };

    // Keys match the WVCAM "vcam" device map (device_maps.py) emitted by pcap_vcam_raw_broadcast.py.
    FVCamControllerInput In;
    In.LeftLeftX   = Axis(TEXT("left_left_x"));   In.LeftLeftY   = Axis(TEXT("left_left_y"));
    In.LeftRightX  = Axis(TEXT("left_right_x"));  In.LeftRightY  = Axis(TEXT("left_right_y"));
    In.RightLeftX  = Axis(TEXT("right_left_x"));  In.RightLeftY  = Axis(TEXT("right_left_y"));
    In.RightRightX = Axis(TEXT("right_right_x")); In.RightRightY = Axis(TEXT("right_right_y"));
    In.LeftGain    = Axis(TEXT("left_gain"));     In.RightGain   = Axis(TEXT("right_gain"));
    In.LeftX  = Btn(TEXT("left_x"));   In.LeftY  = Btn(TEXT("left_y"));
    In.LeftA  = Btn(TEXT("left_a"));   In.LeftB  = Btn(TEXT("left_b"));
    In.LeftUp = Btn(TEXT("left_up"));  In.LeftDown = Btn(TEXT("left_down"));
    In.LeftLeft = Btn(TEXT("left_left")); In.LeftRight = Btn(TEXT("left_right"));
    In.RightX = Btn(TEXT("right_x"));  In.RightY = Btn(TEXT("right_y"));
    In.RightA = Btn(TEXT("right_a"));  In.RightB = Btn(TEXT("right_b"));
    In.RightUp = Btn(TEXT("right_up")); In.RightDown = Btn(TEXT("right_down"));
    In.RightLeft = Btn(TEXT("right_left")); In.RightRight = Btn(TEXT("right_right"));

    FScopeLock Lock(&InputMutex);
    LatestInput = In;
    bHasInput = true;
    LastInputPacketTime = FPlatformTime::Seconds();   // the tick's staleness gate reads this
    ++InputPacketCount;
}

void UPCAPVCamSubsystem::ApplyInputIntents(const FVCamInputIntents& Intents)
{
    // Per-frame speeds — integrated (rate * dt) in Tick by Accumulate{Navigate,NavigateRotation,Zoom}.
    RuntimeState.NavigateRate         = Intents.TranslationSpeed;
    RuntimeState.NavigateRotationRate = Intents.RotationSpeed;
    RuntimeState.ZoomRate             = Intents.ZoomSpeed;
    RuntimeState.TranslationGain      = Intents.TranslationGain;   // left_gain/4095 (HUD readout)
    RuntimeState.ZoomGain             = Intents.ZoomGainTrim;      // 1 - right_gain/4095 (HUD readout)

    switch (Intents.Mapping)
    {
        case EVCamMapping::Shifted: RuntimeState.ActiveMapping = TEXT("SHIFTED"); break;
        case EVCamMapping::Sony:    RuntimeState.ActiveMapping = TEXT("SONY");    break;
        default:                    RuntimeState.ActiveMapping = TEXT("STANDARD"); break;
    }

    // Edge actions.
    if (Intents.bZeroEverything) { ZeroSpace(); }
    if (Intents.bSavePosition)   { SaveCurrentPosition(); }
    if (Intents.bLensNext)       { CycleFocalLengthUp(); }
    if (Intents.bLensPrev)       { CycleFocalLengthDown(); }
    if (Intents.bWorldScaleNext) { SelectNextWorldScale(); }
    if (Intents.bWorldScalePrev) { SelectPreviousWorldScale(); }

    if (Intents.bToggleFlightMode) { SetFlightMode(!RuntimeState.bFlightMode); }
    if (Intents.bToggleHold)
    {
        const bool bNew = !RuntimeState.bHold;
        SetHold(bNew);
        bPrevInputHold = bNew;
    }
    if (Intents.bToggleLocked)
    {
        // WVCAM toggleLocked: if EITHER lock is on, unlock both; otherwise lock both.
        const bool bLocked = RuntimeState.bLockPosition || RuntimeState.bLockRotation;
        SetLockPosition(!bLocked);
        SetLockRotation(!bLocked);
    }

    if (ActiveConfig && ActiveConfig->SavedPositions.Num() > 0)
    {
        if (Intents.bGotoCurrent) { GotoSavedPosition(ActiveConfig->ActiveSavedPositionIndex); }
        if (Intents.bGotoPrev)
        {
            const int32 N = ActiveConfig->SavedPositions.Num();
            int32 Prev = ActiveConfig->ActiveSavedPositionIndex - 1;
            if (Prev < 0) { Prev = N - 1; }   // WVCAM gotoPreviousPosition wraps
            GotoSavedPosition(Prev);
        }
    }
    // Sony platforming: while the Sony layout streams, it owns Platform.Translation X/Y
    // (panel edits to those two are stomped — same live-ownership rule as Navigate).
    // The intents already read 0 on a reset frame; the explicit zero is belt-and-braces.
    if (ActiveConfig && InputLayer.Layout == EVCamButtonLayout::Sony)
    {
        if (Intents.bResetSonyXY)
        {
            ActiveConfig->Platform.Translation.X = 0.f;
            ActiveConfig->Platform.Translation.Y = 0.f;
        }
        else
        {
            ActiveConfig->Platform.Translation.X = Intents.SonyOffsetX;
            ActiveConfig->Platform.Translation.Y = Intents.SonyOffsetY;
        }
    }

    // Deferred (Phase 2): playback transport (bPlaybackToggle/bScrubFwd/bScrubBack) → Sequencer.
}

FVCamControllerInput UPCAPVCamSubsystem::GetLatestInput() const
{
    FScopeLock Lock(&InputMutex);
    return LatestInput;
}

int32 UPCAPVCamSubsystem::GetInputPacketCount() const
{
    FScopeLock Lock(&InputMutex);
    return InputPacketCount;
}

bool UPCAPVCamSubsystem::IsReceivingControllerInput() const
{
    FScopeLock Lock(&InputMutex);
    return bHasInput && (FPlatformTime::Seconds() - LastInputPacketTime) <= GVCamInputStaleSeconds;
}
