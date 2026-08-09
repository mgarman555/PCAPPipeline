#include "PCAPTakeRecorderSubsystem.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"
#include "PCAPTakeRecordWriter.h"
#include "StageConfigAsset.h"
#include "PCAPVCamSubsystem.h"
#include "PCAPVCamActor.h"
#include "Misc/ScopeExit.h"      // ON_SCOPE_EXIT — release transport ownership on every exit path
#include "UObject/LazyObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Engine/Engine.h"
#include "LevelSequence.h"
#include "MovieScene.h"

#if WITH_EDITOR
#include "Editor.h"             // GEditor
#include "EditorSubsystem.h"    // UEditorSubsystem — the session-state probe's second shape
#endif

#include "Recorder/TakeRecorderBlueprintLibrary.h"
#include "Recorder/TakeRecorderParameters.h"
#include "Recorder/TakeRecorderSubsystem.h"
#include "Recorder/TakeRecorder.h"
#include "TakeRecorderSource.h"
#include "TakeRecorderSources.h"
#include "TakeMetaData.h"
#include "TakePreset.h"

// ── Reflected names: the Mocap Manager's live session state ─────────────────
// UNVERIFIED-PENDING-WINDOWS, and INTENTIONALLY EMPTY.
//
// The Workflow plugin's session objects live in its private (editor) module. Its record
// structs are known — FPCapSessionRecord and friends are read by UPCAPTakeRecordWriter —
// but none of them carries an "is this session live right now" flag, and the object that
// does hold that state could not be identified from the headers available here. Rather
// than guess a /Script path, the probe ships switched off: fill both constants in once the
// holder is confirmed on Windows and detection upgrades itself with no other change.
//
// Nothing depends on this. The deferral policy is driven by transport ownership (a take in
// flight that PCAPTool did not start), which needs no reflection at all — the probe only
// buys earlier detection, before the official recorder rolls.
static const TCHAR* GPCapSessionStateClassPath = TEXT("");   // e.g. "/Script/PerformanceCaptureWorkflow.<SessionStateClass>"
static const TCHAR* GPCapSessionActiveProperty = TEXT("");   // bool / FGuid / object field meaning "a session is live"

// ── Lifecycle ───────────────────────────────────────────────────────────────

void UPCAPTakeRecorderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Bind to Take Recorder's record events (the BP-library setters are deprecated in 5.4+).
    if (GEngine)
    {
        if (UTakeRecorderSubsystem* TR = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
        {
            TR->TakeRecorderStarted.AddDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeStarted);
            TR->TakeRecorderFinished.AddDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeFinished);
        }
    }
}

void UPCAPTakeRecorderSubsystem::Deinitialize()
{
    if (GEngine)
    {
        if (UTakeRecorderSubsystem* TR = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
        {
            TR->TakeRecorderStarted.RemoveDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeStarted);
            TR->TakeRecorderFinished.RemoveDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeFinished);
        }
    }
    bPCAPDrivesActiveTake    = false;
    bObservingExternalRecord = false;
    ClearPendingTake();
    Super::Deinitialize();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

UMocapDatabase* UPCAPTakeRecorderSubsystem::GetDB() const
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    return Settings ? Settings->GetDatabase() : nullptr;
}

void UPCAPTakeRecorderSubsystem::SetState(EPCAPRecordState NewState)
{
    if (RecordState == NewState) return;
    RecordState = NewState;
    OnRecordStateChanged.Broadcast(NewState);
}

bool UPCAPTakeRecorderSubsystem::IsRecording() const
{
    return UTakeRecorderBlueprintLibrary::IsRecording();
}

FString UPCAPTakeRecorderSubsystem::PeekNextTakeID() const
{
    UMocapDatabase* DB = GetDB();
    return DB ? DB->BuildNextTakeID() : FString();
}

void UPCAPTakeRecorderSubsystem::ClearPendingTake()
{
    PendingTakeID.Empty();
    PendingProductionCode.Empty();
    PendingDayID.Empty();
    PendingSessionID.Empty();
    PendingShotID.Empty();
    PendingHasVCam = false;
}

