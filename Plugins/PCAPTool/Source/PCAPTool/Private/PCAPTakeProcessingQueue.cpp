#include "PCAPTakeProcessingQueue.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "Engine/Engine.h"      // GEngine->GetEngineSubsystem
#include "FileHelpers.h"        // FEditorFileUtils::PromptForCheckoutAndSave
#include "UObject/Package.h"    // UPackage::IsDirty

// Editor-session bookkeeping, not take data — same shape as UPCAPTakeRecordWriter's pinned
// session UID. Deliberately not stored on the database asset: whether the operator wants each
// attestation flushed to disk immediately is a preference about this editor run, and the change
// serial only exists so a panel's active timer can tell "something moved" from "nothing did".
static bool  GPCAPQueueAutoSave     = true;
static int32 GPCAPQueueChangeSerial = 0;

// ── Access ──────────────────────────────────────────────────────────────────

UPCAPTakeProcessingQueue* UPCAPTakeProcessingQueue::Get()
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPTakeProcessingQueue>() : nullptr;
}

UMocapDatabase* UPCAPTakeProcessingQueue::GetProjectDatabase()
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    return Settings ? Settings->GetDatabase() : nullptr;
}

// ── Step metadata ───────────────────────────────────────────────────────────

TArray<EPCAPProcessingStep> UPCAPTakeProcessingQueue::PipelineSteps()
{
    // Pipeline order, not FTakeProcessingState's member order — the merge runs last.
    return TArray<EPCAPProcessingStep>({
        EPCAPProcessingStep::BodySolveCleanup,
        EPCAPProcessingStep::HMCSolve,
        EPCAPProcessingStep::BodyRetarget,
        EPCAPProcessingStep::AudioSyncTrim,
        EPCAPProcessingStep::MergeToSequencer
    });
}

FString UPCAPTakeProcessingQueue::StepDisplayName(EPCAPProcessingStep Step)
{
    switch (Step)
    {
        case EPCAPProcessingStep::BodySolveCleanup: return TEXT("Body Solve Cleanup");
        case EPCAPProcessingStep::HMCSolve:         return TEXT("HMC Solve");
        case EPCAPProcessingStep::BodyRetarget:     return TEXT("Body Retarget");
        case EPCAPProcessingStep::AudioSyncTrim:    return TEXT("Audio Sync / Trim");
        case EPCAPProcessingStep::MergeToSequencer: return TEXT("Merge to Sequencer");
    }
    return FString();
}

bool UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep Step)
{
    // These two are not Unreal operations and never will be. The other three could in principle
    // be engine work; today none of them has an executor either — see StepExecutionNote.
    return Step == EPCAPProcessingStep::BodySolveCleanup
        || Step == EPCAPProcessingStep::HMCSolve;
}

FString UPCAPTakeProcessingQueue::StepExecutionNote(EPCAPProcessingStep Step)
{
    switch (Step)
    {
        case EPCAPProcessingStep::BodySolveCleanup:
            return TEXT("External — solved in Shogun Post / Motive. Unreal cannot perform this step; the operator marks it.");

        case EPCAPProcessingStep::HMCSolve:
            return TEXT("External — solved in MetaHuman Animator or the head-cam vendor's tool. Unreal cannot perform this step; the operator marks it.");

        case EPCAPProcessingStep::BodyRetarget:
            return TEXT("In-engine in principle (IK Retargeter), but no automation is wired and the take carries no body anim asset to retarget. The operator marks it.");

        case EPCAPProcessingStep::AudioSyncTrim:
            return TEXT("In-engine in principle (sequence audio offset / trim), but no audio asset ever reaches the take. The operator marks it.");

        case EPCAPProcessingStep::MergeToSequencer:
            return TEXT("In-engine in principle (Sequencer assembly), but no automation is wired. The operator marks it.");
    }
    return FString();
}

// ── Applicability ───────────────────────────────────────────────────────────

FPCAPProcessingPlan UPCAPTakeProcessingQueue::MakeProcessingPlan(const FTake& Take)
{
    // The manifest is the only record-time evidence of what a take contains. These are the
    // streams that were ARMED for the take — FTakeSubjectSnapshot copies the shot subject's
    // configured flags at record time — which is the right question for applicability but is not
    // a claim that any stream was healthy.
    bool bAnyBody  = false;
    bool bAnyFace  = false;
    bool bAnyAudio = false;

    for (const FTakeSubjectSnapshot& Subject : Take.SubjectManifest)
    {
        if (Subject.bHadBodyStream)          { bAnyBody  = true; }
        if (Subject.bHadFaceStream)          { bAnyFace  = true; }
        if (Subject.AudioChannels.Num() > 0) { bAnyAudio = true; }
    }

    FPCAPProcessingPlan Plan;
    Plan.bApplyBodySolve     = bAnyBody;
    Plan.bApplyHMCSolve      = bAnyFace;    // never an HMC solve on a take with no face stream
    Plan.bApplyBodyRetarget  = bAnyBody;
    Plan.bApplyAudioSyncTrim = bAnyAudio;

    // Merge is unconditional, so the count is never zero for a real take.
    Plan.ApplicableStepCount = 1
        + (bAnyBody  ? 2 : 0)               // body solve cleanup + body retarget
        + (bAnyFace  ? 1 : 0)
        + (bAnyAudio ? 1 : 0);

    return Plan;
}

