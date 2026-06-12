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

// Persisted vcam config. One DataAsset per stage setup (saved under the project's
// content; referenced by the active stage). Mirrors UStageConfigAsset.
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPVCamConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FName LiveLinkSubjectName = "TPVCam";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset AlignRigidBody; // axis correction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Setup;          // zero origin
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Navigate;       // joystick stacking

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamSmoothingConfig Smoothing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamScaleConfig     Scaling;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<float> FocalLengthPresets = {18.f, 24.f, 35.f, 50.f, 85.f};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") float ActiveFocalLength = 35.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveButtonLayout = 0;   // 0 Default, 1 Var1, 2 Inverted

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<FPCAPVCamAlignOffset> SavedPositions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveSavedPositionIndex = -1;
};
