#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"
#include "Animation/AnimSequence.h"
#include "LevelSequence.h"

// IK asset types referenced via TSoftObjectPtr — forward declarations sufficient.
class UIKRigDefinition;
class UIKRetargeter;

#include "PCAPToolTypes.generated.h"

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EBodySystem : uint8
{
    None    UMETA(DisplayName = "None"),
    Xsens   UMETA(DisplayName = "Xsens"),
    Shogun  UMETA(DisplayName = "Shogun"),
    Motive  UMETA(DisplayName = "Motive"),
    Giant   UMETA(DisplayName = "Giant")
};

UENUM(BlueprintType)
enum class EFaceSystem : uint8
{
    None         UMETA(DisplayName = "None"),
    Technoprops  UMETA(DisplayName = "Technoprops HMC"),
    ARKit        UMETA(DisplayName = "ARKit")
};

UENUM(BlueprintType)
enum class EAudioSystem : uint8
{
    None            UMETA(DisplayName = "None"),
    RodeWirelessGo2 UMETA(DisplayName = "Rode Wireless Go 2"),
    DAW             UMETA(DisplayName = "DAW"),
    Timecode        UMETA(DisplayName = "Timecode")
};

UENUM(BlueprintType)
enum class EVCamSystem : uint8
{
    None         UMETA(DisplayName = "None"),
    Technoprops  UMETA(DisplayName = "Technoprops VCam"),
    iPadJoyCon   UMETA(DisplayName = "iPad + Joy-Con")
};

UENUM(BlueprintType)
enum class ETimecodeSource : uint8
{
    Hardware UMETA(DisplayName = "Hardware"),
    Software UMETA(DisplayName = "Software"),
    Clapper  UMETA(DisplayName = "Clapper")
};

UENUM(BlueprintType)
enum class EStreamStatus : uint8
{
    Connected    UMETA(DisplayName = "Connected"),
    Disconnected UMETA(DisplayName = "Disconnected"),
    Degraded     UMETA(DisplayName = "Degraded")
};

UENUM(BlueprintType)
enum class ETakeLabel : uint8
{
    Captured UMETA(DisplayName = "Captured"),
    Best     UMETA(DisplayName = "Best"),
    Alt      UMETA(DisplayName = "Alt"),
    Burn     UMETA(DisplayName = "Burn")
};

UENUM(BlueprintType)
enum class EProcessingStatus : uint8
{
    Pending    UMETA(DisplayName = "Pending"),
    Queued     UMETA(DisplayName = "Queued"),
    InProgress UMETA(DisplayName = "In Progress"),
    Complete   UMETA(DisplayName = "Complete"),
    Failed     UMETA(DisplayName = "Failed")
};

UENUM(BlueprintType)
enum class EShotType : uint8
{
    Production  UMETA(DisplayName = "Production"),
    Calibration UMETA(DisplayName = "Calibration"),
    TestShot    UMETA(DisplayName = "Test Shot"),
    Retargeting UMETA(DisplayName = "Retargeting")
};

UENUM(BlueprintType)
enum class EPreviewMode : uint8
{
    VCam             UMETA(DisplayName = "VCam Playback"),
    RealtimeViewport UMETA(DisplayName = "Realtime Viewport")
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCAPTOOL_API FRetargetConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    TSoftObjectPtr<USkeleton> SourceSkeletonAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    TSoftObjectPtr<UIKRigDefinition> IKRigSource;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    TSoftObjectPtr<UIKRigDefinition> IKRigTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    TSoftObjectPtr<UIKRetargeter> IKRetargeter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    bool HasFingerData = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Retarget")
    TSoftObjectPtr<UAnimSequence> FallbackHandPose;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FStageConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString ConfigName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EBodySystem BodySystem = EBodySystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EFaceSystem FaceSystem = EFaceSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EAudioSystem AudioSystem = EAudioSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EVCamSystem VCamSystem = EVCamSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString LiveLinkPresetPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FRetargetConfig RetargetChain;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    ETimecodeSource TimecodeSource = ETimecodeSource::Software;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString Notes;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FBodyStreamEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SuitID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FFaceStreamEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DeviceID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FAudioStreamEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ChannelID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DeviceLabel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InputLevel = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FShotSubject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool IsActive = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasBodyStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bHasBodyStream"))
    FBodyStreamEntry BodyStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasFaceStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bHasFaceStream"))
    FFaceStreamEntry FaceStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAudioStreamEntry> AudioStreams;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FPropEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool IsTracked = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="IsTracked"))
    FName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasStreamStatus = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bHasStreamStatus"))
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakeSubjectSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool HadBodyStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool HadFaceStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> AudioChannels;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakePropSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool WasTracked = false;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FProcessingStep
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString StepName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EProcessingStatus Status = EProcessingStatus::Pending;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime StartedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime CompletedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasStarted = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasCompleted = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakeProcessingState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EProcessingStatus OverallStatus = EProcessingStatus::Pending;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProcessingStep BodySolveCleanup;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProcessingStep HMCSolve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProcessingStep BodyRetarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProcessingStep MergeToSequencer;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProcessingStep AudioSyncTrim;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyBodySolve = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyHMCSolve = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyBodyRetarget = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyAudioSyncTrim = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime QueuedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime CompletedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasQueued = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasCompleted = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<ULevelSequence> OutputSequence;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTake
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString TakeID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString DayID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString ShotID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString TakeNumber;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString SessionID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing")
    FDateTime RecordedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Timing")
    float DurationSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Label")
    ETakeLabel Label = ETakeLabel::Captured;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Manifest")
    TArray<FTakeSubjectSnapshot> SubjectManifest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Manifest")
    TArray<FTakePropSnapshot> PropManifest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assets")
    TArray<TSoftObjectPtr<UAnimSequence>> BodyAnimAssets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assets")
    TArray<TSoftObjectPtr<UAnimSequence>> FaceAnimAssets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assets")
    TArray<TSoftObjectPtr<USoundWave>> AudioAssets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assets")
    TSoftObjectPtr<ULevelSequence> VCamAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assets")
    bool bHasVCam = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    EPreviewMode LastTakePreviewMode = EPreviewMode::RealtimeViewport;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    TSoftObjectPtr<UTexture2D> LastTakeViewportCapture;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview")
    bool bHasViewportCapture = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Processing")
    FTakeProcessingState ProcessingState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString DirectorNotes;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FShotEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ShotID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DayID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SessionID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FShotSubject> CalledActors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPropEntry> CalledProps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> LinkedTakeIDs;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FShot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ShotID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EShotType ShotType = EShotType::Production;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FShotSubject> Subjects;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPropEntry> Props;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FTake> Takes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FSession
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SessionID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FShot> Shots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FShotEntry> ShotList;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime StartedAt = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasStarted = false;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FShootDay
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DayID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime CalendarDate = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FSession> Sessions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridesStageConfig = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bOverridesStageConfig"))
    FStageConfig ActiveStageConfig;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FProduction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString ProductionName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString ProjectCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FDateTime CreatedDate = FDateTime(0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FStageConfig ActiveStageConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Days")
    TArray<FShootDay> Days;
};