bool UPCAPTakeProcessingQueue::StepApplies(const FTakeProcessingState& State, EPCAPProcessingStep Step)
{
    switch (Step)
    {
        case EPCAPProcessingStep::BodySolveCleanup: return State.bApplyBodySolve;
        case EPCAPProcessingStep::HMCSolve:         return State.bApplyHMCSolve;
        case EPCAPProcessingStep::BodyRetarget:     return State.bApplyBodyRetarget;
        case EPCAPProcessingStep::AudioSyncTrim:    return State.bApplyAudioSyncTrim;

        // Unconditional for an admitted take — the locked pipeline always merges, which is
        // exactly why FTakeProcessingState carries no bApplyMergeToSequencer. Before admission
        // nothing applies, because the plan has not been computed yet.
        case EPCAPProcessingStep::MergeToSequencer: return State.bHasQueued;
    }
    return false;
}

EProcessingStatus UPCAPTakeProcessingQueue::RollUpStatus(const FTakeProcessingState& State)
{
    // A take that was never admitted has no queue state to roll up.
    if (!State.bHasQueued)
    {
        return EProcessingStatus::Pending;
    }

    int32 ApplicableCount = 0;
    int32 CompleteCount   = 0;
    bool  bAnyFailed      = false;
    bool  bAnyInProgress  = false;

    for (const EPCAPProcessingStep Step : PipelineSteps())
    {
        if (!StepApplies(State, Step))
        {
            continue;
        }
        ++ApplicableCount;

        const FProcessingStep* Entry = FindStep(State, Step);
        if (!Entry)
        {
            continue;
        }

        switch (Entry->Status)
        {
            case EProcessingStatus::Failed:     bAnyFailed     = true; break;
            case EProcessingStatus::InProgress: bAnyInProgress = true; break;
            case EProcessingStatus::Complete:   ++CompleteCount;       break;
            default: break;
        }
    }

    if (ApplicableCount == 0)
    {
        return EProcessingStatus::Pending;
    }

    // Failure wins outright: a day's queue holding one failed step must read Failed, never
    // "mostly complete". After that, anything in flight, then partial progress, then Queued.
    if (bAnyFailed)                       { return EProcessingStatus::Failed; }
    if (bAnyInProgress)                   { return EProcessingStatus::InProgress; }
    if (CompleteCount == ApplicableCount) { return EProcessingStatus::Complete; }
    if (CompleteCount > 0)                { return EProcessingStatus::InProgress; }

    return EProcessingStatus::Queued;
}

// ── Take / step resolution ──────────────────────────────────────────────────

FTake* UPCAPTakeProcessingQueue::FindTake(UMocapDatabase& DB, const FPCAPTakeKey& Key)
{
    if (!Key.IsValid())
    {
        return nullptr;
    }

    // GetShot takes the session; GetTake does not, and would return the first take with this ID
    // in any session of the day. Shot slots are reused across sessions, so that lookup can
    // silently land on the wrong take — the exact case a multi-session day makes real.
    FShot* Shot = DB.GetShot(Key.ProjectCode, Key.DayID, Key.SessionID, Key.ShotID);
    if (!Shot)
    {
        return nullptr;
    }

    for (FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == Key.TakeID)
        {
            return &Take;
        }
    }
    return nullptr;
}

void UPCAPTakeProcessingQueue::GatherTakeKeys(UMocapDatabase& DB, const FString& ProjectCodeFilter,
                                              const FString& DayIDFilter, TArray<FPCAPTakeKey>& OutKeys)
{
    for (const FProduction& Production : DB.Productions)
    {
        if (!ProjectCodeFilter.IsEmpty() && Production.ProjectCode != ProjectCodeFilter)
        {
            continue;
        }

        for (const FShootDay& Day : Production.Days)
        {
            if (!DayIDFilter.IsEmpty() && Day.DayID != DayIDFilter)
            {
                continue;
            }

            for (const FSession& Session : Day.Sessions)
            {
                for (const FShot& Shot : Session.Shots)
                {
                    for (const FTake& Take : Shot.Takes)
                    {
                        FPCAPTakeKey Key;
                        Key.ProjectCode = Production.ProjectCode;   // only knowable here
                        Key.DayID       = Day.DayID;
                        Key.SessionID   = Session.SessionID;
                        Key.ShotID      = Shot.ShotID;
                        Key.TakeID      = Take.TakeID;
                        OutKeys.Add(Key);
                    }
                }
            }
        }
    }
}

