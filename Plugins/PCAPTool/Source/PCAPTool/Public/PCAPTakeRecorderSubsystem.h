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
    UFUNCTION(BlueprintCallable, Category = "PCAP|Record")
    void FinishReview();

    UPROPERTY(BlueprintAssignable, Category = "PCAP|Record")
    FOnPCAPRecordStateChanged OnRecordStateChanged;

private:
    EPCAPRecordState RecordState = EPCAPRecordState::Ready;

    // Identity of the take currently being recorded (captured at start, used at harvest).
    FString PendingTakeID;
    FString PendingProductionCode;
    FString PendingDayID;
    FString PendingSessionID;
    FString PendingShotID;

    UMocapDatabase* GetDB() const;
    void SetState(EPCAPRecordState NewState);

    // Bound to UTakeRecorderSubsystem's dynamic delegates.
    UFUNCTION() void HandleTakeStarted();
    UFUNCTION() void HandleTakeFinished(ULevelSequence* SequenceAsset);

    // ── Reflection helpers (Take Recorder source classes are engine-private) ──
    // Adds a Live Link recording source for SubjectName to a UTakeRecorderSources*. Returns
    // true on success. Resolves /Script/LiveLinkSequencer.TakeRecorderLiveLinkSource and sets
    // its SubjectName (FName) via FProperty.
    bool AddLiveLinkSource(UObject* Sources, FName SubjectName) const;
    static UClass* FindSourceClass(const TCHAR* ScriptPath);
    static void SetSourceFName(UObject* Source, const TCHAR* PropName, FName Value);
};
