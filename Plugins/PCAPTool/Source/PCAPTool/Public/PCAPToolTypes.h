#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"
#include "Animation/AnimSequence.h"
#include "LevelSequence.h"

// Forward declarations — keeps IKRIG_API macros out of UHT's scan path.
// Any .cpp that resolves these soft refs must include:
//   "Rig/IKRigDefinition.h" and "Retargeter/IKRetargeter.h"
class UIKRigDefinition;
class UIKRetargeter;
class UStageConfigAsset;   // soft-ref'd by FProduction / FShootDay (see StageConfigAsset.h)

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

// FStageConfig was promoted to the UStageConfigAsset DataAsset — see StageConfigAsset.h.
// FProduction / FShootDay now reference it by TSoftObjectPtr<UStageConfigAsset>.

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

// ---------------------------------------------------------------------------
// HMC — Connection / Camera / Feed enums
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EHMCConnectionState : uint8
{
    Disconnected UMETA(DisplayName = "Disconnected"),
    Connected    UMETA(DisplayName = "Connected"),
    Offline      UMETA(DisplayName = "Offline")
};

UENUM(BlueprintType)
enum class EHMCCameraRole : uint8
{
    Top    UMETA(DisplayName = "Top"),
    Bottom UMETA(DisplayName = "Bottom"),
    Center UMETA(DisplayName = "Center"),
    Left   UMETA(DisplayName = "Left"),
    Right  UMETA(DisplayName = "Right")
};

UENUM(BlueprintType)
enum class EHMCFeedState : uint8
{
    Clear        UMETA(DisplayName = "Clear"),
    NeedsFix     UMETA(DisplayName = "Needs Fix"),
    Disconnected UMETA(DisplayName = "Disconnected")
};

// Capture pipeline a device's monitor checks run for. Set per device during
// shoot-day setup (an HMC assigned to an actor -> the asset they're shooting).
// Each pipeline defines its own active checks + thresholds + framing target
// (see UPCAPToolStatics::GetPipelineProfile).
UENUM(BlueprintType)
enum class ECapturePipeline : uint8
{
    MetaHumanHMC UMETA(DisplayName = "MetaHuman HMC"),
    FaceWareHMC  UMETA(DisplayName = "Faceware HMC")   // checks TBD — its own pipeline (not MetaHuman); runs no checks until its docs land
    // Future: ViconBody, OptiTrackBody, ... -- each a new check bundle.
};

// Camera setup for a device. The watcher resolves its definition by
// Pipeline x CaptureConfiguration -- the checks differ across these (e.g. a
// tripod is a head-and-shoulders shot, a head-mount is a tight face; stereo
// adds checkerboard board calibration). See GetDefinition().
UENUM(BlueprintType)
enum class ECaptureConfiguration : uint8
{
    MonoTripod      UMETA(DisplayName = "Tripod"),
    MonoHeadMount   UMETA(DisplayName = "Mono - Head Mount"),
    StereoHeadMount UMETA(DisplayName = "Stereo - Head Mount"),
    PhoneHeadMount  UMETA(DisplayName = "Phone - Head Mount")
};

// Per-camera framing reference captured at setup: where the actor's face should
// be. The monitor flags live drift from this. Normalized 0..1 frame coords.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FHMCFramingRef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bSet = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector2D Center = FVector2D(0.5, 0.5);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Size = 0.f;   // subject extent, frame fraction
};

// ---------------------------------------------------------------------------
// HMC Device Config — registration data (persisted to HMCConfig.json)
// ---------------------------------------------------------------------------

class UHMCRigEntry;   // library rig a live device was created from (soft ref on the config)


