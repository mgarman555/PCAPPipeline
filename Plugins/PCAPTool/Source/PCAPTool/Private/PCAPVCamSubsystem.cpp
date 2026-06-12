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

// ── Lifecycle ───────────────────────────────────────────────────────────────

void UPCAPVCamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UPCAPVCamSubsystem::Deinitialize()
{
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

    FTransform Raw;
    if (!ReadLiveLinkTransform(Raw))
    {
        SetStreamStatus(EStreamStatus::Disconnected);
        return;
    }
    SetStreamStatus(EStreamStatus::Connected);

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

// ── Readouts ────────────────────────────────────────────────────────────────

FTransform UPCAPVCamSubsystem::GetCurrentTransform() const
{
    return FTransform(RuntimeState.SmoothedRotation, RuntimeState.SmoothedPosition);
}

float UPCAPVCamSubsystem::GetCurrentFocalLength() const
{
    return ActiveConfig ? ActiveConfig->ActiveFocalLength : 0.f;
}
