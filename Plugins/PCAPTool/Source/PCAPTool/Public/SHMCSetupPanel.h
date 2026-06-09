#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PCAPToolTypes.h"

class UPCAPToolSubsystem;
class SVerticalBox;
class SEditableTextBox;
class SWindow;
struct FSlateBrush;

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
    void   OnActorChosen(FString DeviceName, FString ActorID);

    // ── Detail panel (selected device: live feed + controls) ──────────────────
    FString ActiveDeviceName;                                  // selected row
    TMap<FString, TSharedPtr<FSlateBrush>> FeedBrushPersist;   // "Device_Cam"

    FReply OnSelectDevice(FString DeviceName);
    EActiveTimerReturnType OnFastRepaint(double CurrentTime, float DeltaTime);

    TSharedRef<SWidget> BuildDetailPanel();
    TSharedRef<SWidget> BuildSetupFeed(int32 CameraIndex, const FString& Label);  // styled like Preview
    TSharedRef<SWidget> BuildRoleDropdown(int32 CameraIndex);                      // Top/Bottom/Left/Right
    TSharedRef<SWidget> BuildBoomDropdown();                                       // Left/Right
    TSharedRef<SWidget> BuildStepper(const FString& Label,
        TFunction<FText()> ValueFn, FOnClicked OnMinus, FOnClicked OnPlus);

    // Issue-driven feed border + banner (mirrors Preview's look).
    FLinearColor FeedBorderColor(const FString& DeviceName, int32 CameraIndex) const;
    FString      FeedBannerText(const FString& DeviceName, int32 CameraIndex) const;
    EHMCCameraRole GetCameraRole(const FString& DeviceName, int32 CameraIndex) const;
    static FString RoleName(EHMCCameraRole Role);

    // Controls operate on ActiveDeviceName via SendDeviceCommand (ganged = both cams).
    // Exposure is three single-digit dropdowns: ones . tenths hundredths (e.g. 4.55).
    TSharedRef<SWidget> BuildExposureControl();
    TSharedRef<SWidget> BuildExposureDigit(int32 Place);   // 0=ones, 1=tenths, 2=hundredths
    void   OnExposureDigit(int32 Place, int32 Digit);
    FReply OnGainStep(int32 Dir);       // ±1 dB
    FReply OnLightStep(bool bTop, int32 Dir);  // ±5
    void   OnCameraRoleChosen(int32 CameraIndex, int32 RoleValue);  // SetCameraRole
    void   OnBoomChosen(int32 Side);    // 0 = Left, 1 = Right

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