// ── Mocap Manager deferral ──────────────────────────────────────────────────
//
// There is exactly ONE Take Recorder transport. UE 5.8's Mocap Manager drives it too, so
// without a policy both controllers can call StartRecording and the operator gets a
// double-record — two sequences for one performance, or a take that stops halfway. The
// conservative default is that whoever got there first owns the transport.

// The Mocap Manager's live session state is held in the Workflow plugin's private module and
// we could not confirm WHICH object holds it against the real 5.8 headers (the data-asset and
// record types were verified; the runtime session holder was not). Rather than guess a class
// path and silently return a wrong answer, the probe reports "unresolvable" and the caller
// falls back to transport ownership — which is directly observable and always correct.
// Confirm the holder on Windows and this becomes a positive signal; see the design spec.
bool UPCAPTakeRecorderSubsystem::ProbeMocapManagerSessionFlag(bool& bOutActive)
{
    bOutActive = false;

    static bool bLoggedUnresolved = false;
    if (!bLoggedUnresolved)
    {
        bLoggedUnresolved = true;
        UE_LOG(LogTemp, Log,
            TEXT("[PCAP] Mocap Manager session-state probe is not wired to a confirmed holder; ")
            TEXT("deferral falls back to Take Recorder transport ownership."));
    }
    return false;   // unresolved — bOutActive carries no meaning
}

bool UPCAPTakeRecorderSubsystem::IsMocapManagerSessionActive() const
{
    // Signal 1: the official session flag, when we can actually read it.
    bool bProbeSaysActive = false;
    if (ProbeMocapManagerSessionFlag(bProbeSaysActive))
    {
        return bProbeSaysActive;
    }

    // Signal 2 (fallback): transport ownership. A recording is in flight that PCAPTool did not
    // start, so something else is driving Take Recorder. That is exactly the condition we must
    // not add a second controller to, whoever owns it.
    return IsRecording() && !bPCAPDrivesActiveTake;
}

bool UPCAPTakeRecorderSubsystem::ShouldDeferToMocapManager() const
{
    switch (DeferralMode)
    {
    case EPCAPRecorderDeferral::AlwaysDefer: return true;
    case EPCAPRecorderDeferral::NeverDefer:  return false;
    case EPCAPRecorderDeferral::Auto:
    default:                                 return IsMocapManagerSessionActive();
    }
}

void UPCAPTakeRecorderSubsystem::SetDeferralMode(EPCAPRecorderDeferral NewMode)
{
    if (DeferralMode == NewMode) return;
    DeferralMode = NewMode;

    if (NewMode == EPCAPRecorderDeferral::NeverDefer)
    {
        // This removes the only guard against two controllers on one transport, so it is never
        // silent — an accidental double-record costs a take that cannot be re-shot.
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Recorder deferral set to NeverDefer — PCAPTool will drive Take Recorder ")
            TEXT("even when the Mocap Manager is recording. Double-record is now possible."));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[PCAP] Recorder deferral mode set to %d."), static_cast<int32>(NewMode));
    }
}

FString UPCAPTakeRecorderSubsystem::GetTransportOwner() const
{
    if (!IsRecording())          return FString();
    if (bPCAPDrivesActiveTake)   return TEXT("PCAPTool");
    return TEXT("Mocap Manager");
}

bool UPCAPTakeRecorderSubsystem::BeginObservedTake()
{
    // Someone else started a take. Attribute it to the operator's active shot so it harvests
    // exactly like one of ours — same take-ID scheme, same manifest — and mark it read-only so
    // StopRecord() never reaches for a transport we do not own.
    UMocapDatabase* DB = GetDB();
    if (!DB) return false;

    const FShot* Shot = DB->GetActiveShot();
    if (!Shot)
    {
        // Nothing to attribute it to. Harvesting would invent a shot, so we stay out entirely.
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] A take started that PCAPTool did not drive, but there is no active shot ")
            TEXT("to attribute it to — it will not be harvested into the database."));
        return false;
    }

    PendingProductionCode = DB->ActiveProductionCode;
    PendingDayID          = DB->ActiveDayID;
    PendingSessionID      = DB->ActiveSessionID;
    PendingShotID         = DB->ActiveShotID;
    PendingTakeID         = DB->BuildNextTakeID();
    PendingHasVCam        = false;   // we did not arm the sources, so we cannot claim a VCam track

    if (PendingTakeID.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Observing an external take but could not derive a take id; not harvesting."));
        ClearPendingTake();
        return false;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[PCAP] Observing an externally-driven take as '%s' (shot '%s') — PCAPTool will ")
        TEXT("harvest and publish it but will not drive the transport."),
        *PendingTakeID, *PendingShotID);
    return true;
}

