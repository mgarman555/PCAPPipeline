#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "PCAPToolTypes.h"   // FTake / FTakeProcessingState / FProcessingStep / ETakeLabel / EProcessingStatus
#include "PCAPTakeProcessingQueue.generated.h"

class UMocapDatabase;

// The five post-take steps, in PIPELINE order.
//
// This is deliberately NOT the member order of FTakeProcessingState, which declares
// MergeToSequencer *before* AudioSyncTrim. Merge is the terminal step — it consumes what the
// others produce — so anything that walks the steps must walk this enum and never the struct's
// declaration order. FTakeProcessingState carries five named members and no step enum, which is
// exactly why one lives here.
UENUM(BlueprintType)
enum class EPCAPProcessingStep : uint8
{
    BodySolveCleanup UMETA(DisplayName = "Body Solve Cleanup"),
    HMCSolve         UMETA(DisplayName = "HMC Solve"),
    BodyRetarget     UMETA(DisplayName = "Body Retarget"),
    AudioSyncTrim    UMETA(DisplayName = "Audio Sync / Trim"),
    MergeToSequencer UMETA(DisplayName = "Merge to Sequencer")
};

// Where a take lives in the database, as the full five-part coordinate.
//
// Every entry point here is keyed on this rather than on FTake*/FShot*: the database hands out
// raw pointers into TArray storage nested four deep, and any Add anywhere in the hierarchy — the
// record path appends to FShot::Takes from a Take Recorder callback, which can land mid-batch —
// reallocates and dangles them. Keys are re-resolved immediately before each read or write and
// are never cached across a frame.
//
// SessionID is load-bearing. ShotIDs are slot-only 3-digit values reused across the sessions of
// a day, so a take is only uniquely addressable with the session in hand.
// UMocapDatabase::GetTake omits SessionID and returns the first match across the whole day, so
// nothing here uses it — resolution goes through GetShot(Project, Day, Session, Shot) plus a
// TakeID search over that shot's takes.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPTakeKey
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    FString ProjectCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    FString DayID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    FString SessionID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    FString ShotID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    FString TakeID;

    bool IsValid() const
    {
        return !ProjectCode.IsEmpty() && !DayID.IsEmpty() && !SessionID.IsEmpty()
            && !ShotID.IsEmpty() && !TakeID.IsEmpty();
    }

    // "DA/Day 001/S01/Shot 003/001003_004" — for logs and skip lines.
    FString ToDisplayString() const
    {
        return FString::Printf(TEXT("%s/Day %s/%s/Shot %s/%s"),
            *ProjectCode, *DayID, *SessionID, *ShotID, *TakeID);
    }
};

// Which steps apply to one take, computed from what that take's own manifest records.
//
// There is no flag for MergeToSequencer because the locked pipeline always merges — which is why
// FTakeProcessingState carries four bApply* flags for five steps. Merge is applicable for any
// admitted take and inapplicable for one that was never queued.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPProcessingPlan
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bApplyBodySolve = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bApplyHMCSolve = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bApplyBodyRetarget = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bApplyAudioSyncTrim = false;

    // MergeToSequencer included, so this is never zero for a real take.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    int32 ApplicableStepCount = 0;
};

// How many of a pass's takes carry one step. Lets a caller spell out the payload of a batch
// before the click — "Body Solve Cleanup x4 · HMC Solve x2" — instead of after it.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPQueueStepSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    EPCAPProcessingStep Step = EPCAPProcessingStep::BodySolveCleanup;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    int32 NumTakes = 0;
};

// Outcome of one queue pass — previewed or committed, the shape is identical, which is what
// makes a preview trustworthy. Nothing in here reports work performed: see the class comment.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPQueueReport
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    int32 NumQueued = 0;            // admitted by this pass (or that would be, in a preview)

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    int32 NumAlreadyQueued = 0;     // admitted by an earlier pass, left untouched

    // Considered but not newly queued: already in the queue, or labelled and ineligible. Takes
    // still on the default Captured label are not counted — they are the bulk of a shoot day and
    // would bury the skips that matter.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    int32 NumSkipped = 0;

    // Steps carried by the takes this pass queues, in pipeline order. Steps that apply to no
    // take are omitted.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TArray<FPCAPQueueStepSummary> Steps;

    // One line per skipped take — Burn takes included, so the archived-raw rule is seen being
    // applied rather than takes quietly vanishing. Carries a single un-counted line instead when
    // the pass could not run at all (no database).
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TArray<FString> SkipReasons;
};

