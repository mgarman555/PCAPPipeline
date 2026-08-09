#include "Misc/AutomationTest.h"
#include "PCAPTakeProcessingQueue.h"
#include "MocapDatabase.h"
#include "PCAPToolTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Post-take processing state (Layer 5). These pin the rules that would silently corrupt a shoot
// day if they drifted: which takes may enter the queue, which steps a take's manifest actually
// calls for, and what each attestation transition is allowed to write.
//
// Nothing here asserts that a solve ran, because nothing runs one. Body solve cleanup happens in
// Shogun Post / Motive and the HMC solve in MetaHuman Animator or the head-cam vendor's tool;
// retarget, merge and audio sync/trim are in-engine in principle but have no executor and no input
// assets. Every step is an operator attestation, so what is tested is the record — which step is
// outstanding, who said it started, what went wrong — never work performed.
//
// Fixtures are transient-package UMocapDatabase objects built in memory, exactly as
// PCAPDataModelTests does; no asset is loaded and nothing reaches disk.

namespace
{
    // One production / day / session / shot for every fixture, so a take is addressable by TakeID
    // alone. Prefixed because the module compiles as a single unity translation unit and a
    // file-scope name here is visible to all ~47 of its .cpp files (see CurrentErrorCodes: a bare
    // `Dt` in the VCam tests broke the 5.8 build with C4459, warnings being errors).
    const TCHAR* const kPCAPTakeProcProject = TEXT("DA");
    const TCHAR* const kPCAPTakeProcDay     = TEXT("001");
    const TCHAR* const kPCAPTakeProcSession = TEXT("S01");
    const TCHAR* const kPCAPTakeProcShot    = TEXT("003");

    // Every mutating entry point flushes the database to disk when auto-save is on, which is the
    // default. These fixtures live in the transient package and must never reach the filesystem,
    // so the mutating tests switch it off for their duration and restore whatever they found.
    // (The transient package also refuses to go dirty, so the save is a no-op even if the engine
    // subsystem is unavailable — this is the belt to that pair of braces.)
    struct FPCAPTakeProcNoAutoSave
    {
        UPCAPTakeProcessingQueue* Queue;
        bool bRestore;

        FPCAPTakeProcNoAutoSave()
            : Queue(UPCAPTakeProcessingQueue::Get())
            , bRestore(true)
        {
            if (Queue)
            {
                bRestore = Queue->IsAutoSaveEnabled();
                Queue->SetAutoSave(false);
            }
        }

        ~FPCAPTakeProcNoAutoSave()
        {
            if (Queue)
            {
                Queue->SetAutoSave(bRestore);
            }
        }
    };

    // A recorded take carrying exactly the streams asked for. RecordedAt is deliberately non-zero:
    // a zero timestamp marks one of the seeded shot slots, which is not an eligible take.
    FTake PCAPTakeProcMakeTake(const FString& TakeID, ETakeLabel Label,
                               bool bBody, bool bFace, bool bAudio)
    {
        FTake Take;
        Take.TakeID     = TakeID;
        Take.DayID      = kPCAPTakeProcDay;
        Take.SessionID  = kPCAPTakeProcSession;
        Take.ShotID     = kPCAPTakeProcShot;
        Take.TakeNumber = TakeID.Right(3);
        Take.Label      = Label;
        Take.RecordedAt = FDateTime(2026, 8, 9);

        FTakeSubjectSnapshot Subject;
        Subject.ActorID        = TEXT("kevinDorman");
        Subject.CharacterName  = TEXT("Val");
        Subject.bHadBodyStream = bBody;
        Subject.bHadFaceStream = bFace;
        if (bAudio)
        {
            Subject.AudioChannels.Add(TEXT("ch01"));
        }
        Take.SubjectManifest.Add(Subject);

        return Take;
    }

    UMocapDatabase* PCAPTakeProcMakeDatabase(const TArray<FTake>& Takes)
    {
        UMocapDatabase* DB = NewObject<UMocapDatabase>();

        FProduction P; P.ProjectCode = kPCAPTakeProcProject;
        FShootDay   D; D.DayID       = kPCAPTakeProcDay;
        FSession    S; S.SessionID   = kPCAPTakeProcSession;
        FShot    Shot; Shot.ShotID   = kPCAPTakeProcShot;

        Shot.Takes = Takes;
        S.Shots.Add(Shot);
        D.Sessions.Add(S);
        P.Days.Add(D);
        DB->Productions.Add(P);

        return DB;
    }

    FPCAPTakeKey PCAPTakeProcMakeKey(const FString& TakeID)
    {
        FPCAPTakeKey Key;
        Key.ProjectCode = kPCAPTakeProcProject;
        Key.DayID       = kPCAPTakeProcDay;
        Key.SessionID   = kPCAPTakeProcSession;
        Key.ShotID      = kPCAPTakeProcShot;
        Key.TakeID      = TakeID;
        return Key;
    }