FProcessingStep* UPCAPTakeProcessingQueue::FindStep(FTakeProcessingState& State, EPCAPProcessingStep Step)
{
    switch (Step)
    {
        case EPCAPProcessingStep::BodySolveCleanup: return &State.BodySolveCleanup;
        case EPCAPProcessingStep::HMCSolve:         return &State.HMCSolve;
        case EPCAPProcessingStep::BodyRetarget:     return &State.BodyRetarget;
        case EPCAPProcessingStep::AudioSyncTrim:    return &State.AudioSyncTrim;
        case EPCAPProcessingStep::MergeToSequencer: return &State.MergeToSequencer;
    }
    return nullptr;
}

const FProcessingStep* UPCAPTakeProcessingQueue::FindStep(const FTakeProcessingState& State,
                                                          EPCAPProcessingStep Step)
{
    return FindStep(const_cast<FTakeProcessingState&>(State), Step);
}

FTake* UPCAPTakeProcessingQueue::ResolveForWrite(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason)
{
    if (!DB)
    {
        OutReason = FText::FromString(
            TEXT("No PCAP database is assigned — set one in Project Settings under PCAP."));
        return nullptr;
    }

    FTake* Take = FindTake(*DB, Key);
    if (!Take)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' is no longer in the database."), *Key.ToDisplayString()));
        return nullptr;
    }
    return Take;
}

// ── Dependency table ────────────────────────────────────────────────────────

bool UPCAPTakeProcessingQueue::StepDependsOn(EPCAPProcessingStep Step, EPCAPProcessingStep Prerequisite)
{
    switch (Step)
    {
        // You cannot retarget a body that has not been cleaned up.
        case EPCAPProcessingStep::BodyRetarget:
            return Prerequisite == EPCAPProcessingStep::BodySolveCleanup;

        // The terminal step consumes everything else. Listing all four keeps the table
        // transitively complete, so an upstream reset reaches the merge without a chain walk.
        case EPCAPProcessingStep::MergeToSequencer:
            return Prerequisite != EPCAPProcessingStep::MergeToSequencer;

        // Body solve cleanup, the HMC solve and audio sync/trim are independent of one another.
        // They happen at different stations and often in parallel; ordering them would be a
        // fiction the operator would only have to work around.
        default:
            return false;
    }
}

bool UPCAPTakeProcessingQueue::ArePrerequisitesMet(const FTakeProcessingState& State,
                                                   EPCAPProcessingStep Step, FText& OutReason)
{
    for (const EPCAPProcessingStep Candidate : PipelineSteps())
    {
        if (Candidate == Step || !StepDependsOn(Step, Candidate))
        {
            continue;
        }
        if (!StepApplies(State, Candidate))
        {
            continue;   // a step that does not apply to this take cannot block anything
        }

        const FProcessingStep* Entry = FindStep(State, Candidate);
        if (!Entry || Entry->Status != EProcessingStatus::Complete)
        {
            OutReason = FText::FromString(FString::Printf(
                TEXT("%s cannot start until %s is marked complete."),
                *StepDisplayName(Step), *StepDisplayName(Candidate)));
            return false;
        }
    }
    return true;
}

// ── Eligibility / transition guards ─────────────────────────────────────────

bool UPCAPTakeProcessingQueue::IsEligibleForProcessing(const FTake& Take, FString& OutReason)
{
    if (Take.Label == ETakeLabel::Burn)
    {
        OutReason = TEXT("is labelled Burn — Burn takes are archived raw and are never processed.");
        return false;
    }
    if (Take.Label != ETakeLabel::Best && Take.Label != ETakeLabel::Alt)
    {
        OutReason = TEXT("is labelled Captured — only Best and Alt takes enter the processing queue.");
        return false;
    }

    // Every shoot day is seeded with placeholder takes on the calibration / test / retarget shot
    // slots. They have no record timestamp and nothing to process; a label on one of those would
    // otherwise put an empty slot into the day's worklist.
    if (Take.RecordedAt == FDateTime(0))
    {
        OutReason = TEXT("has no record timestamp — it is a seeded shot slot, not a recorded take.");
        return false;
    }

    if (Take.ProcessingState.OverallStatus == EProcessingStatus::Complete)
    {
        OutReason = TEXT("has already finished processing.");
        return false;
    }

    OutReason.Empty();
    return true;
}