USTRUCT(BlueprintType)
struct PCAPTOOL_API FHMCDeviceConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DeviceName;     // e.g. "ORION" — operator-facing label

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString IPAddress;      // e.g. "192.168.50.117"

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorID;      // FShotSubject.ActorID — set after connection

    // DEPRECATED — unused. The HMC layer polls over HTTP; there is no WebSocket.
    // Retained only for HMCConfig.json save-format compatibility (avoids a schema
    // migration). Leave empty when registering devices. Do not remove this field.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString WebSocketEndpoint;

    // Capture pipeline this device's monitor checks run for (default MetaHuman HMC).
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECapturePipeline Pipeline = ECapturePipeline::MetaHumanHMC;

    // Camera setup -- selects the watcher definition together with Pipeline.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECaptureConfiguration CaptureConfig = ECaptureConfiguration::StereoHeadMount;

    // Per-camera "where the face should be" reference, captured at setup.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FHMCFramingRef FramingRef0;   // Top
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FHMCFramingRef FramingRef1;   // Bottom

    // Marked ready by the "Prepped for Preview" action. Only prepped devices appear
    // in HMC Preview. Persisted.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bPreppedForPreview = false;

    // ── Scan readiness (per actor on this device) — drives the Setup gate ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bPerformerPrepConfirmed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bNeutralCaptured = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bTeethCaptured   = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bROMCaptured     = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString NeutralStillPath;   // PNG (Saved/PCAPTool/Identity)
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString TeethStillPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ROMTakeLabel;       // marked device take id/label

    // Per-device tuned focus floor from the Setup focus helper; < 0 = use pipeline default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FocusMinOverride = -1.f;

    // ── Stereo calibration takes (checkerboard board — start & end of session) ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCalibStartCaptured = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCalibEndCaptured   = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CalibStartStillPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CalibEndStillPath;

    // The library rig this device was created from (soft — survives rename). Empty for
    // manually-added devices.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UHMCRigEntry> SourceRig;
};

// ---------------------------------------------------------------------------
// HMC Device Status — live state pulled from the device via HTTP polling
// (GET /control?cmd=no&param=, parsed from control.json)
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCAPTOOL_API FHMCDeviceStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString DeviceName;

    UPROPERTY(BlueprintReadOnly)
    FString IPAddress;

    UPROPERTY(BlueprintReadOnly)
    FString ActorID;

    UPROPERTY(BlueprintReadOnly)
    EHMCConnectionState ConnectionState = EHMCConnectionState::Disconnected;

    UPROPERTY(BlueprintReadOnly)
    bool bIsRecording = false;

    UPROPERTY(BlueprintReadOnly)
    float BatteryVoltage = 0.f;         // raw volts — 4S LiPo, nominal ~15.5V

    UPROPERTY(BlueprintReadOnly)
    float AvailableStorageMB = 0.f;

    UPROPERTY(BlueprintReadOnly)
    float CPUUsagePercent = 0.f;

    UPROPERTY(BlueprintReadOnly)
    float TemperatureCelsius = 0.f;

    UPROPERTY(BlueprintReadOnly)
    FString LastClipStatus;             // "Not ready", "Ready", etc.

    UPROPERTY(BlueprintReadOnly)
    FString StatusMessage;              // quip from GenerateStatusQuip()

    UPROPERTY(BlueprintReadOnly)
    FString CurrentTakeName;

    UPROPERTY(BlueprintReadOnly)
    FDateTime LastUpdateTime = FDateTime(0);

    UPROPERTY(BlueprintReadOnly)
    float FPS = 0.f;

    // ── Per-camera telemetry (control.json, parsed in OnPollResponse) ──
    UPROPERTY(BlueprintReadOnly) int32 DroppedFrames0 = 0;   // skippedFrames0
    UPROPERTY(BlueprintReadOnly) int32 DroppedFrames1 = 0;   // skippedFrames1
    UPROPERTY(BlueprintReadOnly) int32 Exposure0 = 0;        // exposure0 (raw, ~4550 normal)
    UPROPERTY(BlueprintReadOnly) int32 Exposure1 = 0;        // exposure1
    UPROPERTY(BlueprintReadOnly) int32 Gain0 = 0;            // gain0 (dB, 2 normal)
    UPROPERTY(BlueprintReadOnly) int32 Gain1 = 0;            // gain1
    UPROPERTY(BlueprintReadOnly) int32 TopLights = 0;        // topLights (0–100)
    UPROPERTY(BlueprintReadOnly) int32 BottomLights = 0;     // bottomLights (0–100)
    UPROPERTY(BlueprintReadOnly) bool  bStreaming0 = false;  // streaming0
    UPROPERTY(BlueprintReadOnly) bool  bStreaming1 = false;  // streaming1

    // Camera geometry (control.json). Sensor is landscape; rotation is applied for
    // display — a 2048x1536 sensor at rotation 90 shows as 1536x2048 portrait.
    UPROPERTY(BlueprintReadOnly) int32 FrameWidth  = 0;      // width  (e.g. 2048)
    UPROPERTY(BlueprintReadOnly) int32 FrameHeight = 0;      // height (e.g. 1536)
    UPROPERTY(BlueprintReadOnly) int32 Rotation0   = 0;      // rotation0 (degrees)
    UPROPERTY(BlueprintReadOnly) int32 Rotation1   = 0;      // rotation1 (degrees)
    UPROPERTY(BlueprintReadOnly) int32 BoomPos     = 0;      // boomPos (0 = Left, 1 = Right)

    // Hardware issue bitmasks (EHMCIssueFlag), evaluated each poll. Per camera.
    UPROPERTY(BlueprintReadOnly) int32 IssueFlags0 = 0;
    UPROPERTY(BlueprintReadOnly) int32 IssueFlags1 = 0;
};