bool UPCAPTakeRecorderSubsystem::PublishTakeRecord(const FString& TakeID)
{
    if (TakeID.IsEmpty()) return false;

    UMocapDatabase* DB = GetDB();
    if (!DB) return false;

    // Prefer the shot we last harvested into — the operator's console selection may well have
    // moved on by the time a label is set.
    const FShot* Shot = DB->GetShot(LastHarvestedProductionCode, LastHarvestedDayID,
                                    LastHarvestedSessionID, LastHarvestedShotID);
    if (!Shot)
    {
        Shot = DB->GetActiveShot();
    }
    if (!Shot) return false;

    for (const FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == TakeID)
        {
            // Find-or-create by TakeID, so re-publishing after a label edit updates in place
            // rather than duplicating the row.
            return UPCAPTakeRecordWriter::WriteTake(Take);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCAP] PublishTakeRecord: take '%s' not found."), *TakeID);
    return false;
}

bool UPCAPTakeRecorderSubsystem::AreActiveStreamsReady(FString& OutError) const
{
    OutError.Reset();
    UMocapDatabase* DB = GetDB();
    if (!DB) { OutError = TEXT("No database assigned (Project Settings → PCAP)."); return false; }

    FShot* Shot = DB->GetActiveShot();
    if (!Shot) { OutError = TEXT("No active shot selected."); return false; }

    bool bAnyActive = false;
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        bAnyActive = true;

        if (Subj.bHasBodyStream && Subj.BodyStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s body stream not connected."), *Subj.ActorID);
            return false;
        }
        if (Subj.bHasFaceStream && Subj.FaceStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s face stream not connected."), *Subj.ActorID);
            return false;
        }
        for (const FAudioStreamEntry& A : Subj.AudioStreams)
        {
            if (A.StreamStatus != EStreamStatus::Connected)
            {
                OutError = FString::Printf(TEXT("%s audio '%s' not connected."), *Subj.ActorID, *A.ChannelID);
                return false;
            }
        }
    }

    if (!bAnyActive) { OutError = TEXT("No actors are called (active) on this shot."); return false; }
    return true;
}

// ── Record flow ───────────────────────────────────────────────────────────────