bool UPCAPTakeProcessingQueue::ValidateStepContext(const FTake& Take, EPCAPProcessingStep Step,
                                                   FText& OutReason)
{
    // Re-checked on every transition rather than trusted from admission: nothing stops a take
    // being relabelled after it was queued, and a Burn take must never be worked on afterwards.
    if (Take.Label == ETakeLabel::Burn)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' was relabelled Burn after it was queued — Burn takes are archived raw and are never processed. Remove it from the queue."),
            *Take.TakeID));
        return false;
    }
    if (Take.Label != ETakeLabel::Best && Take.Label != ETakeLabel::Alt)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' was relabelled Captured after it was queued. Relabel it Best or Alt, or remove it from the queue."),
            *Take.TakeID));
        return false;
    }

    if (!Take.ProcessingState.bHasQueued)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' is not in the processing queue — queue it first."), *Take.TakeID));
        return false;
    }

    if (!StepApplies(Take.ProcessingState, Step))
    {
        // Name the missing stream rather than saying "not applicable" — this is the message an
        // operator sees when they try to solve a face that was never captured.
        FString MissingStream = TEXT("body stream");
        if (Step == EPCAPProcessingStep::HMCSolve)
        {
            MissingStream = TEXT("face stream");
        }
        else if (Step == EPCAPProcessingStep::AudioSyncTrim)
        {
            MissingStream = TEXT("audio channels");
        }

        OutReason = FText::FromString(FString::Printf(
            TEXT("%s does not apply to take '%s' — its manifest records no %s."),
            *StepDisplayName(Step), *Take.TakeID, *MissingStream));
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

// ── Step mutation primitives ────────────────────────────────────────────────

void UPCAPTakeProcessingQueue::ClearStepToPending(FProcessingStep& OutStep)
{
    // Fully cleared, StepName included: post-admission an empty step is a step that does not
    // apply to this take, and it must carry no residue from a previous plan.
    OutStep.StepName.Empty();
    OutStep.Status        = EProcessingStatus::Pending;
    OutStep.StartedAt     = FDateTime(0);
    OutStep.CompletedAt   = FDateTime(0);
    OutStep.bHasStarted   = false;
    OutStep.bHasCompleted = false;
    OutStep.ErrorMessage.Empty();
}

void UPCAPTakeProcessingQueue::ResetStepToQueued(FProcessingStep& OutStep)
{
    // Keeps StepName — the step still applies, it is just outstanding work again.
    OutStep.Status        = EProcessingStatus::Queued;
    OutStep.StartedAt     = FDateTime(0);
    OutStep.CompletedAt   = FDateTime(0);
    OutStep.bHasStarted   = false;
    OutStep.bHasCompleted = false;
    OutStep.ErrorMessage.Empty();
}

void UPCAPTakeProcessingQueue::ResetStepAndDependents(FTakeProcessingState& State, EPCAPProcessingStep Step)
{
    for (const EPCAPProcessingStep Candidate : PipelineSteps())
    {
        const bool bIsTarget    = (Candidate == Step);
        const bool bIsDependent = StepDependsOn(Candidate, Step);
        if (!bIsTarget && !bIsDependent)
        {
            continue;
        }
        if (!StepApplies(State, Candidate))
        {
            continue;
        }

        if (FProcessingStep* Entry = FindStep(State, Candidate))
        {
            ResetStepToQueued(*Entry);
        }
    }
}

void UPCAPTakeProcessingQueue::RefreshOverallStatus(FTakeProcessingState& State)
{
    const EProcessingStatus NewStatus = RollUpStatus(State);

    if (NewStatus == EProcessingStatus::Complete)
    {
        if (!State.bHasCompleted)   // stamp once — a re-roll must not move the completion time
        {
            State.bHasCompleted = true;
            State.CompletedAt   = FDateTime::UtcNow();
        }
    }
    else
    {
        State.bHasCompleted = false;
        State.CompletedAt   = FDateTime(0);
    }

    State.OverallStatus = NewStatus;
}

void UPCAPTakeProcessingQueue::AdmitTake(FTake& Take)
{
    const FPCAPProcessingPlan Plan = MakeProcessingPlan(Take);

    FTakeProcessingState& State = Take.ProcessingState;

    State.bApplyBodySolve     = Plan.bApplyBodySolve;
    State.bApplyHMCSolve      = Plan.bApplyHMCSolve;
    State.bApplyBodyRetarget  = Plan.bApplyBodyRetarget;
    State.bApplyAudioSyncTrim = Plan.bApplyAudioSyncTrim;

    State.QueuedAt      = FDateTime::UtcNow();
    State.bHasQueued    = true;
    State.CompletedAt   = FDateTime(0);
    State.bHasCompleted = false;

    // Applicable steps go straight to Queued. That is what disambiguates step-level Pending:
    // once a take is admitted, a Pending step means "does not apply to this take" and nothing
    // else. EProcessingStatus has no Skipped enumerator, and adding one to a UENUM persisted
    // inside the database asset is not a change worth making for a distinction the bApply* flags
    // already carry.
    for (const EPCAPProcessingStep Step : PipelineSteps())
    {
        FProcessingStep* Entry = FindStep(State, Step);
        if (!Entry)
        {
            continue;
        }

        ClearStepToPending(*Entry);
        if (StepApplies(State, Step))
        {
            Entry->StepName = StepDisplayName(Step);
            Entry->Status   = EProcessingStatus::Queued;
        }
    }

    RefreshOverallStatus(State);
}

// ── Enumeration ─────────────────────────────────────────────────────────────

TArray<FPCAPTakeKey> UPCAPTakeProcessingQueue::CollectEligibleTakes(UMocapDatabase* DB,
                                                                    const FString& ProjectCode,
                                                                    const FString& DayID)
{
    TArray<FPCAPTakeKey> Result;
    if (!DB)
    {
        return Result;
    }

    TArray<FPCAPTakeKey> Candidates;
    GatherTakeKeys(*DB, ProjectCode, DayID, Candidates);

    for (const FPCAPTakeKey& Key : Candidates)
    {
        const FTake* Take = FindTake(*DB, Key);
        if (!Take)
        {
            continue;
        }

        FString Reason;
        if (IsEligibleForProcessing(*Take, Reason))
        {
            Result.Add(Key);
        }
    }
    return Result;
}

TArray<FPCAPTakeKey> UPCAPTakeProcessingQueue::CollectQueuedTakes(UMocapDatabase* DB)
{
    TArray<FPCAPTakeKey> Result;
    if (!DB)
    {
        return Result;
    }

    TArray<FPCAPTakeKey> Candidates;
    GatherTakeKeys(*DB, FString(), FString(), Candidates);

    for (const FPCAPTakeKey& Key : Candidates)
    {
        const FTake* Take = FindTake(*DB, Key);
        if (!Take || !Take->ProcessingState.bHasQueued)
        {
            continue;
        }

        const EProcessingStatus Status = Take->ProcessingState.OverallStatus;
        if (Status == EProcessingStatus::Queued
            || Status == EProcessingStatus::InProgress
            || Status == EProcessingStatus::Failed)
        {
            Result.Add(Key);
        }
    }
    return Result;
}

TArray<FPCAPTakeKey> UPCAPTakeProcessingQueue::CollectStrandedQueuedTakes(UMocapDatabase* DB)
{
    TArray<FPCAPTakeKey> Result;
    if (!DB)
    {
        return Result;
    }

    TArray<FPCAPTakeKey> Candidates;
    GatherTakeKeys(*DB, FString(), FString(), Candidates);

    for (const FPCAPTakeKey& Key : Candidates)
    {
        const FTake* Take = FindTake(*DB, Key);
        if (!Take || !Take->ProcessingState.bHasQueued)
        {
            continue;
        }
        if (Take->Label != ETakeLabel::Best && Take->Label != ETakeLabel::Alt)
        {
            Result.Add(Key);
        }
    }
    return Result;
}

bool UPCAPTakeProcessingQueue::ReadProcessingState(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                                   FTakeProcessingState& OutState)
{
    if (!DB)
    {
        return false;
    }

    const FTake* Take = FindTake(*DB, Key);
    if (!Take)
    {
        return false;
    }

    OutState = Take->ProcessingState;   // copy — the caller must never hold database storage
    return true;
}

// ── Queueing (admission) ────────────────────────────────────────────────────

FPCAPQueueReport UPCAPTakeProcessingQueue::RunQueuePass(UMocapDatabase* DB, const FString& ProjectCode,
                                                        const FString& DayID, bool bCommit)
{
    FPCAPQueueReport Report;

    if (!DB)
    {
        Report.SkipReasons.Add(TEXT("No PCAP database is assigned — set one in Project Settings under PCAP."));
        return Report;
    }

    // Collect the coordinates first, then re-resolve one take at a time. Admission does not
    // resize any array here, but the record path appends to FShot::Takes from a Take Recorder
    // callback and could fire mid-pass, so no FTake* survives across an iteration.
    TArray<FPCAPTakeKey> Candidates;
    GatherTakeKeys(*DB, ProjectCode, DayID, Candidates);

    // Per-step tallies over the takes this pass queues. Counted from the plan rather than from
    // written state, so a preview and the commit that follows it report identically.
    const TArray<EPCAPProcessingStep> Steps = PipelineSteps();
    TArray<int32> StepTallies;
    for (int32 Index = 0; Index < Steps.Num(); ++Index)
    {
        StepTallies.Add(0);
    }

    for (const FPCAPTakeKey& Key : Candidates)
    {
        FTake* Take = FindTake(*DB, Key);
        if (!Take)
        {
            continue;   // vanished between the walk and here — nothing to report, nothing to do
        }

        // Already admitted: left exactly as it is. The plan is computed once, at admission, so a
        // second press cannot clobber a bApply* flag the operator adjusted by hand.
        if (Take->ProcessingState.bHasQueued)
        {
            ++Report.NumAlreadyQueued;
            ++Report.NumSkipped;
            Report.SkipReasons.Add(FString::Printf(
                TEXT("%s is already in the processing queue."), *Key.ToDisplayString()));
            continue;
        }

        FString Reason;
        if (!IsEligibleForProcessing(*Take, Reason))
        {
            // Report every take the operator deliberately labelled — including Burn, so the
            // end-of-day pass shows the archived-raw rule being applied rather than takes quietly
            // vanishing. Captured is the untouched default and is the overwhelming majority of a
            // shoot day; listing those would bury the skips that matter.
            if (Take->Label != ETakeLabel::Captured)
            {
                ++Report.NumSkipped;
                Report.SkipReasons.Add(FString::Printf(TEXT("%s %s"), *Key.ToDisplayString(), *Reason));
            }
            continue;
        }

        const FPCAPProcessingPlan Plan = MakeProcessingPlan(*Take);
        for (int32 Index = 0; Index < Steps.Num(); ++Index)
        {
            bool bApplies = false;
            switch (Steps[Index])
            {
                case EPCAPProcessingStep::BodySolveCleanup: bApplies = Plan.bApplyBodySolve;     break;
                case EPCAPProcessingStep::HMCSolve:         bApplies = Plan.bApplyHMCSolve;      break;
                case EPCAPProcessingStep::BodyRetarget:     bApplies = Plan.bApplyBodyRetarget;  break;
                case EPCAPProcessingStep::AudioSyncTrim:    bApplies = Plan.bApplyAudioSyncTrim; break;
                case EPCAPProcessingStep::MergeToSequencer: bApplies = true;                     break;
            }
            if (bApplies)
            {
                ++StepTallies[Index];
            }
        }

        if (bCommit)
        {
            AdmitTake(*Take);
        }
        ++Report.NumQueued;
    }

    for (int32 Index = 0; Index < Steps.Num(); ++Index)
    {
        if (StepTallies[Index] <= 0)
        {
            continue;   // steps no queued take carries are omitted, not listed as zero
        }

        FPCAPQueueStepSummary Summary;
        Summary.Step     = Steps[Index];
        Summary.NumTakes = StepTallies[Index];
        Report.Steps.Add(Summary);
    }

    if (bCommit)
    {
        if (Report.NumQueued > 0)
        {
            // One dirty + one save for the whole pass. A crash before this point is safe:
            // admission requires a take that has never been queued, so the un-persisted takes
            // are simply re-admitted on the next press.
            CommitDatabase(*DB);
        }

        UE_LOG(LogTemp, Log,
            TEXT("[PCAP] Processing queue: %d take(s) queued, %d already queued, %d skipped. ")
            TEXT("Nothing was processed — a queued take is a worklist entry for the operator."),
            Report.NumQueued, Report.NumAlreadyQueued, Report.NumSkipped);
    }

    return Report;
}

FPCAPQueueReport UPCAPTakeProcessingQueue::PreviewQueueAll(UMocapDatabase* DB)
{
    return RunQueuePass(DB, FString(), FString(), /*bCommit*/ false);
}

FPCAPQueueReport UPCAPTakeProcessingQueue::QueueAll(UMocapDatabase* DB)
{
    return RunQueuePass(DB, FString(), FString(), /*bCommit*/ true);
}

FPCAPQueueReport UPCAPTakeProcessingQueue::PreviewQueueDay(UMocapDatabase* DB, const FString& ProjectCode,
                                                           const FString& DayID)
{
    return RunQueuePass(DB, ProjectCode, DayID, /*bCommit*/ false);
}

FPCAPQueueReport UPCAPTakeProcessingQueue::QueueDay(UMocapDatabase* DB, const FString& ProjectCode,
                                                     const FString& DayID)
{
    return RunQueuePass(DB, ProjectCode, DayID, /*bCommit*/ true);
}

bool UPCAPTakeProcessingQueue::CanQueueTake(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason)
{
    const FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }

    FString Reason;
    if (!IsEligibleForProcessing(*Take, Reason))
    {
        OutReason = FText::FromString(FString::Printf(TEXT("Take '%s' %s"), *Take->TakeID, *Reason));
        return false;
    }

    if (Take->ProcessingState.bHasQueued)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' is already in the processing queue."), *Take->TakeID));
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::QueueTake(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason)
{
    if (!CanQueueTake(DB, Key, OutReason))
    {
        return false;
    }

    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }

    AdmitTake(*Take);
    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::RemoveTakeFromQueue(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason)
{
    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }

    FTakeProcessingState& State = Take->ProcessingState;
    if (!State.bHasQueued && State.OverallStatus == EProcessingStatus::Pending)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("Take '%s' is not in the processing queue."), *Take->TakeID));
        return false;
    }

    for (const EPCAPProcessingStep Step : PipelineSteps())
    {
        if (FProcessingStep* Entry = FindStep(State, Step))
        {
            ClearStepToPending(*Entry);
        }
    }

    State.bApplyBodySolve     = false;
    State.bApplyHMCSolve      = false;
    State.bApplyBodyRetarget  = false;
    State.bApplyAudioSyncTrim = false;

    State.QueuedAt      = FDateTime(0);
    State.bHasQueued    = false;
    State.CompletedAt   = FDateTime(0);
    State.bHasCompleted = false;
    State.OverallStatus = EProcessingStatus::Pending;

    // OutputSequence is left alone on purpose. It names a real asset; clearing the reference
    // would not delete it, it would only lose track of it.

    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

