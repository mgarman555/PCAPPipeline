#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PCAPToolTypes.h"

class UPCAPToolSubsystem;
class SVerticalBox;
class SEditableTextBox;

// One pending device row before it's been saved and connected
struct FPendingDeviceRow
{
    TSharedPtr<SEditableTextBox> NameInput;
    TSharedPtr<SEditableTextBox> IPInput;
};

class PCAPTOOL_API SHMCSetupPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SHMCSetupPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SVerticalBox> ConnectedDeviceBox;  // refreshed by timer
    TSharedPtr<SVerticalBox> PendingRowBox;        // never touched by timer
    TArray<TSharedPtr<FPendingDeviceRow>> PendingRows;

    TSharedRef<SWidget> BuildConnectedDeviceRow(const FHMCDeviceConfig& Config,
                                                 const FHMCDeviceStatus& Status);
    TSharedRef<SWidget> BuildPendingRow(TSharedPtr<FPendingDeviceRow> Row);
    TSharedRef<SWidget> BuildVitalCell(const FString& Label, const FString& Value,
                                        const FLinearColor& ValueColor);

    void RefreshDeviceList();        // timer-safe: only rebuilds connected rows
    void AddPendingRow(TSharedPtr<FPendingDeviceRow> Row);
    void RemovePendingRowWidget(TSharedPtr<FPendingDeviceRow> Row);
    EActiveTimerReturnType OnRefreshTimer(double CurrentTime, float DeltaTime);

    FReply OnAddDeviceClicked();
    FReply OnSaveAndConnectClicked();
    FReply OnRemovePendingRow(TSharedPtr<FPendingDeviceRow> Row);
    FReply OnDisconnectDevice(FString DeviceName);
    FReply OnPingDevice(TSharedPtr<FPendingDeviceRow> Row);

    static UPCAPToolSubsystem* GetSubsystem();

    static FLinearColor VoltageColor(float V);
    static FLinearColor StorageColor(float MB);
    static FLinearColor CPUColor(float Pct);
    static FLinearColor TempColor(float C);

    const FLinearColor BgPanel  = FLinearColor(0.102f, 0.102f, 0.102f);
    const FLinearColor BgCard   = FLinearColor(0.141f, 0.141f, 0.141f);
    const FLinearColor BgStrip  = FLinearColor(0.082f, 0.082f, 0.082f);
    const FLinearColor ColGreen = FLinearColor(0.137f, 0.467f, 0.220f);
    const FLinearColor ColRed   = FLinearColor(0.800f, 0.133f, 0.133f);
    const FLinearColor ColYellow= FLinearColor(0.800f, 0.600f, 0.000f);
    const FLinearColor ColGray  = FLinearColor(0.333f, 0.333f, 0.333f);
    const FLinearColor ColMuted = FLinearColor(0.500f, 0.500f, 0.500f);
    const FLinearColor ColWhite = FLinearColor(0.900f, 0.900f, 0.900f);
};