bool UPCAPTakeRecorderSubsystem::StartRecordForActiveShot(FString& OutError)
{
    if (IsRecording()) { OutError = TEXT("A recording is already in progress."); return false; }

    // One transport, one controller. If the Mocap Manager is driving Take Recorder, starting
    // here would double-record the performance — refuse and say who has it.
    if (ShouldDeferToMocapManager())
    {
        OutError = TEXT("The Mocap Manager is driving Take Recorder — PCAPTool is deferring to it. "
                        "Record from the Mocap Manager; the take is still harvested and published here.");
        return false;
    }

    if (!AreActiveStreamsReady(OutError)) return false;

    UMocapDatabase* DB = GetDB();
    FShot* Shot = DB->GetActiveShot();   // non-null: AreActiveStreamsReady validated it

    // Capture the take identity for the harvest step.
    PendingProductionCode = DB->ActiveProductionCode;
    PendingDayID          = DB->ActiveDayID;
    PendingSessionID      = DB->ActiveSessionID;
    PendingShotID         = DB->ActiveShotID;
    PendingTakeID         = DB->BuildNextTakeID();
    if (PendingTakeID.IsEmpty()) { OutError = TEXT("Could not derive next take id."); return false; }

    // Base sequence + sources + metadata — mirrors STakeRecorderPanel: a transient preset
    // hands us a level sequence, and the sources/metadata live as metadata on it.
    UTakePreset* Preset = NewObject<UTakePreset>(GetTransientPackage(), NAME_None, RF_Transient);
    ULevelSequence* LevelSequence = Preset->GetOrCreateLevelSequence();
    if (!LevelSequence) { OutError = TEXT("Failed to create base level sequence."); return false; }

    UTakeRecorderSources* Sources  = LevelSequence->FindOrAddMetaData<UTakeRecorderSources>();
    UTakeMetaData*        MetaData = LevelSequence->FindOrAddMetaData<UTakeMetaData>();
    if (!Sources || !MetaData) { OutError = TEXT("Failed to allocate take sources/metadata."); return false; }

    // Arm a Live Link source per active body/face subject (hybrid: a stage preset may add more).
    // NOTE: audio (TakeRecorderMicrophoneAudioSource) arming is the next reflection pass — see
    // header. Body/face here + VCam (below) cover the primary streams; audio follows.
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        if (Subj.bHasBodyStream && !Subj.BodyStream.LiveLinkSubjectName.IsNone())
        {
            AddLiveLinkSource(Sources, Subj.BodyStream.LiveLinkSubjectName);
        }
        if (Subj.bHasFaceStream && !Subj.FaceStream.LiveLinkSubjectName.IsNone())
        {
            AddLiveLinkSource(Sources, Subj.FaceStream.LiveLinkSubjectName);
        }
    }

    // Arm the VCam camera actor as a source if the active stage records a vcam.
    PendingHasVCam = false;
    if (UStageConfigAsset* Stage = DB->GetActiveStageConfig())
    {
        if (Stage->VCamSystem != EVCamSystem::None)
        {
            if (UPCAPVCamSubsystem* VCamSys = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
            {
                if (AActor* Cam = VCamSys->GetOrCreateVCamActor())
                {
                    PendingHasVCam = AddActorSource(Sources, Cam);
                }
            }
        }
    }

    // Slate = ShotID, take number = trailing 3 digits of the TakeID ("001003_004" → 4).
    MetaData->SetSlate(PendingShotID, /*bEmitChanged*/ false);
    MetaData->SetTakeNumber(FCString::Atoi(*PendingTakeID.Right(3)), /*bEmitChanged*/ false);

    // Output path: /Game/Mocap/Productions/<Code>/Day_<Day>/Session_<Sess>/Shot_<Shot>/<TakeID>/
    FTakeRecorderParameters Params = UTakeRecorderBlueprintLibrary::GetDefaultParameters();
    Params.Project.RootTakeSaveDir.Path = FString::Printf(
        TEXT("%s/%s/Day_%s/Session_%s/Shot_%s"),
        *PCAPPaths::Productions(), *PendingProductionCode, *PendingDayID, *PendingSessionID, *PendingShotID);
    Params.Project.TakeSaveDir = PendingTakeID;   // the take's own folder

    // Claim the transport BEFORE starting: TakeRecorderStarted fires from inside StartRecording,
    // and HandleTakeStarted reads this flag to decide whether the take is ours or one to observe.
    bPCAPDrivesActiveTake = true;

    UTakeRecorder* Recorder = UTakeRecorderBlueprintLibrary::StartRecording(LevelSequence, Sources, MetaData, Params);
    if (!Recorder)
    {
        bPCAPDrivesActiveTake = false;   // nothing started, so release the claim
        ClearPendingTake();
        OutError = TEXT("Take Recorder failed to start — see Output Log.");
        return false;
    }
    // RecordState flips to Capturing via HandleTakeStarted.
    return true;
}

void UPCAPTakeRecorderSubsystem::StopRecord()
{
    // Never stop a take we did not start — the Mocap Manager operator is mid-performance and
    // stopping their transport from here would cut the take.
    if (bObservingExternalRecord)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] StopRecord ignored: the take in flight is driven by the Mocap Manager. ")
            TEXT("Stop it there."));
        return;
    }

    if (IsRecording())
    {
        UTakeRecorderBlueprintLibrary::StopRecording();
    }
    // RecordState flips via HandleTakeFinished.
}