FString UPCAPTakeProcessingQueue::DescribeQueueReport(const FPCAPQueueReport& Report)
{
    // Deliberately says "queued for processing", never "processed" — a pass admits takes to a
    // worklist and runs nothing.
    FString Summary = FString::Printf(TEXT("%d take(s) queued for processing"), Report.NumQueued);

    if (Report.NumAlreadyQueued > 0)
    {
        Summary += FString::Printf(TEXT(" · %d already queued"), Report.NumAlreadyQueued);
    }

    const int32 OtherSkips = Report.NumSkipped - Report.NumAlreadyQueued;
    if (OtherSkips > 0)
    {
        Summary += FString::Printf(TEXT(" · %d skipped"), OtherSkips);
    }

    Summary += TEXT(". Nothing has been processed — each step is now outstanding for the operator.");
    return Summary;
}

// ── Operator attestation ────────────────────────────────────────────────────
//
// None of the transitions below performs work. They record what a person did in Shogun Post,
// Motive, MetaHuman Animator or the editor, so the day's state is a truthful account of where
// each take stands.

bool UPCAPTakeProcessingQueue::CanMarkStepStarted(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                                  EPCAPProcessingStep Step, FText& OutReason)
{
    const FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }
    if (!ValidateStepContext(*Take, Step, OutReason))
    {
        return false;
    }

    const FProcessingStep* Entry = FindStep(Take->ProcessingState, Step);
    if (!Entry)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s has no state on this take."), *StepDisplayName(Step)));
        return false;
    }

    if (Entry->Status == EProcessingStatus::InProgress)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is already marked as started."), *StepDisplayName(Step)));
        return false;
    }
    if (Entry->Status == EProcessingStatus::Complete)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is already complete — reset it first to work on it again."), *StepDisplayName(Step)));
        return false;
    }

    return ArePrerequisitesMet(Take->ProcessingState, Step, OutReason);
}