// ---------------------------------------------------------------------------
// HMC issue flags — bitmask, OR'd together. NOT a UENUM (not BP-exposed as a
// type); the int32 masks live on FHMCDeviceStatus and are interpreted by
// UPCAPToolStatics. Bits 0–8 are hardware (from control.json); bits 16+ are
// operator-reported manual flags (the device has no sensor for these).
// ---------------------------------------------------------------------------
enum EHMCIssueFlag : int32
{
    HMC_Issue_None          = 0,
    HMC_Issue_NotStreaming  = 1 << 0,   // streamingN == 0
    HMC_Issue_Overexposed   = 1 << 1,   // exposureN > 7000
    HMC_Issue_Underexposed  = 1 << 2,   // exposureN < 1000
    HMC_Issue_DroppedFrames = 1 << 3,   // skippedFramesN > 0
    HMC_Issue_ClipNotReady  = 1 << 4,   // lastMovieIntegrityStatus != "Ready"
    HMC_Issue_LowBattery    = 1 << 5,   // batteryVoltage < 13.0
    HMC_Issue_LowStorage    = 1 << 6,   // availableStorageInMB < 10240
    HMC_Issue_HighCPU       = 1 << 7,   // cpuUsage > 80
    HMC_Issue_HighTemp      = 1 << 8,   // cpuTemp > 50
    HMC_Issue_NoFace        = 1 << 9,   // no subject in frame — derived from the
                                        // video pixels, NOT control.json (red)

    // Auto image-analysis flags — derived from the decoded frame, per camera
    // (see UPCAPToolStatics::AnalyzeFrameBGRA / MapMetricsToAutoFlags).
    HMC_Issue_OutOfFocus    = 1 << 10,  // variance-of-Laplacian below pipeline floor (red)
    HMC_Issue_UnevenLight   = 1 << 11,  // half/quadrant luma spread too high (amber)
    HMC_Issue_FramingDrift  = 1 << 12,  // subject drifted from the captured reference (red)
    HMC_Issue_Bumped        = 1 << 13,  // sudden position jump — mount knocked (red)
    HMC_Issue_Unstable      = 1 << 14,  // high position variance — loose / wobbling mount (red)
    HMC_Issue_LowFPS        = 1 << 15,  // device frameRate below the pipeline target (red)
    // (bits 16-20 are the retired HMC_Manual_* operator flags below)
    HMC_Issue_TooClose      = 1 << 21,  // subject size above the pipeline max -- pull back (red)
    HMC_Issue_TooFar        = 1 << 22,  // subject size below the pipeline min -- move closer (red)

    // Operator-reported (manual) bits — RETIRED (no UI; superseded by the automatic
    // framing check above). Kept for save-format / bit-stability compatibility only.
    HMC_Manual_FaceOffAxis  = 1 << 16,
    HMC_Manual_HeadsetShift = 1 << 17,
    HMC_Manual_OutOfFocus   = 1 << 18,
    HMC_Manual_LipSeal      = 1 << 19,
    HMC_Manual_Eyelid       = 1 << 20,
};

// Severity rollup for a set of flags — drives the feed border color.
UENUM(BlueprintType)
enum class EHMCIssueSeverity : uint8
{
    None  UMETA(DisplayName = "None"),    // green border
    Amber UMETA(DisplayName = "Amber"),   // amber border (warning)
    Red   UMETA(DisplayName = "Red")      // red border (hard issue)
};

