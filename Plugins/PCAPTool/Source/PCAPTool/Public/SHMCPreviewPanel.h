#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PCAPToolTypes.h"

class UPCAPToolSubsystem;
class SVerticalBox;

class PCAPTOOL_API SHMCPreviewPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SHMCPreviewPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SVerticalBox> CardContainer;

    void RefreshCards();
    EActiveTimerReturnType OnRefreshTimer(double CurrentTime, float DeltaTime);

    TSharedRef<SWidget> BuildDeviceCard(const FHMCDeviceStatus& Status);
    TSharedRef<SWidget> BuildStatusStrip(const FHMCDeviceStatus& Status);
    TSharedRef<SWidget> BuildVitalBar(const FHMCDeviceStatus& Status);
    TSharedRef<SWidget> BuildFeedPlaceholder(const FString& Label);

    static UPCAPToolSubsystem* GetSubsystem();

    static FLinearColor CardStatusColor(const FHMCDeviceStatus& Status);
    static FLinearColor VoltageColor(float V);
    static FLinearColor StorageColor(float MB);
    static FLinearColor CPUColor(float Pct);
    static FLinearColor TempColor(float C);
    static FString FormatStorage(float MB);
    static FString FormatSecondsSince(const FDateTime& T);

    const FLinearColor BgPanel  = FLinearColor(0.102f, 0.102f, 0.102f);
    const FLinearColor BgCard   = FLinearColor(0.141f, 0.141f, 0.141f);
    const FLinearColor BgFeed   = FLinearColor(0.031f, 0.031f, 0.031f);
    const FLinearColor ColGreen = FLinearColor(0.137f, 0.467f, 0.220f);
    const FLinearColor ColRed   = FLinearColor(0.800f, 0.133f, 0.133f);
    const FLinearColor ColYellow= FLinearColor(0.800f, 0.600f, 0.000f);
    const FLinearColor ColGray  = FLinearColor(0.333f, 0.333f, 0.333f);
    const FLinearColor ColMuted = FLinearColor(0.500f, 0.500f, 0.500f);
    const FLinearColor ColWhite = FLinearColor(0.900f, 0.900f, 0.900f);
};