bool UPCAPTakeProcessingQueue::MarkStepStarted(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                               EPCAPProcessingStep Step, FText& OutReason)
{
    if (!CanMarkStepStarted(DB, Key, Step, OutReason))
    {
        return false;
    }

    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    FProcessingStep* Entry = Take ? FindStep(Take->ProcessingState, Step) : nullptr;
    if (!Take || !Entry)
    {
        return false;   // ResolveForWrite already explained a missing take
    }

    Entry->Status        = EProcessingStatus::InProgress;
    Entry->bHasStarted   = true;
    Entry->StartedAt     = FDateTime::UtcNow();
    Entry->bHasCompleted = false;
    Entry->CompletedAt   = FDateTime(0);
    Entry->ErrorMessage.Empty();   // a retry starts clean

    RefreshOverallStatus(Take->ProcessingState);
    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::CanMarkStepComplete(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                                   EPCAPProcessingStep Step, FText& OutReason)
{
    const FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }
    if (!ValidateStepContext(*Take, Step, OutReason))
    {
        return false;
    }

    const FProcessingStep* Entry = FindStep(Take->ProcessingState, Step);
    if (!Entry)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s has no state on this take."), *StepDisplayName(Step)));
        return false;
    }

    if (Entry->Status == EProcessingStatus::Complete)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is already complete."), *StepDisplayName(Step)));
        return false;
    }
    if (Entry->Status == EProcessingStatus::Failed)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is marked failed — reset it before marking it complete."), *StepDisplayName(Step)));
        return false;
    }

    return ArePrerequisitesMet(Take->ProcessingState, Step, OutReason);
}