bool UPCAPTakeRecorderSubsystem::RecordNextTake(FString& OutError)
{
    // One-tap: same setup, next take number. BuildNextTakeID already advances off the shot's
    // current take count, so a plain start records the next take.
    return StartRecordForActiveShot(OutError);
}

void UPCAPTakeRecorderSubsystem::FinishReview()
{
    // The operator has just labelled the take (Best/Alt/Burn). Re-publish so that label reaches
    // Epic's TakeStatus — the row written at harvest time still carries the default Captured.
    if (!LastHarvestedTakeID.IsEmpty())
    {
        PublishTakeRecord(LastHarvestedTakeID);
    }

    SetState(EPCAPRecordState::Ready);
}

// ── Take Recorder callbacks ─────────────────────────────────────────────────

void UPCAPTakeRecorderSubsystem::HandleTakeStarted()
{
    // This fires for EVERY take on the transport, including the Mocap Manager's — which is what
    // makes observe-mode possible. If we did not start this one, adopt it read-only.
    if (!bPCAPDrivesActiveTake)
    {
        bObservingExternalRecord = BeginObservedTake();
    }

    SetState(EPCAPRecordState::Capturing);
}

void UPCAPTakeRecorderSubsystem::HandleTakeFinished(ULevelSequence* SequenceAsset)
{
    // Whatever happens below, this take is over — the next one re-decides ownership.
    ON_SCOPE_EXIT
    {
        bPCAPDrivesActiveTake    = false;
        bObservingExternalRecord = false;
    };

    UMocapDatabase* DB = GetDB();
    if (!DB) { SetState(EPCAPRecordState::Ready); return; }

    // Empty when an external take could not be attributed to a shot — harvesting it would
    // invent data, so we stay out.
    if (PendingTakeID.IsEmpty()) { SetState(EPCAPRecordState::Ready); return; }

    FShot* Shot = DB->GetShot(PendingProductionCode, PendingDayID, PendingSessionID, PendingShotID);
    if (!Shot) { SetState(EPCAPRecordState::Ready); return; }

    FTake Take;
    Take.TakeID     = PendingTakeID;
    Take.DayID      = PendingDayID;
    Take.ShotID     = PendingShotID;
    Take.SessionID  = PendingSessionID;
    Take.TakeNumber = PendingTakeID.Right(3);
    Take.RecordedAt = FDateTime::UtcNow();
    Take.Label      = ETakeLabel::Captured;
    Take.MasterSequence = SequenceAsset;   // the assembled take
    Take.bHasVCam = PendingHasVCam;   // VCamAsset (the dedicated sub-asset) resolves in the
                                      // same later per-stream pass as Body/Face/Audio refs.

    // Subject manifest (record-time provenance, incl. DrivenTarget).
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        FTakeSubjectSnapshot Snap;
        Snap.ActorID        = Subj.ActorID;
        Snap.CharacterName  = Subj.CharacterName;
        Snap.bHadBodyStream = Subj.bHasBodyStream;
        Snap.bHadFaceStream = Subj.bHasFaceStream;
        for (const FAudioStreamEntry& A : Subj.AudioStreams)
        {
            Snap.AudioChannels.Add(A.ChannelID);
        }
        Snap.DrivenTarget = Subj.DrivenTarget;
        Take.SubjectManifest.Add(Snap);
    }

    // Prop manifest.
    for (const FPropEntry& Prop : Shot->Props)
    {
        FTakePropSnapshot PS;
        PS.PropID      = Prop.PropID;
        PS.bWasTracked = Prop.bIsTracked;
        Take.PropManifest.Add(PS);
    }

    // NOTE: per-stream asset refs (BodyAnimAssets/FaceAnimAssets/AudioAssets) are resolved in a
    // later pass — they're bindings inside the recorded sequence; MasterSequence is the entry point.

    Shot->Takes.Add(Take);
    DB->MarkPackageDirty();   // persisted on the next SaveDatabase()/editor save

    // Remember where it landed so the label the operator is about to set can be republished
    // without depending on wherever the console selection has moved to by then.
    LastHarvestedTakeID         = Take.TakeID;
    LastHarvestedProductionCode = PendingProductionCode;
    LastHarvestedDayID          = PendingDayID;
    LastHarvestedSessionID      = PendingSessionID;
    LastHarvestedShotID         = PendingShotID;

    // Publish the row into Epic's take DataTable so the Mocap Manager's Review tab sees it.
    // A take that fails to publish is still recorded here — the database is the record of
    // truth — so this is logged, never fatal.
    if (!UPCAPTakeRecordWriter::WriteTake(Take))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[PCAP] Take '%s' harvested, but no FPCapTakeRecord row was written ")
            TEXT("(no resolvable Mocap Manager session). The take is safe in the database."),
            *Take.TakeID);
    }

    ClearPendingTake();

    SetState(EPCAPRecordState::Reviewing);   // operator labels the take, then FinishReview()
}

