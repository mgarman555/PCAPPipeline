#include "PCAPVCamSubsystem.h"
#include "VCamConfig.h"
#include "PCAPVCamActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"            // TActorIterator
#include "Editor.h"                 // GEditor
#include "CineCameraComponent.h"

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

    // Controller input (WVCAM raw broadcast) → intents → apply. Runs before AccumulateNavigate
    // consumes NavigateRate. Edge actions (Zero/Save/lens/world-scale) are already debounced in
    // the input layer; Hold is change-gated in ApplyInputIntents.
    {
        FVCamControllerInput In;
        bool bGot = false;
        {
            FScopeLock Lock(&InputMutex);
            if (bHasInput) { In = LatestInput; bGot = true; }
        }
        if (bGot)
        {
            InputLayer.Layout = (EVCamButtonLayout)FMath::Clamp(ActiveConfig->ActiveButtonLayout, 0, 1);
            ApplyInputIntents(InputLayer.Process(In, DeltaTime));
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
    if (Config) { StartInputListener(Config->InputBroadcastPort); }
    else        { StopInputListener(); }
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
    if (ReadLiveLinkTransform(Raw)) { FPCAPVCamProcessor::ZeroSpace(*ActiveConfig, Raw); }
}

void UPCAPVCamSubsystem::SetHold(bool bEnabled)
{
    if (!ActiveConfig) { return; }
    FTransform Raw;
    ReadLiveLinkTransform(Raw);   // raw needed only on release; safe if it fails
    FPCAPVCamProcessor::SetHold(*ActiveConfig, RuntimeState, bEnabled, Raw);
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
}

void UPCAPVCamSubsystem::GotoSavedPosition(int32 Index)
{
    if (!ActiveConfig) { return; }
    if (ActiveConfig->SavedPositions.IsValidIndex(Index))
    {
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
    if (ActiveConfig) { ActiveConfig->Scaling.WorldSpaceScale = Scale; }
}

void UPCAPVCamSubsystem::SetNavigateRate(FVector Rate)
{
    RuntimeState.NavigateRate = Rate;
}

void UPCAPVCamSubsystem::SetActiveButtonLayout(int32 Layout)
{
    if (ActiveConfig) { ActiveConfig->ActiveButtonLayout = FMath::Clamp(Layout, 0, 1); }
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
}

void UPCAPVCamSubsystem::SelectPreviousWorldScale()
{
    if (!ActiveConfig || ActiveConfig->WorldScalePresets.Num() == 0) { return; }
    const int32 N = ActiveConfig->WorldScalePresets.Num();
    ActiveConfig->ActiveWorldScaleIndex = (ActiveConfig->ActiveWorldScaleIndex - 1 + N) % N;
    ActiveConfig->Scaling.WorldSpaceScale = ActiveConfig->WorldScalePresets[ActiveConfig->ActiveWorldScaleIndex];
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

void UPCAPVCamSubsystem::StartInputListener(int32 Port)
{
    if (InputSocket && ActiveInputPort == Port) { return; }   // already listening here
    StopInputListener();
    if (Port <= 0) { return; }

    const FIPv4Endpoint Endpoint(FIPv4Address::Any, (uint16)Port);
    InputSocket = FUdpSocketBuilder(TEXT("PCAPVCamInput"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .WithReceiveBufferSize(256 * 1024);

    if (!InputSocket)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] VCam input: could not bind UDP port %d (in use?)."), Port);
        return;
    }

    const FTimespan WaitTime = FTimespan::FromMilliseconds(100);
    InputReceiver = new FUdpSocketReceiver(InputSocket, WaitTime, TEXT("PCAPVCamInputRecv"));
    InputReceiver->OnDataReceived().BindUObject(this, &UPCAPVCamSubsystem::OnInputPacket);
    InputReceiver->Start();
    ActiveInputPort = Port;
    UE_LOG(LogTemp, Log, TEXT("[PCAP] VCam input: listening for WVCAM raw broadcast on UDP %d."), Port);
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
    FScopeLock Lock(&InputMutex);
    bHasInput = false;
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
    // Deferred (no UE equivalent yet): playback transport (bPlaybackToggle/bScrubFwd/bScrubBack),
    // and Sony platforming (bResetSonyXY → SonyRawX/Y offset).
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