bool UPCAPTakeProcessingQueue::MarkStepComplete(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                                EPCAPProcessingStep Step, FText& OutReason)
{
    if (!CanMarkStepComplete(DB, Key, Step, OutReason))
    {
        return false;
    }

    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    FProcessingStep* Entry = Take ? FindStep(Take->ProcessingState, Step) : nullptr;
    if (!Take || !Entry)
    {
        return false;
    }

    const FDateTime Now = FDateTime::UtcNow();

    // One-click "mark complete" from Queued: the step never passed through a separate started
    // state, so both stamps land on the same instant. StartedAt == CompletedAt is the signature
    // of an attestation made in a single action, not a claim that the work took no time.
    if (!Entry->bHasStarted)
    {
        Entry->bHasStarted = true;
        Entry->StartedAt   = Now;
    }

    Entry->Status        = EProcessingStatus::Complete;
    Entry->bHasCompleted = true;
    Entry->CompletedAt   = Now;
    Entry->ErrorMessage.Empty();

    RefreshOverallStatus(Take->ProcessingState);
    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::CanMarkStepFailed(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                                 EPCAPProcessingStep Step, FText& OutReason)
{
    const FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }
    if (!ValidateStepContext(*Take, Step, OutReason))
    {
        return false;
    }

    const FProcessingStep* Entry = FindStep(Take->ProcessingState, Step);
    if (!Entry)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s has no state on this take."), *StepDisplayName(Step)));
        return false;
    }

    if (Entry->Status == EProcessingStatus::Complete)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is already complete — reset it before recording a problem."), *StepDisplayName(Step)));
        return false;
    }
    if (Entry->Status == EProcessingStatus::Failed)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s is already marked failed."), *StepDisplayName(Step)));
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::MarkStepFailed(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                              EPCAPProcessingStep Step, const FString& ErrorMessage,
                                              FText& OutReason)
{
    // ErrorMessage is the only free text a step carries, and the steps are per-take while the HMC
    // solve and the retarget are really per-subject. A failure with no message loses the one
    // place "actor B's face solve failed, actor A is fine" can be recorded.
    if (ErrorMessage.TrimStartAndEnd().IsEmpty())
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s needs a description of the problem before it can be marked failed."),
            *StepDisplayName(Step)));
        return false;
    }

    if (!CanMarkStepFailed(DB, Key, Step, OutReason))
    {
        return false;
    }

    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    FProcessingStep* Entry = Take ? FindStep(Take->ProcessingState, Step) : nullptr;
    if (!Take || !Entry)
    {
        return false;
    }

    Entry->Status       = EProcessingStatus::Failed;
    Entry->ErrorMessage = ErrorMessage;

    // CompletedAt means "the step ended", and it is stamped on failure too — FProcessingStep has
    // no FailedAt, and losing the time an overnight batch went wrong is worse than a slightly
    // loose field name. bHasCompleted stays false: it is the SUCCESS bit, not an "ended" bit.
    // bHasStarted is left as it was — a step abandoned before any work began keeps it false, and
    // that distinction is worth preserving.
    Entry->bHasCompleted = false;
    Entry->CompletedAt   = FDateTime::UtcNow();

    RefreshOverallStatus(Take->ProcessingState);
    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::CanResetStep(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                            EPCAPProcessingStep Step, FText& OutReason)
{
    const FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }

    // A completed take is still resettable — reopening a step is how a mistake gets corrected —
    // so this checks the label/admission/applicability prelude, not eligibility.
    if (!ValidateStepContext(*Take, Step, OutReason))
    {
        return false;
    }

    const FProcessingStep* Entry = FindStep(Take->ProcessingState, Step);
    if (!Entry)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s has no state on this take."), *StepDisplayName(Step)));
        return false;
    }

    if (Entry->Status == EProcessingStatus::Queued || Entry->Status == EProcessingStatus::Pending)
    {
        OutReason = FText::FromString(FString::Printf(
            TEXT("%s has not been started, so there is nothing to reset."), *StepDisplayName(Step)));
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

bool UPCAPTakeProcessingQueue::ResetStep(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                         EPCAPProcessingStep Step, FText& OutReason)
{
    if (!CanResetStep(DB, Key, Step, OutReason))
    {
        return false;
    }

    FTake* Take = ResolveForWrite(DB, Key, OutReason);
    if (!Take)
    {
        return false;
    }

    // The step and everything built on it. Reopening the body solve invalidates the retarget and
    // the merge that were made from it; leaving those reading Complete would be a false record.
    ResetStepAndDependents(Take->ProcessingState, Step);

    RefreshOverallStatus(Take->ProcessingState);
    CommitDatabase(*DB);

    OutReason = FText::GetEmpty();
    return true;
}

// ── Persistence ─────────────────────────────────────────────────────────────

bool UPCAPTakeProcessingQueue::SaveDatabase(UMocapDatabase* DB)
{
    if (!DB)
    {
        return false;
    }

    UPackage* Package = DB->GetOutermost();
    if (!Package)
    {
        return false;
    }

    // Database edits only MarkPackageDirty by default, so without this a whole day of queue and
    // attestation state lives in memory until someone happens to save — and is gone on a crash
    // or a single "don't save".
    if (Package->IsDirty())
    {
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
    return true;
}

void UPCAPTakeProcessingQueue::CommitDatabase(UMocapDatabase& DB)
{
    DB.MarkPackageDirty();
    ++GPCAPQueueChangeSerial;

    if (GPCAPQueueAutoSave)
    {
        SaveDatabase(&DB);
    }
}

void UPCAPTakeProcessingQueue::SetAutoSave(bool bEnabled)
{
    GPCAPQueueAutoSave = bEnabled;
}

bool UPCAPTakeProcessingQueue::IsAutoSaveEnabled() const
{
    return GPCAPQueueAutoSave;
}

int32 UPCAPTakeProcessingQueue::GetChangeSerial() const
{
    return GPCAPQueueChangeSerial;
}
