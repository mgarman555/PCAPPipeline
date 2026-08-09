#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "PCAPTakeRecorderSubsystem.generated.h"

class ULevelSequence;
class UMocapDatabase;
struct FShot;

UENUM(BlueprintType)
enum class EPCAPRecordState : uint8
{
    Ready      UMETA(DisplayName = "Ready"),
    Capturing  UMETA(DisplayName = "Capturing"),
    Reviewing  UMETA(DisplayName = "Reviewing")
};

// How PCAPTool behaves when the Mocap Manager's own recorder is driving Take Recorder.
// There is one Take Recorder transport, so two controllers on it means a double-record —
// the deferral policy exists to make that impossible.
UENUM(BlueprintType)
enum class EPCAPRecorderDeferral : uint8
{
    // Defer whenever an official session is detected (default, and the conservative choice):
    // PCAPTool stops driving the transport and observes + writes the record instead.
    Auto        UMETA(DisplayName = "Auto — defer when the Mocap Manager is active"),
    // Never drive the transport. Use on a shoot run entirely from the Mocap Manager, where
    // PCAPTool is present only for its databases, HMC/VCam and take records.
    AlwaysDefer UMETA(DisplayName = "Always defer to the Mocap Manager"),
    // PCAPTool always drives. Escape hatch for a false positive in detection — it removes
    // the double-record guard, so it warns when it lets a record through.
    NeverDefer  UMETA(DisplayName = "Never defer — PCAPTool always drives")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPCAPRecordStateChanged, EPCAPRecordState, NewState);

/**
 * Editor-lifetime record controller — the Phase 2 backend the Realtime Operator drives.
 *
 * Owns the record flow: validate active streams → set the take's output path + slate/take
 * from the active shot → drive Take Recorder → harvest the finished ULevelSequence into an
 * FTake on the active shot → flip to Reviewing for labelling.
 *
 * Access from Slate / Blueprint via GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>().
 *
 * Source arming is HYBRID (per design): a per-stage Take Recorder preset can seed the baseline,
 * and this controller adds a Live Link source per active body/face subject. The concrete Take
 * Recorder source classes are engine-private, so sources are added by UClass (resolved via
 * /Script path) and configured via FProperty reflection — that part is the one place expecting
 * a build-loop pass to confirm class paths / property names on 5.7.
 *
 * DEFERRAL (UE 5.8): the Mocap Manager ships its own recorder onto the same single Take
 * Recorder transport. When an official session is driving it, this controller does NOT drive
 * too — it adopts the in-flight take read-only, harvests it into an FTake exactly as if it had
 * started it, and publishes the FPCapTakeRecord row via UPCAPTakeRecordWriter. See
 * docs/specs/2026-08-09-pcap-take-record-reconciliation-design.md.
 */
UCLASS()
class PCAPTOOL_API UPCAPTakeRecorderSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // The TakeID the next record will produce (UI display). Empty if no active shot.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    FString PeekNextTakeID() const;

    // True if every active subject's body/face/audio streams are Connected.
    // OutError describes the first failure (empty when ready).
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool AreActiveStreamsReady(FString& OutError) const;

    // Start recording the active shot. Returns false (+ OutError) if not ready / already recording.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool StartRecordForActiveShot(FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    void StopRecord();

    // One-tap "Next Take": record the next take number of the current shot with the current setup.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool RecordNextTake(FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    EPCAPRecordState GetRecordState() const { return RecordState; }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool IsRecording() const;

    // Clears Reviewing back to Ready (call after the post-take label prompt is dismissed).
    // Also re-publishes the last harvested take's FPCapTakeRecord row, so the label the
    // operator just set (Best/Burn/…) reaches Epic's TakeStatus.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    void FinishReview();

    UPROPERTY(BlueprintAssignable, Category = "PCAP|Record")
    FOnPCAPRecordStateChanged OnRecordStateChanged;

    // ── Mocap Manager deferral (UE 5.8) ─────────────────────────────────────

    // True when an official Mocap Manager session is judged to be driving Take Recorder.
    // Two signals, in order: the session-state reflection probe (off until its holder is
    // confirmed on Windows — see the .cpp), then transport ownership — a recording is in
    // flight that PCAPTool did not start.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool IsMocapManagerSessionActive() const;

    // IsMocapManagerSessionActive() adjusted by the deferral mode. True = PCAPTool must not
    // touch the transport.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool ShouldDeferToMocapManager() const;

    // True while PCAPTool is riding along on a take someone else started: it harvests and
    // publishes the take but never drives play/stop.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool IsObservingExternalRecord() const { return bObservingExternalRecord; }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    EPCAPRecorderDeferral GetDeferralMode() const { return DeferralMode; }

    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    void SetDeferralMode(EPCAPRecorderDeferral NewMode);

    // Who owns the Take Recorder transport right now, for operator-facing display:
    // "PCAPTool", "Mocap Manager", or empty when nothing is recording.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    FString GetTransportOwner() const;

    // Re-write an already-recorded take's FPCapTakeRecord row (find-or-create by TakeID, so
    // this updates in place). Called by FinishReview; also usable after a late note or label
    // edit. Looks the take up in the last-harvested shot first, then the active selection.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    bool PublishTakeRecord(const FString& TakeID);

    // The most recent take harvested into the database this editor session (empty if none).
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    FString GetLastHarvestedTakeID() const { return LastHarvestedTakeID; }