// Operator-reported issues, BP-friendly. Enumerator order MUST match the
// HMC_Manual_* bit order in EHMCIssueFlag (bit = 1 << (16 + index)).
UENUM(BlueprintType)
enum class EHMCManualIssue : uint8
{
    FaceOffAxis  UMETA(DisplayName = "Face off-axis"),
    HeadsetShift UMETA(DisplayName = "Headset shifted"),
    OutOfFocus   UMETA(DisplayName = "Out of focus"),
    LipSeal      UMETA(DisplayName = "Lip seal not visible"),
    Eyelid       UMETA(DisplayName = "Eyelid not visible")
};

// Which way the light is wrong -- Epic's documented bad-lighting cases. Even = OK;
// over/under-exposure are handled separately by the exposure check.
UENUM(BlueprintType)
enum class EHMCLightDir : uint8
{
    Even   UMETA(DisplayName = "Even"),
    Below  UMETA(DisplayName = "Lit from below"),
    Side   UMETA(DisplayName = "Lit from side"),
    Back   UMETA(DisplayName = "Back-lit"),
    Shadow UMETA(DisplayName = "Shadows present")
};

// Coarse state of a stereo calibration-board take (heuristic from edge energy + the
// bright-region position/size). Fine pose cases — inverted / horizontal / over-rotated
// / hand-occluded / actor-present — need CV and stay operator-judged (see the checklist).
UENUM(BlueprintType)
enum class EHMCBoardState : uint8
{
    Good        UMETA(DisplayName = "Board OK"),
    NotDetected UMETA(DisplayName = "No board / occluded"),
    TooClose    UMETA(DisplayName = "Board too close"),
    TooFar      UMETA(DisplayName = "Board too far"),
    OffCenter   UMETA(DisplayName = "Board off-centre")
};

// Per-pipeline check bundle: which checks are active + their thresholds + the
// framing target. Resolved per (pipeline x configuration) by
// UPCAPToolStatics::GetDefinition(); GetPipelineProfile() gives the pipeline base.
// Plain struct (engine-internal; not Blueprint-exposed).
struct FPipelineCheckProfile
{
    bool  bCheckSubject   = true;
    bool  bCheckFraming   = true;
    bool  bCheckFocus     = true;
    bool  bCheckExposure  = true;
    bool  bCheckLighting  = true;

    float FocusMin        = 0.0f;          // var-of-Laplacian floor; 0 = off until tuned on rig
    float BlownFracMax    = 0.05f;         // blown fraction above this -> overexposed
    float MeanLumaMin     = 40.f / 255.f;  // mean luma (0..1) below this -> underexposed
    float RegionSpreadMax = 0.25f;         // (max-min)/mean above this -> uneven lighting

    FVector2D FramingTargetCenter = FVector2D(0.5, 0.5);  // pipeline-ideal center
    float FramingSizeMin   = 0.45f;        // acceptable subject size, frame fraction
    float FramingSizeMax   = 0.85f;
    float FramingCenterTol = 0.10f;        // captured ref must land within this of target
    float FramingDriftTol  = 0.08f;        // live subject may drift this far from the ref

    float BumpJumpMin       = 0.06f;       // sudden one-sample centroid jump -> bump
    float InstabilityStdMax = 0.03f;       // centroid std-dev over the window -> unstable mount
    float BumpHoldSeconds   = 1.2f;        // how long a detected bump stays latched red

    float     TargetFPS         = 30.f;    // below this -> LowFPS (MetaHuman: 60 ideal, 30 for now)
    bool      bClassifyLightDir = true;    // run lighting-direction classification (vs plain spread)
    FVector2D FocusRegionCenter = FVector2D(0.5, 0.55);  // nasolabial band centre (normalized)
    float     FocusRegionExtent = 0.28f;   // half-extent of the focus window; >= 0.5 = whole frame

    // Stereo calibration-board coarse check (enabled by GetDefinition for StereoHeadMount).
    bool  bCheckBoard    = false;
    float BoardEdgeMin   = 0.02f;          // whole-frame edge energy below this -> no board / occluded
    float BoardSizeMin   = 0.35f;          // bright-region extent below this -> board too far
    float BoardSizeMax   = 0.92f;          // above this -> board too close
    float BoardCenterTol = 0.28f;          // centroid this far from frame centre -> off-centre
};

