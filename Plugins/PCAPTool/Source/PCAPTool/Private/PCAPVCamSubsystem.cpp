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
            InputLayer.Layout = (EVCamButtonLayout)FMath::Clamp(ActiveConfig->ActiveButtonLayout, 0, 2);
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

    // Accumulate joystick/d-pad navigation (rate * dt) before processing this frame.
    // Uses last frame's smoothed rotation as the flight-mode basis (changes slowly vs dt).
    FPCAPVCamProcessor::AccumulateNavigate(*ActiveConfig, RuntimeState.NavigateRate,
        RuntimeState.SmoothedRotation, RuntimeState.bFlightMode, DeltaTime);

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

    FLiveLinkSubjectFrameData FrameData;
    const bool bOk = Client.EvaluateFrame_AnyThread(
        ActiveConfig->LiveLinkSubjectName,
        ULiveLinkTransformRole::StaticClass(),
        FrameData);
    if (!bOk) { return false; }

    if (const FLiveLinkTransformFrameData* T = FrameData.FrameData.Cast<FLiveLinkTransformFrameData>())
    {
        OutTransform = T->Transform;
        return true;
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
    if (ActiveConfig) { ActiveConfig->ActiveButtonLayout = FMath::Clamp(Layout, 0, 2); }
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

    FVCamControllerInput In;
    In.LeftJoyX  = Axis(TEXT("left_joy_x"));  In.LeftJoyY  = Axis(TEXT("left_joy_y"));
    In.RightJoyX = Axis(TEXT("right_joy_x")); In.RightJoyY = Axis(TEXT("right_joy_y"));
    In.LeftEnc   = Axis(TEXT("left_enc"));    In.RightEnc  = Axis(TEXT("right_enc"));
    In.LeftTrigger = Btn(TEXT("left_trigger"));  In.RightTrigger = Btn(TEXT("right_trigger"));
    In.LeftUp   = Btn(TEXT("left_up"));   In.LeftDown  = Btn(TEXT("left_down"));
    In.LeftLeft = Btn(TEXT("left_left")); In.LeftRight = Btn(TEXT("left_right"));
    In.RightUp  = Btn(TEXT("right_up"));  In.RightDown = Btn(TEXT("right_down"));
    In.RightLeft= Btn(TEXT("right_left"));In.RightRight= Btn(TEXT("right_right"));
    In.LeftA = Btn(TEXT("left_a")); In.LeftB = Btn(TEXT("left_b"));
    In.RightA= Btn(TEXT("right_a"));In.RightB= Btn(TEXT("right_b"));

    FScopeLock Lock(&InputMutex);
    LatestInput = In;
    bHasInput = true;
}

void UPCAPVCamSubsystem::ApplyInputIntents(const FVCamInputIntents& Intents)
{
    SetNavigateRate(Intents.NavigateRate);

    if (Intents.bHold != bPrevInputHold) { SetHold(Intents.bHold); bPrevInputHold = Intents.bHold; }

    if (!FMath::IsNearlyZero(Intents.ZoomDelta)) { AddFocalLengthDelta(Intents.ZoomDelta); }
    if (Intents.bSetTranslationGain) { SetTranslationGain(Intents.TranslationGain); }
    if (Intents.bSetZoomGain)        { SetZoomGain(Intents.ZoomGain); }

    if (Intents.bZeroEverything) { ZeroSpace(); }
    if (Intents.bSavePosition)   { SaveCurrentPosition(); }
    if (Intents.bLensNext)       { CycleFocalLengthUp(); }
    if (Intents.bLensPrev)       { CycleFocalLengthDown(); }
    if (Intents.bWorldScaleNext) { SelectNextWorldScale(); }
    if (Intents.bWorldScalePrev) { SelectPreviousWorldScale(); }

    if (ActiveConfig)
    {
        if (Intents.bDeletePosition) { DeleteSavedPosition(ActiveConfig->ActiveSavedPositionIndex); }
        if (Intents.bGotoPrev && ActiveConfig->SavedPositions.Num() > 0)
        {
            GotoSavedPosition(FMath::Max(0, ActiveConfig->ActiveSavedPositionIndex - 1));
        }
        if (Intents.bGotoNext && ActiveConfig->SavedPositions.Num() > 0)
        {
            GotoSavedPosition(FMath::Min(ActiveConfig->SavedPositions.Num() - 1, ActiveConfig->ActiveSavedPositionIndex + 1));
        }
    }
    // Intents.bResetSonyXY → platform offset (deferred — platforming not modelled yet).
}