// ── Reflection: arm an engine-private Live Link source ──────────────────────

UClass* UPCAPTakeRecorderSubsystem::FindSourceClass(const TCHAR* ScriptPath)
{
    // The source classes live in Private/ engine modules — resolve by /Script path at runtime.
    return FindObject<UClass>(nullptr, ScriptPath);
}

void UPCAPTakeRecorderSubsystem::SetSourceFName(UObject* Source, const TCHAR* PropName, FName Value)
{
    if (!Source) return;
    if (FNameProperty* Prop = FindFProperty<FNameProperty>(Source->GetClass(), PropName))
    {
        Prop->SetPropertyValue_InContainer(Source, Value);
    }
}

bool UPCAPTakeRecorderSubsystem::AddLiveLinkSource(UObject* Sources, FName SubjectName) const
{
    UTakeRecorderSources* Src = Cast<UTakeRecorderSources>(Sources);
    if (!Src) return false;

    UClass* LiveLinkSourceClass = FindSourceClass(TEXT("/Script/LiveLinkSequencer.TakeRecorderLiveLinkSource"));
    if (!LiveLinkSourceClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] TakeRecorderLiveLinkSource class not found — is the LiveLink plugin loaded?"));
        return false;
    }

    UTakeRecorderSource* NewSrc = Src->AddSource(LiveLinkSourceClass);
    SetSourceFName(NewSrc, TEXT("SubjectName"), SubjectName);
    return NewSrc != nullptr;
}

// ── Reflection: arm an engine-private Actor source (VCam camera) ─────────────
// CONFIRM-AT-BUILD: class path + "Target" property name/type (TLazyObjectPtr<AActor>
// → FLazyObjectProperty) are the 5.7 risk points — log if either lookup is null.

void UPCAPTakeRecorderSubsystem::SetSourceLazyActor(UObject* Source, const TCHAR* PropName, AActor* Value)
{
    if (!Source) { return; }
    if (FLazyObjectProperty* Prop = FindFProperty<FLazyObjectProperty>(Source->GetClass(), PropName))
    {
        FLazyObjectPtr Lazy;
        Lazy = Value;
        Prop->SetPropertyValue_InContainer(Source, Lazy);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Actor source has no '%s' FLazyObjectProperty — confirm against 5.7 source."), PropName);
    }
}

bool UPCAPTakeRecorderSubsystem::AddActorSource(UObject* Sources, AActor* Target) const
{
    UTakeRecorderSources* Src = Cast<UTakeRecorderSources>(Sources);
    if (!Src || !Target) { return false; }

    UClass* ActorSourceClass = FindSourceClass(TEXT("/Script/TakeRecorderSources.TakeRecorderActorSource"));
    if (!ActorSourceClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] TakeRecorderActorSource class not found — confirm /Script path on 5.7."));
        return false;
    }

    UTakeRecorderSource* NewSrc = Src->AddSource(ActorSourceClass);
    SetSourceLazyActor(NewSrc, TEXT("Target"), Target);
    return NewSrc != nullptr;
}