    // Relabel a take where it lives, the way UPCAPToolEditorWidget::SetTakeLabel does: resolve the
    // session-qualified shot, then assign through the reference. Nothing in the label path is aware
    // of the queue, which is precisely the case the stranded-take tests exist for.
    bool PCAPTakeProcRelabel(UMocapDatabase& DB, const FString& TakeID, ETakeLabel NewLabel)
    {
        FShot* Shot = DB.GetShot(kPCAPTakeProcProject, kPCAPTakeProcDay,
                                 kPCAPTakeProcSession, kPCAPTakeProcShot);
        if (!Shot)
        {
            return false;
        }

        for (FTake& Take : Shot->Takes)
        {
            if (Take.TakeID == TakeID)
            {
                Take.Label = NewLabel;
                return true;
            }
        }
        return false;
    }

    bool PCAPTakeProcContainsTake(const TArray<FPCAPTakeKey>& Keys, const TCHAR* TakeID)
    {
        return Keys.ContainsByPredicate([TakeID](const FPCAPTakeKey& Key){ return Key.TakeID == TakeID; });
    }
}

// The locked label rule, which is the whole gate on the queue: Best and Alt are admitted, Burn is
// archived raw and never processed, Captured is the untouched default and stays out. A Burn take
// must come through a batch with its processing state not merely unqueued but untouched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcQueueLabelsTest,
    "PCAP.TakeProcessing.QueueAdmitsOnlyBestAndAlt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcQueueLabelsTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best,     /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false),
        PCAPTakeProcMakeTake(TEXT("001003_002"), ETakeLabel::Alt,      /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false),
        PCAPTakeProcMakeTake(TEXT("001003_003"), ETakeLabel::Burn,     /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false),
        PCAPTakeProcMakeTake(TEXT("001003_004"), ETakeLabel::Captured, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false)
    });

    const TArray<FPCAPTakeKey> Eligible = UPCAPTakeProcessingQueue::CollectEligibleTakes(DB);
    TestEqual(TEXT("only Best + Alt are eligible"), Eligible.Num(), 2);
    TestTrue (TEXT("Best eligible"),      PCAPTakeProcContainsTake(Eligible, TEXT("001003_001")));
    TestTrue (TEXT("Alt eligible"),       PCAPTakeProcContainsTake(Eligible, TEXT("001003_002")));
    TestFalse(TEXT("Burn never eligible"),     PCAPTakeProcContainsTake(Eligible, TEXT("001003_003")));
    TestFalse(TEXT("Captured never eligible"), PCAPTakeProcContainsTake(Eligible, TEXT("001003_004")));

    const FPCAPQueueReport Report = UPCAPTakeProcessingQueue::QueueAll(DB);
    TestEqual(TEXT("two takes queued"),   Report.NumQueued, 2);
    TestEqual(TEXT("none already queued"), Report.NumAlreadyQueued, 0);

    // Burn is reported as skipped so the archived-raw rule is seen being applied; Captured is the
    // bulk of a shoot day and is passed over silently, so exactly one skip line is expected.
    TestEqual(TEXT("only the Burn take is reported skipped"), Report.NumSkipped, 1);
    TestEqual(TEXT("one skip line"), Report.SkipReasons.Num(), 1);
    if (Report.SkipReasons.Num() == 1)
    {
        TestTrue(TEXT("skip line names the Burn take"), Report.SkipReasons[0].Contains(TEXT("001003_003")));
        TestTrue(TEXT("skip line names the reason"),    Report.SkipReasons[0].Contains(TEXT("Burn")));
    }

    // Body-only manifests: three steps carried, and no HMC solve or audio trim in the payload.
    TestEqual(TEXT("three steps carried by the queued takes"), Report.Steps.Num(), 3);
    if (Report.Steps.Num() == 3)
    {
        TestEqual(TEXT("step 1 is body solve cleanup"),
            (int32)Report.Steps[0].Step, (int32)EPCAPProcessingStep::BodySolveCleanup);
        TestEqual(TEXT("step 2 is body retarget"),
            (int32)Report.Steps[1].Step, (int32)EPCAPProcessingStep::BodyRetarget);
        TestEqual(TEXT("step 3 is merge to sequencer"),
            (int32)Report.Steps[2].Step, (int32)EPCAPProcessingStep::MergeToSequencer);
        TestEqual(TEXT("both takes carry the body solve"), Report.Steps[0].NumTakes, 2);
    }

    FTakeProcessingState State;
    TestTrue (TEXT("Best take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, PCAPTakeProcMakeKey(TEXT("001003_001")), State));
    TestTrue (TEXT("Best take admitted"), State.bHasQueued);
    TestEqual(TEXT("Best take reads Queued"), (int32)State.OverallStatus, (int32)EProcessingStatus::Queued);

    // The Burn take's processing state must be untouched, not just unqueued — nothing may write to
    // a take that is archived raw.
    TestTrue (TEXT("Burn take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, PCAPTakeProcMakeKey(TEXT("001003_003")), State));
    TestFalse(TEXT("Burn take never queued"), State.bHasQueued);
    TestEqual(TEXT("Burn take stays Pending"), (int32)State.OverallStatus, (int32)EProcessingStatus::Pending);
    TestFalse(TEXT("Burn take has no step plan"), State.bApplyBodySolve);
    TestTrue (TEXT("Burn take queue stamp is clear"), State.QueuedAt == FDateTime(0));
    TestTrue (TEXT("Burn take body solve unnamed"), State.BodySolveCleanup.StepName.IsEmpty());
    TestEqual(TEXT("Burn take body solve Pending"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Pending);

    TestTrue (TEXT("Captured take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, PCAPTakeProcMakeKey(TEXT("001003_004")), State));
    TestFalse(TEXT("Captured take never queued"), State.bHasQueued);

    FText Reason;
    TestFalse(TEXT("Burn take cannot be queued by hand"),
        UPCAPTakeProcessingQueue::CanQueueTake(DB, PCAPTakeProcMakeKey(TEXT("001003_003")), Reason));
    TestTrue (TEXT("refusal explains the Burn label"), Reason.ToString().Contains(TEXT("Burn")));

    TestFalse(TEXT("Captured take cannot be queued by hand"),
        UPCAPTakeProcessingQueue::CanQueueTake(DB, PCAPTakeProcMakeKey(TEXT("001003_004")), Reason));
    TestFalse(TEXT("refusal is explained"), Reason.ToString().IsEmpty());

    return true;
}

// Nothing in the label path knows about the queue, so a take can be relabelled Burn after it was
// admitted. From that moment no step may be worked on it, and it must be visible as stranded
// rather than quietly dropped — an operator has to see that a take they queued is now archived raw.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcBurnAfterQueueTest,
    "PCAP.TakeProcessing.BurnIsNeverWorkedAfterRelabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcBurnAfterQueueTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false)
    });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));

    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);
    TestTrue (TEXT("relabelled Burn after admission"),
        PCAPTakeProcRelabel(*DB, TEXT("001003_001"), ETakeLabel::Burn));

    TestEqual(TEXT("surfaced as stranded"),
        UPCAPTakeProcessingQueue::CollectStrandedQueuedTakes(DB).Num(), 1);
    TestEqual(TEXT("still listed in the queue rather than vanishing"),
        UPCAPTakeProcessingQueue::CollectQueuedTakes(DB).Num(), 1);
    TestEqual(TEXT("no longer eligible"),
        UPCAPTakeProcessingQueue::CollectEligibleTakes(DB).Num(), 0);

    FText Reason;
    TestFalse(TEXT("cannot mark a Burn take's step started"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue (TEXT("refusal names the Burn label"), Reason.ToString().Contains(TEXT("Burn")));

    TestFalse(TEXT("cannot mark a Burn take's step complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestFalse(TEXT("cannot record a failure on a Burn take"),
        UPCAPTakeProcessingQueue::MarkStepFailed(DB, Key, EPCAPProcessingStep::BodySolveCleanup,
                                                 TEXT("marker gap"), Reason));

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("body solve untouched by the refusals"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Queued);
    TestFalse(TEXT("nothing recorded as started"), State.BodySolveCleanup.bHasStarted);
    TestTrue (TEXT("no error text written"), State.BodySolveCleanup.ErrorMessage.IsEmpty());

    // Removal is the escape hatch, and the only legal route back to Pending — it does not care
    // about the label, because a stranded take must always be clearable.
    TestTrue(TEXT("stranded take can be removed from the queue"),
        UPCAPTakeProcessingQueue::RemoveTakeFromQueue(DB, Key, Reason));

    TestTrue (TEXT("take resolves after removal"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestFalse(TEXT("queue stamp cleared"), State.bHasQueued);
    TestEqual(TEXT("back to Pending"), (int32)State.OverallStatus, (int32)EProcessingStatus::Pending);
    TestFalse(TEXT("step plan cleared"), State.bApplyBodySolve);
    TestEqual(TEXT("body solve cleared to Pending"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Pending);
    TestTrue (TEXT("body solve name cleared"), State.BodySolveCleanup.StepName.IsEmpty());
    TestEqual(TEXT("stranded list now empty"),
        UPCAPTakeProcessingQueue::CollectStrandedQueuedTakes(DB).Num(), 0);

    return true;
}

// A take whose manifest records no face stream must never carry an HMC solve. Getting this wrong
// puts an impossible step on a day's worklist — an operator sent to solve a face that was never
// captured — and the take can then never finish.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcNoFaceTest,
    "PCAP.TakeProcessing.NoFaceStreamNoHMCSolve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcNoFaceTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    const FTake BodyAndAudio =
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ true);

    const FPCAPProcessingPlan Plan = UPCAPTakeProcessingQueue::MakeProcessingPlan(BodyAndAudio);
    TestFalse(TEXT("no face stream -> no HMC solve"), Plan.bApplyHMCSolve);
    TestTrue (TEXT("body stream -> body solve"),      Plan.bApplyBodySolve);
    TestTrue (TEXT("body stream -> body retarget"),   Plan.bApplyBodyRetarget);
    TestTrue (TEXT("audio -> audio sync/trim"),       Plan.bApplyAudioSyncTrim);
    TestEqual(TEXT("four steps: body solve, retarget, audio, merge"), Plan.ApplicableStepCount, 4);

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({ BodyAndAudio });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));
    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestFalse(TEXT("HMC solve not planned"), State.bApplyHMCSolve);
    TestFalse(TEXT("HMC solve does not apply"),
        UPCAPTakeProcessingQueue::StepApplies(State, EPCAPProcessingStep::HMCSolve));

    // Post-admission a Pending step is an inapplicable step — that is the distinction the bApply*
    // flags carry, since EProcessingStatus has no Skipped enumerator. The applicable steps sit at
    // Queued and carry their name, so the two are never confusable.
    TestEqual(TEXT("HMC solve left Pending"),
        (int32)State.HMCSolve.Status, (int32)EProcessingStatus::Pending);
    TestTrue (TEXT("HMC solve carries no name"), State.HMCSolve.StepName.IsEmpty());
    TestEqual(TEXT("body solve is outstanding work"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Queued);
    TestEqual(TEXT("body solve carries its name"), State.BodySolveCleanup.StepName,
        UPCAPTakeProcessingQueue::StepDisplayName(EPCAPProcessingStep::BodySolveCleanup));

    FText Reason;
    TestFalse(TEXT("cannot start an HMC solve that does not apply"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, Key, EPCAPProcessingStep::HMCSolve, Reason));
    TestTrue (TEXT("refusal names the missing face stream"),
        Reason.ToString().Contains(TEXT("face stream")));

    TestFalse(TEXT("cannot complete an HMC solve that does not apply"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::HMCSolve, Reason));
    TestFalse(TEXT("cannot fail an HMC solve that does not apply"),
        UPCAPTakeProcessingQueue::MarkStepFailed(DB, Key, EPCAPProcessingStep::HMCSolve,
                                                 TEXT("no head-cam on this take"), Reason));

    TestTrue (TEXT("take resolves after the refusals"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("HMC solve still Pending"),
        (int32)State.HMCSolve.Status, (int32)EProcessingStatus::Pending);
    TestFalse(TEXT("HMC solve never marked started"),  State.HMCSolve.bHasStarted);
    TestFalse(TEXT("HMC solve never marked complete"), State.HMCSolve.bHasCompleted);
    TestTrue (TEXT("HMC solve end stamp still clear"), State.HMCSolve.CompletedAt == FDateTime(0));

    return true;
}

// A take with no audio channels must never carry an audio sync/trim — and, the other half of the
// same rule, that inapplicable step must not hold the take open. A take that can never reach
// Complete is a take that sits in the queue forever.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcNoAudioTest,
    "PCAP.TakeProcessing.NoAudioNoAudioSyncTrim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcNoAudioTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    const FTake BodyAndFace =
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ true, /*bAudio*/ false);

    const FPCAPProcessingPlan Plan = UPCAPTakeProcessingQueue::MakeProcessingPlan(BodyAndFace);
    TestFalse(TEXT("no audio -> no audio sync/trim"), Plan.bApplyAudioSyncTrim);
    TestTrue (TEXT("face stream -> HMC solve"),       Plan.bApplyHMCSolve);
    TestEqual(TEXT("four steps: body solve, HMC, retarget, merge"), Plan.ApplicableStepCount, 4);

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({ BodyAndFace });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));
    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestFalse(TEXT("audio sync/trim not planned"), State.bApplyAudioSyncTrim);
    TestFalse(TEXT("audio sync/trim does not apply"),
        UPCAPTakeProcessingQueue::StepApplies(State, EPCAPProcessingStep::AudioSyncTrim));
    TestEqual(TEXT("audio sync/trim left Pending"),
        (int32)State.AudioSyncTrim.Status, (int32)EProcessingStatus::Pending);
    TestTrue (TEXT("audio sync/trim carries no name"), State.AudioSyncTrim.StepName.IsEmpty());
    TestEqual(TEXT("HMC solve is outstanding work"),
        (int32)State.HMCSolve.Status, (int32)EProcessingStatus::Queued);

    FText Reason;
    TestFalse(TEXT("cannot complete an audio trim that does not apply"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::AudioSyncTrim, Reason));
    TestTrue (TEXT("refusal names the missing audio"), Reason.ToString().Contains(TEXT("audio")));

    // Work the four steps that do apply. The merge is terminal and needs the rest first; the body
    // retarget needs the body solve. The audio trim never applied, so it must not block either.
    TestTrue(TEXT("body solve marked complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue(TEXT("HMC solve marked complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::HMCSolve, Reason));
    TestTrue(TEXT("body retarget marked complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodyRetarget, Reason));
    TestTrue(TEXT("merge marked complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::MergeToSequencer, Reason));

    TestTrue (TEXT("take resolves after the attestations"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("take reads Complete without the audio step"),
        (int32)State.OverallStatus, (int32)EProcessingStatus::Complete);
    TestTrue (TEXT("take completion stamped"), State.bHasCompleted);
    TestTrue (TEXT("completion time recorded"), State.CompletedAt != FDateTime(0));
    TestEqual(TEXT("audio sync/trim still Pending, never claimed done"),
        (int32)State.AudioSyncTrim.Status, (int32)EProcessingStatus::Pending);
    TestFalse(TEXT("audio sync/trim never marked complete"), State.AudioSyncTrim.bHasCompleted);

    return true;
}

// Applicability is folded across the whole manifest, not read off the first subject: in a
// two-hander where only one performer wore a head-cam the take still needs an HMC solve. Before
// admission nothing applies at all, including the otherwise-unconditional merge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcPlanTest,
    "PCAP.TakeProcessing.PlanFollowsTheManifest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcPlanTest::RunTest(const FString&)
{
    // An empty manifest still merges — the locked pipeline always does, which is why
    // FTakeProcessingState carries four bApply* flags for five steps.
    FTake Bare = PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best,
                                      /*bBody*/ false, /*bFace*/ false, /*bAudio*/ false);
    Bare.SubjectManifest.Reset();

    const FPCAPProcessingPlan BarePlan = UPCAPTakeProcessingQueue::MakeProcessingPlan(Bare);
    TestFalse(TEXT("no streams -> no body solve"),  BarePlan.bApplyBodySolve);
    TestFalse(TEXT("no streams -> no HMC solve"),   BarePlan.bApplyHMCSolve);
    TestFalse(TEXT("no streams -> no retarget"),    BarePlan.bApplyBodyRetarget);
    TestFalse(TEXT("no streams -> no audio trim"),  BarePlan.bApplyAudioSyncTrim);
    TestEqual(TEXT("merge alone still applies"),    BarePlan.ApplicableStepCount, 1);

    // Two performers, one body-only, one face + audio. Every flag comes up.
    FTake TwoHander = PCAPTakeProcMakeTake(TEXT("001003_002"), ETakeLabel::Alt,
                                           /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false);
    FTakeSubjectSnapshot FacePerformer;
    FacePerformer.ActorID        = TEXT("sarahKov");
    FacePerformer.CharacterName  = TEXT("Emiko");
    FacePerformer.bHadFaceStream = true;
    FacePerformer.AudioChannels.Add(TEXT("ch02"));
    TwoHander.SubjectManifest.Add(FacePerformer);

    const FPCAPProcessingPlan TwoHanderPlan = UPCAPTakeProcessingQueue::MakeProcessingPlan(TwoHander);
    TestTrue (TEXT("first performer's body counts"),  TwoHanderPlan.bApplyBodySolve);
    TestTrue (TEXT("second performer's face counts"), TwoHanderPlan.bApplyHMCSolve);
    TestTrue (TEXT("retarget follows the body"),      TwoHanderPlan.bApplyBodyRetarget);
    TestTrue (TEXT("second performer's audio counts"),TwoHanderPlan.bApplyAudioSyncTrim);
    TestEqual(TEXT("all five steps apply"), TwoHanderPlan.ApplicableStepCount, 5);

    // A state that has never been admitted has no plan, so nothing applies to it — the merge
    // included, because its applicability is admission itself.
    const FTakeProcessingState Unadmitted;
    TestFalse(TEXT("unadmitted: body solve does not apply"),
        UPCAPTakeProcessingQueue::StepApplies(Unadmitted, EPCAPProcessingStep::BodySolveCleanup));
    TestFalse(TEXT("unadmitted: merge does not apply"),
        UPCAPTakeProcessingQueue::StepApplies(Unadmitted, EPCAPProcessingStep::MergeToSequencer));
    TestEqual(TEXT("unadmitted rolls up to Pending"),
        (int32)UPCAPTakeProcessingQueue::RollUpStatus(Unadmitted), (int32)EProcessingStatus::Pending);

    return true;
}

// The attestation state machine. A step may only be started once it is in the queue and its
// prerequisites are met, may not be restarted or re-completed, and a one-click completion straight
// from Queued stamps both times to the same instant rather than pretending to a duration.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcTransitionsTest,
    "PCAP.TakeProcessing.StepTransitionsAreGuarded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcTransitionsTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false),
        PCAPTakeProcMakeTake(TEXT("001003_002"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false)
    });
    const FPCAPTakeKey WorkedKey  = PCAPTakeProcMakeKey(TEXT("001003_001"));
    const FPCAPTakeKey OneClickKey = PCAPTakeProcMakeKey(TEXT("001003_002"));

    FText Reason;
    TestFalse(TEXT("a take outside the queue has no steps to start"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue (TEXT("refusal says to queue it first"), Reason.ToString().Contains(TEXT("queue")));

    TestEqual(TEXT("both takes queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 2);

    // The retarget consumes the cleaned-up body solve, so it cannot start ahead of it.
    TestFalse(TEXT("retarget blocked by the outstanding body solve"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodyRetarget, Reason));
    TestTrue (TEXT("refusal names the prerequisite"),
        Reason.ToString().Contains(UPCAPTakeProcessingQueue::StepDisplayName(EPCAPProcessingStep::BodySolveCleanup)));

    TestTrue(TEXT("body solve marked started"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, WorkedKey, State));
    TestEqual(TEXT("body solve In Progress"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::InProgress);
    TestTrue (TEXT("start recorded"), State.BodySolveCleanup.bHasStarted);
    TestTrue (TEXT("start time stamped"), State.BodySolveCleanup.StartedAt != FDateTime(0));
    TestFalse(TEXT("started is not completed"), State.BodySolveCleanup.bHasCompleted);
    TestTrue (TEXT("no end time yet"), State.BodySolveCleanup.CompletedAt == FDateTime(0));
    TestEqual(TEXT("take reads In Progress"),
        (int32)State.OverallStatus, (int32)EProcessingStatus::InProgress);

    TestFalse(TEXT("a started step cannot be started again"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue(TEXT("body solve marked complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, WorkedKey, State));
    TestEqual(TEXT("body solve Complete"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Complete);
    TestTrue (TEXT("completion recorded"), State.BodySolveCleanup.bHasCompleted);
    TestTrue (TEXT("end time stamped"), State.BodySolveCleanup.CompletedAt != FDateTime(0));
    TestTrue (TEXT("start still recorded"), State.BodySolveCleanup.bHasStarted);
    TestTrue (TEXT("end is not before the start"),
        State.BodySolveCleanup.CompletedAt >= State.BodySolveCleanup.StartedAt);

    // One step of three done, so the take is under way but not finished.
    TestEqual(TEXT("take still In Progress"),
        (int32)State.OverallStatus, (int32)EProcessingStatus::InProgress);
    TestFalse(TEXT("take not marked complete"), State.bHasCompleted);

    TestFalse(TEXT("a complete step cannot be restarted"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestFalse(TEXT("a complete step cannot be completed again"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, WorkedKey, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue(TEXT("retarget unblocked once the body solve is complete"),
        UPCAPTakeProcessingQueue::MarkStepStarted(DB, WorkedKey, EPCAPProcessingStep::BodyRetarget, Reason));

    // The one-click form an end-of-day pass wants: Queued straight to Complete. Both stamps land on
    // the same instant, which is the signature of an attestation made in a single action and not a
    // claim that the work took no time.
    TestTrue(TEXT("second take: complete straight from Queued"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, OneClickKey, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue (TEXT("second take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, OneClickKey, State));
    TestEqual(TEXT("body solve Complete"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Complete);
    TestTrue (TEXT("start back-filled"), State.BodySolveCleanup.bHasStarted);
    TestTrue (TEXT("single-action attestation: start == end"),
        State.BodySolveCleanup.StartedAt == State.BodySolveCleanup.CompletedAt);
    TestTrue (TEXT("stamp is a real time"), State.BodySolveCleanup.StartedAt != FDateTime(0));

    return true;
}

// A failure has to survive as a failure. The message is the only free text a step carries and the
// steps are per-take while the HMC solve and the retarget are really per-subject, so it is the one
// place "failed for actor B, actor A is fine" can live — an unexplained failure is refused. A take
// holding one failed step must read Failed however much of the rest is done.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcFailureTest,
    "PCAP.TakeProcessing.FailedStepRecordsItsError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcFailureTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ true, /*bAudio*/ false)
    });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));
    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);

    FText Reason;
    TestFalse(TEXT("a failure with no message is refused"),
        UPCAPTakeProcessingQueue::MarkStepFailed(DB, Key, EPCAPProcessingStep::BodySolveCleanup,
                                                 FString(), Reason));
    TestFalse(TEXT("whitespace is not a message"),
        UPCAPTakeProcessingQueue::MarkStepFailed(DB, Key, EPCAPProcessingStep::BodySolveCleanup,
                                                 TEXT("   "), Reason));

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("refused failures wrote nothing"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Queued);

    const FString Problem = TEXT("marker swap on the left hand, frames 220-410; re-solve in Shogun");
    TestTrue(TEXT("failure recorded with its message"),
        UPCAPTakeProcessingQueue::MarkStepFailed(DB, Key, EPCAPProcessingStep::BodySolveCleanup,
                                                 Problem, Reason));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("body solve Failed"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Failed);
    TestEqual(TEXT("message kept verbatim"), State.BodySolveCleanup.ErrorMessage, Problem);
    TestFalse(TEXT("a failed step is not complete"), State.BodySolveCleanup.bHasCompleted);
    TestTrue (TEXT("the step still ended, and when is recorded"),
        State.BodySolveCleanup.CompletedAt != FDateTime(0));
    TestEqual(TEXT("take reads Failed"), (int32)State.OverallStatus, (int32)EProcessingStatus::Failed);
    TestFalse(TEXT("take not marked complete"), State.bHasCompleted);

    TestFalse(TEXT("a failed step cannot be completed without a reset"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue (TEXT("refusal explains the reset"), Reason.ToString().Contains(TEXT("reset")));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("still Failed after the refusal"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Failed);
    TestEqual(TEXT("message survives the refusal"), State.BodySolveCleanup.ErrorMessage, Problem);

    // The HMC solve is independent of the body solve, so it can finish while that one is broken.
    // The take must still read Failed — a day's queue holding one failure never reads mostly done.
    TestTrue(TEXT("HMC solve marked complete alongside the failure"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::HMCSolve, Reason));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("failure still wins the rollup"),
        (int32)State.OverallStatus, (int32)EProcessingStatus::Failed);
    TestFalse(TEXT("take still not complete"), State.bHasCompleted);
    TestTrue (TEXT("take completion stamp still clear"), State.CompletedAt == FDateTime(0));

    // Retry: the failed step goes back to outstanding work, fully cleared.
    TestTrue(TEXT("failed step reset for a retry"),
        UPCAPTakeProcessingQueue::ResetStep(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("body solve back to Queued"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Queued);
    TestTrue (TEXT("error cleared"), State.BodySolveCleanup.ErrorMessage.IsEmpty());
    TestFalse(TEXT("start cleared"), State.BodySolveCleanup.bHasStarted);
    TestTrue (TEXT("start time cleared"), State.BodySolveCleanup.StartedAt == FDateTime(0));
    TestTrue (TEXT("end time cleared"),   State.BodySolveCleanup.CompletedAt == FDateTime(0));
    TestEqual(TEXT("take back to In Progress, the HMC solve still standing"),
        (int32)State.OverallStatus, (int32)EProcessingStatus::InProgress);

    return true;
}

// Reopening a step invalidates everything built on it. Leaving a merge reading Complete after the
// body solve it consumed was reopened would be a false record of the day.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcResetTest,
    "PCAP.TakeProcessing.ResetReopensDependentSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcResetTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false)
    });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));

    FText Reason;
    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);
    TestTrue(TEXT("body solve complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue(TEXT("retarget complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodyRetarget, Reason));
    TestTrue(TEXT("merge complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::MergeToSequencer, Reason));

    FTakeProcessingState State;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("take Complete"), (int32)State.OverallStatus, (int32)EProcessingStatus::Complete);

    TestTrue(TEXT("completed body solve can be reopened"),
        UPCAPTakeProcessingQueue::ResetStep(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));

    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, State));
    TestEqual(TEXT("body solve outstanding again"),
        (int32)State.BodySolveCleanup.Status, (int32)EProcessingStatus::Queued);
    TestEqual(TEXT("retarget reopened with it"),
        (int32)State.BodyRetarget.Status, (int32)EProcessingStatus::Queued);
    TestEqual(TEXT("merge reopened with it"),
        (int32)State.MergeToSequencer.Status, (int32)EProcessingStatus::Queued);
    TestFalse(TEXT("merge no longer claims completion"), State.MergeToSequencer.bHasCompleted);
    TestTrue (TEXT("merge end time cleared"), State.MergeToSequencer.CompletedAt == FDateTime(0));

    TestEqual(TEXT("take back to Queued"), (int32)State.OverallStatus, (int32)EProcessingStatus::Queued);
    TestFalse(TEXT("take completion withdrawn"), State.bHasCompleted);
    TestTrue (TEXT("take completion time cleared"), State.CompletedAt == FDateTime(0));

    TestEqual(TEXT("eligible for processing again"),
        UPCAPTakeProcessingQueue::CollectEligibleTakes(DB).Num(), 1);
    TestEqual(TEXT("back in the working queue"),
        UPCAPTakeProcessingQueue::CollectQueuedTakes(DB).Num(), 1);

    return true;
}

// A finished take must never be dragged back through the queue. The end-of-day batch is pressed
// repeatedly across a shoot, and a second press that re-planned a completed take would wipe a day
// of attestation — including any bApply* flag the operator had adjusted by hand.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcCompleteTest,
    "PCAP.TakeProcessing.CompleteTakesAreNotRequeued",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcCompleteTest::RunTest(const FString&)
{
    const FPCAPTakeProcNoAutoSave NoSave;

    UMocapDatabase* DB = PCAPTakeProcMakeDatabase({
        PCAPTakeProcMakeTake(TEXT("001003_001"), ETakeLabel::Best, /*bBody*/ true, /*bFace*/ false, /*bAudio*/ false)
    });
    const FPCAPTakeKey Key = PCAPTakeProcMakeKey(TEXT("001003_001"));

    FText Reason;
    TestEqual(TEXT("take queued"), UPCAPTakeProcessingQueue::QueueAll(DB).NumQueued, 1);
    TestTrue(TEXT("body solve complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodySolveCleanup, Reason));
    TestTrue(TEXT("retarget complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::BodyRetarget, Reason));
    TestTrue(TEXT("merge complete"),
        UPCAPTakeProcessingQueue::MarkStepComplete(DB, Key, EPCAPProcessingStep::MergeToSequencer, Reason));

    FTakeProcessingState Before;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, Before));
    TestEqual(TEXT("take Complete"), (int32)Before.OverallStatus, (int32)EProcessingStatus::Complete);
    TestTrue (TEXT("take completion stamped"), Before.bHasCompleted);

    TestEqual(TEXT("a complete take is no longer eligible"),
        UPCAPTakeProcessingQueue::CollectEligibleTakes(DB).Num(), 0);
    TestEqual(TEXT("a complete take has left the working queue"),
        UPCAPTakeProcessingQueue::CollectQueuedTakes(DB).Num(), 0);

    TestFalse(TEXT("a complete take cannot be queued by hand"),
        UPCAPTakeProcessingQueue::CanQueueTake(DB, Key, Reason));
    TestTrue (TEXT("refusal says it has finished"), Reason.ToString().Contains(TEXT("finished")));

    // A preview must promise exactly what the commit would do, so both are checked.
    const FPCAPQueueReport Preview = UPCAPTakeProcessingQueue::PreviewQueueAll(DB);
    TestEqual(TEXT("preview queues nothing"), Preview.NumQueued, 0);
    TestEqual(TEXT("preview counts it as already queued"), Preview.NumAlreadyQueued, 1);

    const FPCAPQueueReport SecondPass = UPCAPTakeProcessingQueue::QueueAll(DB);
    TestEqual(TEXT("second pass queues nothing"), SecondPass.NumQueued, 0);
    TestEqual(TEXT("second pass reports it already queued"), SecondPass.NumAlreadyQueued, 1);
    TestEqual(TEXT("second pass counts it as skipped"), SecondPass.NumSkipped, 1);
    TestEqual(TEXT("second pass carries no step payload"), SecondPass.Steps.Num(), 0);

    FTakeProcessingState After;
    TestTrue (TEXT("take resolves"), UPCAPTakeProcessingQueue::ReadProcessingState(DB, Key, After));
    TestEqual(TEXT("still Complete after a second pass"),
        (int32)After.OverallStatus, (int32)EProcessingStatus::Complete);
    TestEqual(TEXT("merge not reopened by the second pass"),
        (int32)After.MergeToSequencer.Status, (int32)EProcessingStatus::Complete);
    TestTrue (TEXT("admission time untouched"), After.QueuedAt == Before.QueuedAt);
    TestTrue (TEXT("completion time untouched"), After.CompletedAt == Before.CompletedAt);
    TestTrue (TEXT("merge completion time untouched"),
        After.MergeToSequencer.CompletedAt == Before.MergeToSequencer.CompletedAt);

    return true;
}

// The honesty contract, pinned so it cannot drift quietly. Body solve cleanup happens in Shogun
// Post / Motive and the HMC solve in MetaHuman Animator or the head-cam vendor's tool; Unreal
// cannot perform either, now or later, and every step's note names where the work really happens.
// The step order is the pipeline's, which is deliberately not FTakeProcessingState's member order —
// that struct declares the merge before the audio trim, and the merge is terminal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTakeProcExternalStepsTest,
    "PCAP.TakeProcessing.ExternalStepsStayExternal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTakeProcExternalStepsTest::RunTest(const FString&)
{
    const TArray<EPCAPProcessingStep> Steps = UPCAPTakeProcessingQueue::PipelineSteps();
    TestEqual(TEXT("five steps"), Steps.Num(), 5);
    if (Steps.Num() != 5) return false;

    TestEqual(TEXT("1: body solve cleanup"), (int32)Steps[0], (int32)EPCAPProcessingStep::BodySolveCleanup);
    TestEqual(TEXT("2: HMC solve"),          (int32)Steps[1], (int32)EPCAPProcessingStep::HMCSolve);
    TestEqual(TEXT("3: body retarget"),      (int32)Steps[2], (int32)EPCAPProcessingStep::BodyRetarget);
    TestEqual(TEXT("4: audio sync/trim"),    (int32)Steps[3], (int32)EPCAPProcessingStep::AudioSyncTrim);
    TestEqual(TEXT("5: merge to sequencer is terminal"),
        (int32)Steps[4], (int32)EPCAPProcessingStep::MergeToSequencer);

    TestTrue (TEXT("body solve cleanup is external to Unreal"),
        UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep::BodySolveCleanup));
    TestTrue (TEXT("HMC solve is external to Unreal"),
        UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep::HMCSolve));
    TestFalse(TEXT("body retarget is not classed external"),
        UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep::BodyRetarget));
    TestFalse(TEXT("audio sync/trim is not classed external"),
        UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep::AudioSyncTrim));
    TestFalse(TEXT("merge is not classed external"),
        UPCAPTakeProcessingQueue::IsStepExternal(EPCAPProcessingStep::MergeToSequencer));

    // The note is what a panel shows beside a step's controls, so it has to name the tool.
    TestTrue(TEXT("body solve note names Shogun"),
        UPCAPTakeProcessingQueue::StepExecutionNote(EPCAPProcessingStep::BodySolveCleanup).Contains(TEXT("Shogun")));
    TestTrue(TEXT("HMC solve note names MetaHuman Animator"),
        UPCAPTakeProcessingQueue::StepExecutionNote(EPCAPProcessingStep::HMCSolve).Contains(TEXT("MetaHuman Animator")));

    for (const EPCAPProcessingStep Step : Steps)
    {
        TestFalse(TEXT("every step has a display name"),
            UPCAPTakeProcessingQueue::StepDisplayName(Step).IsEmpty());

        // Including the three that could in principle be engine work: none of them has an executor,
        // so every one of the five is marked by a person.
        TestFalse(TEXT("every step says where it happens"),
            UPCAPTakeProcessingQueue::StepExecutionNote(Step).IsEmpty());
        TestTrue (TEXT("every step names the operator as the one who marks it"),
            UPCAPTakeProcessingQueue::StepExecutionNote(Step).Contains(TEXT("operator")));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
