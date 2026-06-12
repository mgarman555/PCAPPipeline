#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UPCAPVCamConfig;
class UPCAPVCamSubsystem;
class SBox;
struct FAssetData;

/**
 * VCAM Operator Panel (Tool) — the on-screen control surface that replaces the WVCAM
 * 3D tab. Picks a UPCAPVCamConfig, shows the live TPVCam status/transform, and drives
 * the camera through UPCAPVCamSubsystem (Zero Space, locks, smoothing, lens, saved
 * positions, button layout). Live controller input (joysticks) is a later pass; this
 * panel is the operator's manual + status surface.
 */
class PCAPTOOL_API SPCAPVCamPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPVCamPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    UPCAPVCamSubsystem* GetVCam() const;

    TSharedPtr<SBox> StatusBox;   // live readout (rebuilt ~10Hz)
    TSharedPtr<SBox> BodyBox;     // controls (rebuilt on config change)

    void RebuildStatus();
    void RebuildBody();
    EActiveTimerReturnType PollStatus(double InCurrentTime, float InDeltaTime);

    TSharedRef<SWidget> BuildConfigPicker();
    void OnConfigPicked(const FAssetData& Asset);

    TSharedRef<SWidget> BuildTransformSection();
    TSharedRef<SWidget> BuildLensSection();
    TSharedRef<SWidget> BuildNavigationSection();
    TSharedRef<SWidget> BuildControllerSection();

    TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Content);
    TSharedRef<SWidget> MakeToggle(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FLinearColor ColRed   = FLinearColor(0.878f, 0.251f, 0.251f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