private:
    EPCAPRecordState RecordState = EPCAPRecordState::Ready;

    // Identity of the take currently being recorded (captured at start, used at harvest).
    FString PendingTakeID;
    FString PendingProductionCode;
    FString PendingDayID;
    FString PendingSessionID;
    FString PendingShotID;

    bool PendingHasVCam = false;   // a VCam source was armed for the in-flight take

    // ── Transport ownership ─────────────────────────────────────────────────
    EPCAPRecorderDeferral DeferralMode = EPCAPRecorderDeferral::Auto;

    // PCAPTool called StartRecording for the take currently in flight. Set before the call
    // (TakeRecorderStarted fires from inside it) and cleared on finish.
    bool bPCAPDrivesActiveTake = false;

    // A take PCAPTool did not start is in flight and has been adopted read-only.
    bool bObservingExternalRecord = false;

    // Identity of the last take harvested into the database — used to re-publish its record
    // after labelling, without depending on the operator's current console selection.
    FString LastHarvestedTakeID;
    FString LastHarvestedProductionCode;
    FString LastHarvestedDayID;
    FString LastHarvestedSessionID;
    FString LastHarvestedShotID;

    UMocapDatabase* GetDB() const;
    void SetState(EPCAPRecordState NewState);

    // Stamp the in-flight (foreign) take with the active shot's identity so it harvests like
    // one of ours. False when there is no active shot to attribute it to.
    bool BeginObservedTake();
    void ClearPendingTake();

    // Reflection probe for the Mocap Manager's live session state. Returns true only when the
    // probe actually resolved (bOutActive then carries the answer); false = unresolvable, so
    // the caller falls back to transport ownership.
    static bool ProbeMocapManagerSessionFlag(bool& bOutActive);

    // Bound to UTakeRecorderSubsystem's dynamic delegates. These fire for EVERY take on the
    // transport, including the Mocap Manager's — which is what makes observe-mode work.
    UFUNCTION() void HandleTakeStarted();
    UFUNCTION() void HandleTakeFinished(ULevelSequence* SequenceAsset);

    // ── Reflection helpers (Take Recorder source classes are engine-private) ──
    // Adds a Live Link recording source for SubjectName to a UTakeRecorderSources*. Returns
    // true on success. Resolves /Script/LiveLinkSequencer.TakeRecorderLiveLinkSource and sets
    // its SubjectName (FName) via FProperty.
    bool AddLiveLinkSource(UObject* Sources, FName SubjectName) const;
    // Arms an actor recording source for Target (used for the VCam camera). Resolves
    // /Script/TakeRecorderSources.TakeRecorderActorSource and sets its Target via FProperty.
    bool AddActorSource(UObject* Sources, AActor* Target) const;
    static void SetSourceLazyActor(UObject* Source, const TCHAR* PropName, AActor* Value);
    static UClass* FindSourceClass(const TCHAR* ScriptPath);
    static void SetSourceFName(UObject* Source, const TCHAR* PropName, FName Value);
};
