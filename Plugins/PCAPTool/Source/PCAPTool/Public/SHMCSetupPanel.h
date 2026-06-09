#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PCAPToolTypes.h"

class UPCAPToolSubsystem;
class SVerticalBox;
class SEditableTextBox;
class SWindow;

class PCAPTOOL_API SHMCSetupPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SHMCSetupPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SVerticalBox> DeviceListBox;        // rebuilt only on device-set change
    TArray<FString> BuiltDeviceNames;
    TArray<TSharedPtr<FString>> ActorOptions;      // dropdown source (from database later)

    // ── Add Device modal ──────────────────────────────────────────────────────
    TSharedPtr<SWindow> ModalWindow;
    TSharedPtr<SEditableTextBox> ModalNameInput;
    TSharedPtr<SEditableTextBox> ModalIPInput;

    // ── Device list (build once + live bindings, like Preview) ────────────────
    void RefreshDeviceList();
    EActiveTimerReturnType OnRefreshTimer(double CurrentTime, float DeltaTime);
    TSharedRef<SWidget> BuildDeviceRow(const FString& DeviceName);
    TSharedRef<SWidget> BuildActorDropdown(const FString& DeviceName);

    FReply OnAddDeviceClicked();        // opens the modal
    FReply OnModalConnectClicked();     // saves Name+IP to the database, closes modal
    FReply OnModalCancelClicked();      // closes modal
    FReply OnPreppedClicked();          // Prepped for Preview → ConnectAll + SaveConfig
    FReply OnDisconnectDevice(FString DeviceName);
    void   OnActorChosen(FString DeviceName, FString ActorName);

    FHMCDeviceStatus GetStatus(const FString& DeviceName) const;
    static UPCAPToolSubsystem* GetSubsystem();

    // Colour helpers — retained for the Increment-2 control/vital panel.
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