// ---------------------------------------------------------------------------
// The state layer behind Layer 5 (Take Browser / post-take management).
//
// FTakeProcessingState has been a field on FTake since the Phase 1 data model and has never been
// written by anything — the single read in the plugin is the Pending test inside
// UMocapDatabase::GetUnprocessedQueuedTakes. This subsystem is its first and only writer. It
// owns four jobs:
//   1. enumerate the takes eligible for processing — label Best or Alt, never Burn, never
//      already Complete;
//   2. compute which steps APPLY to a take from the streams that take's own manifest records;
//   3. drive the per-step EProcessingStatus state machine; and
//   4. persist the result back onto the take in the UMocapDatabase, dirtying (and by default
//      saving) the asset so an end-of-day pass survives an editor restart.
//
// NOTHING HERE PERFORMS A SOLVE, AND NOTHING HERE PRETENDS TO.
//
// Two of the five steps are not Unreal operations at all. Body solve cleanup happens in Shogun
// Post / Motive. The HMC solve happens in MetaHuman Animator or the head-cam vendor's own tool.
// Unreal cannot perform either, now or later. The remaining three — body retarget, merge to
// sequencer, audio sync/trim — are plausibly in-engine, but nothing can run them today: the
// recorder resolves only FTake::MasterSequence and leaves BodyAnimAssets / FaceAnimAssets /
// AudioAssets empty, so there is no per-stream input for a retarget, a merge or a trim to act on.
//
// So all five steps are modelled as OPERATOR ATTESTATION rather than as work. MarkStepStarted
// records that a human began a step, MarkStepComplete that a human finished it, MarkStepFailed
// that a human hit a problem and what it was. The timestamps are testimony about work done
// elsewhere, and QueueAll admits takes to a worklist — it runs nothing. Callers must present it
// that way; StepExecutionNote() exists so that wording comes from one place.
//
// If in-engine automation is ever wired for retarget/merge/audio, it belongs behind a new entry
// point that calls MarkStepComplete on success. The attestation transitions stay, because they
// are the only honest model for the two steps that can only ever happen outside the engine.
//
// APPLICABILITY (locked pipeline): body solve cleanup and body retarget apply when any subject in
// the manifest carried a body stream; the HMC solve applies when any subject carried a face
// stream; audio sync/trim applies when any subject carries audio channels; merge to sequencer is
// unconditional. One caveat about the input, which callers must not overstate:
// FTakeSubjectSnapshot::bHadBodyStream / bHadFaceStream are copied at record time from the shot
// subject's *configured* stream flags, so they record what was armed for the take, not what was
// measured live. That is the right question for applicability ("was this stream part of the
// take?") and it is the only record-time evidence the data model keeps — but it is not a claim
// that the stream was healthy, and no UI should label it one.
//
// SHAPE: the working surface is static and takes the UMocapDatabase explicitly, because its
// callers (Slate panels, tests) already hold one and a queue pass must be unambiguous about
// which asset it writes. The instance surface is the Blueprint face of the same calls, resolving
// the project's assigned database for you. Access it via
// GEngine->GetEngineSubsystem<UPCAPTakeProcessingQueue>(), or UPCAPTakeProcessingQueue::Get().
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPTakeProcessingQueue : public UEngineSubsystem
{
    GENERATED_BODY()

public:

    // ── Access ──────────────────────────────────────────────────────────────

    // The engine subsystem instance, or null before the engine exists. The static API below
    // needs no instance — this is for reaching the auto-save flag and the change serial.
    static UPCAPTakeProcessingQueue* Get();

    // The project's assigned database (Project Settings ▸ PCAP ▸ PCAP Tool). Null when unset.
    static UMocapDatabase* GetProjectDatabase();

    // ── Step metadata (static; pure) ────────────────────────────────────────

    // The five steps in pipeline order. Iterate this, never FTakeProcessingState's members.
    static TArray<EPCAPProcessingStep> PipelineSteps();

    static FString StepDisplayName(EPCAPProcessingStep Step);

    // True for the steps Unreal cannot perform at all (body solve cleanup, HMC solve). False
    // does NOT mean "the engine runs it" — no step has an executor. See StepExecutionNote.
    static bool IsStepExternal(EPCAPProcessingStep Step);

    // Operator-facing note naming where the step actually happens and who marks it. Meant to be
    // shown verbatim beside a step's controls so a UI cannot imply the tool did the work.
    static FString StepExecutionNote(EPCAPProcessingStep Step);

    // ── Applicability (static; pure) ────────────────────────────────────────

    // Which steps this take's manifest calls for. Computed once, at admission, and stored in the
    // bApply* flags; never recomputed for an already-queued take, because those flags are
    // EditAnywhere and the operator may have adjusted the plan by hand.
    static FPCAPProcessingPlan MakeProcessingPlan(const FTake& Take);

    // Reads the plan already stored on a take's state. Post-admission, a step whose Status is
    // Pending is a step that does not apply — there is no Skipped enumerator, and the bApply*
    // flags carry that distinction instead.
    static bool StepApplies(const FTakeProcessingState& State, EPCAPProcessingStep Step);

    // Rollup over the applicable steps. Precedence: Failed > InProgress > Queued > Complete >
    // Pending — a queue holding one failure reads Failed, never "mostly complete". OverallStatus
    // is always derived by this; nothing assigns it directly.
    static EProcessingStatus RollUpStatus(const FTakeProcessingState& State);

    // ── Enumeration (static) ────────────────────────────────────────────────

    // Every take eligible for processing: label Best or Alt (never Burn, never Captured),
    // actually recorded, and not already Complete. Includes takes already in the queue —
    // eligibility is a property of the take, admission is a property of the queue. Empty
    // ProjectCode or DayID widens that axis to all.
    static TArray<FPCAPTakeKey> CollectEligibleTakes(UMocapDatabase* DB,
                                                     const FString& ProjectCode = FString(),
                                                     const FString& DayID = FString());

    // Takes admitted and not finished: OverallStatus Queued, InProgress or Failed. This is the
    // real queue — unlike UMocapDatabase::GetUnprocessedQueuedTakes, whose name says Queued but
    // whose body tests Pending (the never-written default), so it answers "never admitted".
    static TArray<FPCAPTakeKey> CollectQueuedTakes(UMocapDatabase* DB);

    // Queued takes that can no longer be worked because they were relabelled after admission
    // (Burn, or back to Captured). Surfaced deliberately rather than dropped: the operator has
    // to see that a take they queued is now archived-raw, not just lose it.
    static TArray<FPCAPTakeKey> CollectStrandedQueuedTakes(UMocapDatabase* DB);

    // Copy of a take's processing state. False when the key does not resolve.
    static bool ReadProcessingState(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                    FTakeProcessingState& OutState);

    // ── Queueing / admission (static) ───────────────────────────────────────

    // Exactly what QueueAll would do, writing nothing. Same report shape as the committing pass,
    // which is what makes it safe to show as a promise before the click.
    static FPCAPQueueReport PreviewQueueAll(UMocapDatabase* DB);

    // End-of-day batch: admit every eligible take. Queues work; runs none.
    static FPCAPQueueReport QueueAll(UMocapDatabase* DB);

    // Day-scoped forms — the usual end-of-day scope. Empty ProjectCode or DayID widens that axis.
    static FPCAPQueueReport PreviewQueueDay(UMocapDatabase* DB, const FString& ProjectCode, const FString& DayID);
    static FPCAPQueueReport QueueDay(UMocapDatabase* DB, const FString& ProjectCode, const FString& DayID);

    // False + OutReason when this take cannot be admitted right now. OutReason is operator-facing
    // and is meant to be used verbatim as a disabled control's tooltip, so a blocked queue
    // control always explains itself instead of no-opping.
    static bool CanQueueTake(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason);

    // Admit one take: compute and store its step plan, stamp QueuedAt/bHasQueued, and move the
    // take and its applicable steps to Queued. Idempotent — a take already in the queue is
    // refused with a reason rather than re-planned, so hand edits to the bApply* flags survive.
    static bool QueueTake(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason);

    // The only legal route back to Pending: clears the queue stamps, the step plan and all five
    // steps. Without it an accidentally-queued take is stuck in the queue forever, because every
    // other transition moves away from Pending. FTakeProcessingState::OutputSequence is
    // deliberately left alone — it names a real asset, and this class never invents or discards
    // assets.
    static bool RemoveTakeFromQueue(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason);

    // Operator-facing one-liner for a report. Single source of the wording, so a toast says what
    // actually happened ("queued for processing") and never implies takes were solved.
    static FString DescribeQueueReport(const FPCAPQueueReport& Report);

    // ── Operator attestation (static) ───────────────────────────────────────
    //
    // These record what a person did in Shogun Post, Motive, MetaHuman Animator or the editor.
    // None of them performs work. Each Can* fills OutReason with the operator-facing explanation
    // when it refuses, and the matching mutator re-checks it — so a control rebuilt from stale
    // state can never commit a transition that has since become illegal.

    static bool CanMarkStepStarted(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                   EPCAPProcessingStep Step, FText& OutReason);

    // Attest that the operator has started this step elsewhere: Queued/Failed -> InProgress,
    // stamping bHasStarted + StartedAt and clearing any previous error.
    static bool MarkStepStarted(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                EPCAPProcessingStep Step, FText& OutReason);

    static bool CanMarkStepComplete(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                    EPCAPProcessingStep Step, FText& OutReason);

    // Attest that the step is done: InProgress -> Complete, stamping bHasCompleted + CompletedAt.
    // Also legal straight from Queued, for the one-click "mark complete" an end-of-day pass
    // wants; that form stamps StartedAt and CompletedAt to the same instant, which is what an
    // operator marking a finished step in a single action actually means.
    static bool MarkStepComplete(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                 EPCAPProcessingStep Step, FText& OutReason);

    static bool CanMarkStepFailed(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                                  EPCAPProcessingStep Step, FText& OutReason);

    // Record a problem: Queued/InProgress -> Failed, with a mandatory message. ErrorMessage is
    // the only free text a step carries, and the steps are per-take while the HMC solve and the
    // retarget are really per-subject — so this is where "failed for actor B, actor A is fine"
    // has to live. An empty message is refused.
    static bool MarkStepFailed(UMocapDatabase* DB, const FPCAPTakeKey& Key, EPCAPProcessingStep Step,
                               const FString& ErrorMessage, FText& OutReason);

    static bool CanResetStep(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                             EPCAPProcessingStep Step, FText& OutReason);

    // Retry a failed step, or reopen a completed one: back to Queued, fully cleared. Every
    // applicable step that depends on this one is reset with it — reopening the body solve
    // invalidates the retarget and the merge built on it, and leaving those reading Complete
    // would be a false record.
    static bool ResetStep(UMocapDatabase* DB, const FPCAPTakeKey& Key,
                          EPCAPProcessingStep Step, FText& OutReason);

    // ── Persistence (static) ────────────────────────────────────────────────

    // Flush the database asset to disk. MarkPackageDirty alone does not persist — an editor
    // crash, or one "don't save", loses a whole day of queue and attestation state. Every
    // mutator above calls this when auto-save is on (the default).
    static bool SaveDatabase(UMocapDatabase* DB);

    // ── Blueprint surface ───────────────────────────────────────────────────
    //
    // The same calls against the project's assigned database, for Blueprint and UMG. C++ callers
    // that already hold a UMocapDatabase* should use the static forms above.

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    TArray<EPCAPProcessingStep> GetPipelineSteps() const { return PipelineSteps(); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    FString GetStepDisplayName(EPCAPProcessingStep Step) const { return StepDisplayName(Step); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    bool IsStepExternalToUnreal(EPCAPProcessingStep Step) const { return IsStepExternal(Step); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    FString GetStepExecutionNote(EPCAPProcessingStep Step) const { return StepExecutionNote(Step); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    FPCAPProcessingPlan BuildProcessingPlan(const FTake& Take) const { return MakeProcessingPlan(Take); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    bool IsStepApplicable(const FTakeProcessingState& State, EPCAPProcessingStep Step) const
    { return StepApplies(State, Step); }

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    EProcessingStatus RecomputeOverallStatus(const FTakeProcessingState& State) const
    { return RollUpStatus(State); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    TArray<FPCAPTakeKey> GetEligibleTakes() const
    { return CollectEligibleTakes(GetProjectDatabase()); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    TArray<FPCAPTakeKey> GetEligibleTakesForDay(const FString& ProjectCode, const FString& DayID) const
    { return CollectEligibleTakes(GetProjectDatabase(), ProjectCode, DayID); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    TArray<FPCAPTakeKey> GetQueuedTakes() const { return CollectQueuedTakes(GetProjectDatabase()); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    TArray<FPCAPTakeKey> GetStrandedQueuedTakes() const
    { return CollectStrandedQueuedTakes(GetProjectDatabase()); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool GetProcessingState(const FPCAPTakeKey& Key, FTakeProcessingState& OutState) const
    { return ReadProcessingState(GetProjectDatabase(), Key, OutState); }

    // Named for what they do, not for the button that calls them: the locked UI label is
    // "Process All Queued", but the operation is admission to a worklist. A Blueprint author
    // reading this API must not be able to mistake it for execution.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    FPCAPQueueReport PreviewQueueAllTakes() const { return PreviewQueueAll(GetProjectDatabase()); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    FPCAPQueueReport QueueAllTakes() { return QueueAll(GetProjectDatabase()); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    FPCAPQueueReport QueueAllTakesForDay(const FString& ProjectCode, const FString& DayID)
    { return QueueDay(GetProjectDatabase(), ProjectCode, DayID); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool CanAddTakeToQueue(const FPCAPTakeKey& Key, FText& OutReason) const
    { return CanQueueTake(GetProjectDatabase(), Key, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool AddTakeToQueue(const FPCAPTakeKey& Key, FText& OutReason)
    { return QueueTake(GetProjectDatabase(), Key, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool RemoveFromQueue(const FPCAPTakeKey& Key, FText& OutReason)
    { return RemoveTakeFromQueue(GetProjectDatabase(), Key, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool BeginStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason)
    { return MarkStepStarted(GetProjectDatabase(), Key, Step, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool CompleteStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason)
    { return MarkStepComplete(GetProjectDatabase(), Key, Step, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool FailStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step,
                  const FString& ErrorMessage, FText& OutReason)
    { return MarkStepFailed(GetProjectDatabase(), Key, Step, ErrorMessage, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool RetryStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason)
    { return ResetStep(GetProjectDatabase(), Key, Step, OutReason); }

    // ── "Can I?" twins of the transitions above ─────────────────────────────
    //
    // Every one of these returns the operator-facing refusal in OutReason, which callers
    // use verbatim as a disabled-control tooltip — that is how a greyed-out button in the
    // Take Browser always says WHY rather than just sitting dead.
    //
    // Deliberately NOT UFUNCTIONs: CanQueueTake and CanResetStep would then each collide
    // with the same-named static under UHT's reflection registration. They exist for the
    // C++ panel, which needs no Blueprint exposure to call them.
    bool CanBeginStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason) const
    { return CanMarkStepStarted(GetProjectDatabase(), Key, Step, OutReason); }

    bool CanCompleteStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason) const
    { return CanMarkStepComplete(GetProjectDatabase(), Key, Step, OutReason); }

    bool CanFailStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason) const
    { return CanMarkStepFailed(GetProjectDatabase(), Key, Step, OutReason); }

    // Explicitly qualified: these two share a name with the static they forward to, and the
    // arity alone resolving it correctly is not something to leave implicit — an accidental
    // self-call here would be unbounded recursion.
    bool CanResetStep(const FPCAPTakeKey& Key, EPCAPProcessingStep Step, FText& OutReason) const
    { return UPCAPTakeProcessingQueue::CanResetStep(GetProjectDatabase(), Key, Step, OutReason); }

    bool CanQueueTake(const FPCAPTakeKey& Key, FText& OutReason) const
    { return UPCAPTakeProcessingQueue::CanQueueTake(GetProjectDatabase(), Key, OutReason); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    FString DescribeReport(const FPCAPQueueReport& Report) const { return DescribeQueueReport(Report); }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    bool SaveDatabaseNow() { return SaveDatabase(GetProjectDatabase()); }

    // Whether every mutating call flushes to disk immediately (default true). Turn it off only
    // around a caller-driven bulk edit that saves once at the end; the batch entry points here
    // already do exactly that internally. Editor-session scoped, not stored in the database.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Processing")
    void SetAutoSave(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    bool IsAutoSaveEnabled() const;

    // Increments on every successful write. UMocapDatabase has no change delegate, so a panel
    // polls this from its active timer and rebuilds only when it moves — the same change-guard
    // shape SPCAPOperatorConsole uses for the record state.
    UFUNCTION(BlueprintPure, Category = "PCAP|Processing")
    int32 GetChangeSerial() const;

private:

    // Re-resolve a key to live storage. Deliberately not UMocapDatabase::GetTake — that overload
    // drops SessionID and returns the first shot-slot match in the day.
    static FTake* FindTake(UMocapDatabase& DB, const FPCAPTakeKey& Key);

    // One walk of the whole hierarchy, appending a key per take. FTake carries no production
    // code, so the five-part coordinate is only knowable here. Empty filters match everything.
    static void GatherTakeKeys(UMocapDatabase& DB, const FString& ProjectCodeFilter,
                               const FString& DayIDFilter, TArray<FPCAPTakeKey>& OutKeys);

    static FProcessingStep*       FindStep(FTakeProcessingState& State, EPCAPProcessingStep Step);
    static const FProcessingStep* FindStep(const FTakeProcessingState& State, EPCAPProcessingStep Step);

    // Direct prerequisite table. Body retarget needs the body solve; the merge needs everything
    // else. Body solve, HMC solve and audio sync/trim are independent of one another — they
    // happen at different stations, often in parallel, and ordering them would be a fiction. The
    // table is transitively complete: the merge lists all four, so it is reached by any upstream
    // reset without walking the chain.
    static bool StepDependsOn(EPCAPProcessingStep Step, EPCAPProcessingStep Prerequisite);
    static bool ArePrerequisitesMet(const FTakeProcessingState& State, EPCAPProcessingStep Step,
                                    FText& OutReason);

    // Label / recorded / not-already-complete. OutReason is a sentence fragment that reads on
    // from the take's name ("… is labelled Burn — …"), shared by enumeration and admission.
    static bool IsEligibleForProcessing(const FTake& Take, FString& OutReason);

    // Shared prelude for the attestation transitions: the take is still Best/Alt, it has been
    // admitted, and this step applies to it.
    static bool ValidateStepContext(const FTake& Take, EPCAPProcessingStep Step, FText& OutReason);

    static void ClearStepToPending(FProcessingStep& OutStep);
    static void ResetStepToQueued(FProcessingStep& OutStep);
    static void ResetStepAndDependents(FTakeProcessingState& State, EPCAPProcessingStep Step);

    // Recompute OverallStatus and keep the take-level completion stamps in step with it.
    static void RefreshOverallStatus(FTakeProcessingState& State);

    // Write the plan + queue stamps onto an already-validated take.
    static void AdmitTake(FTake& Take);

    // Resolve the take a mutator is about to touch, reporting the same way for every caller.
    static FTake* ResolveForWrite(UMocapDatabase* DB, const FPCAPTakeKey& Key, FText& OutReason);

    // MarkPackageDirty, bump the change serial, and save when auto-save is on.
    static void CommitDatabase(UMocapDatabase& DB);

    // The one queue pass. bCommit false previews it and writes nothing.
    static FPCAPQueueReport RunQueuePass(UMocapDatabase* DB, const FString& ProjectCode,
                                         const FString& DayID, bool bCommit);
};