// Output of one frame's image analysis (per camera). Stored per "Device_Cam" in
// the subsystem; the Setup read-outs and MapMetricsToAutoFlags consume it.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FHMCImageMetrics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) bool  bValid       = false;  // false until first analysis
    UPROPERTY(BlueprintReadOnly) bool  bHasSubject  = false;
    UPROPERTY(BlueprintReadOnly) float FocusScore   = 0.f;    // normalized var-of-Laplacian
    UPROPERTY(BlueprintReadOnly) float MeanLuma     = 0.f;    // 0..1
    UPROPERTY(BlueprintReadOnly) float BlownFrac    = 0.f;    // 0..1 (pixels >= 250)
    UPROPERTY(BlueprintReadOnly) float CrushedFrac  = 0.f;    // 0..1 (pixels <= 5)
    UPROPERTY(BlueprintReadOnly) float RegionSpread = 0.f;    // (max-min)/mean across regions
    UPROPERTY(BlueprintReadOnly) FVector2D SubjectCenter = FVector2D(0.5, 0.5);  // normalized
    UPROPERTY(BlueprintReadOnly) float SubjectSize  = 0.f;    // normalized extent

    // Lighting-direction classification + the per-region luma it's derived from.
    UPROPERTY(BlueprintReadOnly) EHMCLightDir LightDir = EHMCLightDir::Even;
    UPROPERTY(BlueprintReadOnly) float TopMean    = 0.f;     // 0..1, upper-half mean luma
    UPROPERTY(BlueprintReadOnly) float BottomMean = 0.f;     // lower-half
    UPROPERTY(BlueprintReadOnly) float LeftMean   = 0.f;
    UPROPERTY(BlueprintReadOnly) float RightMean  = 0.f;
    UPROPERTY(BlueprintReadOnly) float CenterMean = 0.f;     // central region (for back-lit)
    UPROPERTY(BlueprintReadOnly) float EdgeEnergy = 0.f;     // whole-frame var-of-Laplacian (checkerboard reads high, smooth/occluded low)
};

// ---------------------------------------------------------------------------
// HMC Camera Feed — one camera per entry, grouped by ActorID in Preview
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct PCAPTOOL_API FHMCCameraFeed
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString DeviceName;

    UPROPERTY(BlueprintReadWrite)
    FString ActorID;

    UPROPERTY(BlueprintReadWrite)
    EHMCCameraRole Role = EHMCCameraRole::Top;

    // FeedState: manually toggled in Setup (Clear/NeedsFix).
    // Set to Disconnected by the component when a device poll fails (offline).
    // Never manually set to Disconnected from the UI.
    UPROPERTY(BlueprintReadWrite)
    EHMCFeedState FeedState = EHMCFeedState::Disconnected;

    // Null until a frame arrives. Widget shows "No Feed" placeholder when null.
    // Texture lifetime managed by UHMCMonitorComponent::FrameTextureCache.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UTexture2D> FrameTexture = nullptr;

    UPROPERTY(BlueprintReadOnly)
    FString Timecode;

    // Camera index on device (0 = first cam, 1 = second cam).
    // Used internally to build HTTP frame requests (/video?cam=CameraIndex+1).
    UPROPERTY(BlueprintReadOnly)
    int32 CameraIndex = 0;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FShotSubject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorID;            // ref → UActorRosterEntry.ActorID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsActive = false;     // called to this shot

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

    // What this performance drives in the scene (static/skeletal mesh, MetaHuman, or
    // placed level actor). Assigned by search in the operator UI; swappable per shot
    // for quick changeover. Empty = not yet assigned.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UObject> DrivenTarget;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FPropEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropID;             // ref → UPropRosterEntry.PropID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsTracked = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bIsTracked"))
    FName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakeSubjectSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ActorID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHadBodyStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHadFaceStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> AudioChannels;

    // What this performance drove (record-time provenance of FShotSubject.DrivenTarget).
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UObject> DrivenTarget;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FTakePropSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bWasTracked = false;
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
    TSoftObjectPtr<ULevelSequence> MasterSequence;   // assembled take — entry-point sequence

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString CommentatorNotes;   // added June 2026
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
    TSoftObjectPtr<UStageConfigAsset> ActiveStageConfig;   // null = inherit production

    // Day call sheet — what is called for the whole day (roster ID references).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
    TArray<FString> CalledActorIDs;   // → UActorRosterEntry.ActorID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
    TArray<FString> CalledPropIDs;    // → UPropRosterEntry.PropID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
    TArray<FString> CalledVCamIDs;    // → UPCAPVCamConfig asset name
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
    TSoftObjectPtr<UStageConfigAsset> ActiveStageConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Days")
    TArray<FShootDay> Days;
};
