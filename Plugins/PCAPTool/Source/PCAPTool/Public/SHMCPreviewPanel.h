#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PCAPToolTypes.h"

class UPCAPToolSubsystem;
class SVerticalBox;
struct FSlateBrush;

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

    // Fast repaint loop (~30fps): feed textures update their pixels in place, so the
    // panel just needs to be invalidated to show new frames. Also keeps the editor
    // ticking, which keeps the HTTP frame pulls flowing.
    EActiveTimerReturnType OnFastRepaint(double CurrentTime, float DeltaTime);

    // Cards are built ONCE per device and bind every dynamic value to a lambda that
    // reads live status. The 30fps repaint updates them in place — no teardown /
    // rebuild (the full rebuild every 0.5s was the visible blink).
    TSharedRef<SWidget> BuildDeviceCard(const FString& DeviceName);
    TSharedRef<SWidget> BuildVitalBar(const FString& DeviceName);
    TSharedRef<SWidget> BuildFeed(const FString& DeviceName, int32 CameraIndex, const FString& Label);

    // Live status snapshot for a device (empty if unknown). Called by the binding lambdas.
    FHMCDeviceStatus GetStatus(const FString& DeviceName) const;

    // Device names currently built into cards — rebuild only when this set changes.
    TArray<FString> BuiltDeviceNames;

    // Persistent feed brushes, one per "DeviceName_Cam"; each points at the stable
    // reused texture and is repointed live by the feed image lambda.
    TMap<FString, TSharedPtr<FSlateBrush>> FeedBrushPersist;

    static UPCAPToolSubsystem* GetSubsystem();

    // Per-camera issue-driven feed border colour, plus the aggregated red error text
    // shown in the card's status line (empty when the device has no active issue).
    FLinearColor FeedBorderColor(const FString& DeviceName, int32 CameraIndex) const;
    FString      DeviceErrorText(const FString& DeviceName) const;
    // True when a connected device has no framing reference on either camera yet —
    // the automatic framing-drift check stays inactive until one is set in Setup.
    bool         FramingRefMissing(const FString& DeviceName) const;

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
