#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCamConfig.generated.h"

// One transform offset (axis correction / zero origin / joystick stacking / saved pose).
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamAlignOffset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector  Translation = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FRotator Rotation    = FRotator::ZeroRotator;
};

// Position/rotation smoothing. Speeds are FMath::*InterpTo speeds; 10 is the WVCAM baseline.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamSmoothingConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") bool  bSmoothPosition   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="1.0", ClampMax="20.0"))
    float PositionSmoothing = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") bool  bSmoothRotation   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="1.0", ClampMax="20.0"))
    float RotationSmoothing = 10.f;
};

// World-space and camera-space scaling of the optical input.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamScaleConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector WorldSpaceScale  = FVector(1.f, 1.f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector CameraSpaceScale = FVector(1.f, 1.f, 1.f);
};

// Offline take-smoothing (the 4.26 VcamSequencer post-pass) applied to a recorded take's
// transform curve. Mirrors the pure FVCamSmoothingSettings; default None = non-destructive.
UENUM(BlueprintType)
enum class EPCAPVCamSmoothMethod : uint8
{
    None          UMETA(DisplayName = "None"),
    LowPassFilter UMETA(DisplayName = "Low-pass (Butterworth)"),
    Slerp         UMETA(DisplayName = "Slerp (rotation)"),
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamTakeSmoothingConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") EPCAPVCamSmoothMethod TranslationMethod = EPCAPVCamSmoothMethod::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") EPCAPVCamSmoothMethod RotationMethod    = EPCAPVCamSmoothMethod::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="1.0"))  float ResamplingFps       = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="0.1"))  float TranslationCutoffHz = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="0.1"))  float RotationCutoffHz    = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="0.0", ClampMax="1.0")) float RotationSlerpBlend = 0.8f;
};

// Persisted vcam config. One DataAsset per stage setup (saved under the project's
// content; referenced by the active stage). Mirrors UStageConfigAsset.
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPVCamConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // ── Mocap source (the camera's tracked pose) ─────────────────────────────────
    // The vcam rigid body arrives via Live Link under this subject name. MocapSourceIP/Port
    // identify the mocap server (e.g. Vicon DataStream, default port 801) the Live Link
    // source connects to — surfaced here so the whole vcam connection lives in one place.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Mocap Source") FName   LiveLinkSubjectName = "TPVCam";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Mocap Source") FString MocapSourceIP       = "127.0.0.1";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Mocap Source") int32   MocapSourcePort     = 801;

    // ── Controller feed (joystick / tombstone, raw packets over UDP) ──────────────
    // The plugin LISTENS on ControllerFeedIP:ControllerFeedPort for raw controller packets.
    // ControllerFeedIP is the local interface to bind ("0.0.0.0" = all interfaces); the port
    // must match the controller broadcaster's port.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Controller Feed") FString ControllerFeedIP   = "0.0.0.0";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Controller Feed") int32   ControllerFeedPort = 7401;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset AlignRigidBody; // axis correction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Setup;          // zero origin
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Navigate;       // joystick stacking

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamSmoothingConfig Smoothing;    // live per-frame smoothing
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamScaleConfig     Scaling;

    // Offline post-record smoothing of the take's Level Sequence (opt-in; default None).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam|Take Smoothing") FPCAPVCamTakeSmoothingConfig TakeSmoothing;

    // World-scale presets the controller cycles through (SelectNext/PreviousWorldScale).
    // WVCAM worldScales SeekingTable: [1, 2, 3, 5, 10] (non-looping). == native Tgain.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<FVector> WorldScalePresets = {FVector(1.f), FVector(2.f), FVector(3.f), FVector(5.f), FVector(10.f)};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveWorldScaleIndex = 0;

    // WVCAM default lens SeekingTable (mm), non-looping, clamps at the ends.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<float> FocalLengthPresets = {18.f, 25.f, 32.f, 40.f, 50.f, 75.f, 100.f};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") float ActiveFocalLength = 32.f;

    // Zoom integration clamp (syncLensesToVcam widens min/max to the lens table extents: 18..100).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") float MinFocalLength = 18.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") float MaxFocalLength = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveButtonLayout = 0;   // 0 Default, 1 Sony

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<FPCAPVCamAlignOffset> SavedPositions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveSavedPositionIndex = -1;
};
