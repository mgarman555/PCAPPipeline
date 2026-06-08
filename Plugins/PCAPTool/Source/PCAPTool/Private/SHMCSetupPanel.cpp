#include "SHMCSetupPanel.h"
#include "PCAPToolSubsystem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"

UPCAPToolSubsystem* SHMCSetupPanel::GetSubsystem()
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
}

void SHMCSetupPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // ── Panel header ───────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 10.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("HMC SETUP")))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("+ Add Device")))
                    .OnClicked(this, &SHMCSetupPanel::OnAddDeviceClicked)
                ]
            ]

            // ── Device list ────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SVerticalBox)
                    // Connected devices — rebuilt by timer
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SAssignNew(ConnectedDeviceBox, SVerticalBox)
                    ]
                    // Pending input rows — only modified by Add/Remove, never by timer
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SAssignNew(PendingRowBox, SVerticalBox)
                    ]
                ]
            ]

            // ── Save bar ───────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(12.f, 8.f))
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(1.f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Enter device name and IP, then save.")))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Save and Connect All")))
                        .OnClicked(this, &SHMCSetupPanel::OnSaveAndConnectClicked)
                    ]
                ]
            ]
        ]
    ];

    // Populate with any already-registered devices and start refresh timer
    RefreshDeviceList();

    // Auto-connect devices restored from HMCConfig.json. Without this, saved
    // devices sit Disconnected (zero vitals) until the operator manually re-saves,
    // because Initialize() loads config but never starts the poll. Idempotent —
    // ConnectDevice early-outs on devices already polling.
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
        Sub->ConnectAll();

    RegisterActiveTimer(2.0f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SHMCSetupPanel::OnRefreshTimer));
}

EActiveTimerReturnType SHMCSetupPanel::OnRefreshTimer(double CurrentTime, float DeltaTime)
{
    RefreshDeviceList();
    return EActiveTimerReturnType::Continue;
}

void SHMCSetupPanel::RefreshDeviceList()
{
    // Only refreshes connected device rows — never touches pending input rows.
    // This prevents the timer from destroying text boxes mid-typing.
    if (!ConnectedDeviceBox.IsValid()) return;
    UPCAPToolSubsystem* Sub = GetSubsystem();
    if (!Sub) return;

    ConnectedDeviceBox->ClearChildren();

    for (const FHMCDeviceConfig& Config : Sub->GetRegisteredDevices())
    {
        FHMCDeviceStatus Status = Sub->GetDeviceStatus(Config.DeviceName);
        ConnectedDeviceBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(8.f, 4.f))
        [
            BuildConnectedDeviceRow(Config, Status)
        ];
    }
}

void SHMCSetupPanel::AddPendingRow(TSharedPtr<FPendingDeviceRow> Row)
{
    if (!PendingRowBox.IsValid()) return;
    PendingRowBox->AddSlot()
    .AutoHeight()
    .Padding(FMargin(8.f, 4.f))
    [
        BuildPendingRow(Row)
    ];
}

void SHMCSetupPanel::RemovePendingRowWidget(TSharedPtr<FPendingDeviceRow> Row)
{
    if (!PendingRowBox.IsValid()) return;

    // Rebuild pending box without this row
    PendingRowBox->ClearChildren();
    for (TSharedPtr<FPendingDeviceRow>& R : PendingRows)
    {
        PendingRowBox->AddSlot()
        .AutoHeight()
        .Padding(FMargin(8.f, 4.f))
        [
            BuildPendingRow(R)
        ];
    }
}

TSharedRef<SWidget> SHMCSetupPanel::BuildConnectedDeviceRow(const FHMCDeviceConfig& Config,
                                                              const FHMCDeviceStatus& Status)
{
    const bool bConnected = (Status.ConnectionState == EHMCConnectionState::Connected);
    const FLinearColor DotColor = bConnected ? ColGreen :
        (Status.ConnectionState == EHMCConnectionState::Offline ? ColRed : ColGray);
    const FString StateText = bConnected ? TEXT("Connected") :
        (Status.ConnectionState == EHMCConnectionState::Offline ? TEXT("Offline") : TEXT("Disconnected"));

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // Identity row
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 10.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(Config.DeviceName))
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(Config.IPAddress))
                            .ColorAndOpacity(FSlateColor(ColMuted))
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Status.StatusMessage.IsEmpty()
                            ? TEXT("Waiting for data...") : Status.StatusMessage))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                ]

                // Status indicator
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(8.f, 0.f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(StateText))
                    .ColorAndOpacity(FSlateColor(DotColor))
                ]

                // Disconnect button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Disconnect")))
                    .OnClicked(this, &SHMCSetupPanel::OnDisconnectDevice, Config.DeviceName)
                ]
            ]

            // Vitals strip (only when connected and data available)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(12.f, 6.f))
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)
                    [
                        BuildVitalCell(TEXT("BATTERY"),
                            FString::Printf(TEXT("%.1f V"), Status.BatteryVoltage),
                            VoltageColor(Status.BatteryVoltage))
                    ]

                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)
                    [
                        BuildVitalCell(TEXT("STORAGE"),
                            FString::Printf(TEXT("%.0f GB"), Status.AvailableStorageMB / 1024.f),
                            StorageColor(Status.AvailableStorageMB))
                    ]

                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)
                    [
                        BuildVitalCell(TEXT("CPU"),
                            FString::Printf(TEXT("%.0f%%"), Status.CPUUsagePercent),
                            CPUColor(Status.CPUUsagePercent))
                    ]

                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)
                    [
                        BuildVitalCell(TEXT("TEMP"),
                            FString::Printf(TEXT("%.0f°C"), Status.TemperatureCelsius),
                            TempColor(Status.TemperatureCelsius))
                    ]

                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        BuildVitalCell(TEXT("LAST CLIP"),
                            Status.LastClipStatus.IsEmpty() ? TEXT("—") : Status.LastClipStatus,
                            Status.LastClipStatus == TEXT("Ready") ? ColGreen : ColYellow)
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> SHMCSetupPanel::BuildPendingRow(TSharedPtr<FPendingDeviceRow> Row)
{
    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(12.f, 10.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)

                // Name field
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .Padding(0, 0, 8, 0)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("NAME")))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SAssignNew(Row->NameInput, SEditableTextBox)
                        .HintText(FText::FromString(TEXT("ORION")))
                    ]
                ]

                // IP field
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .Padding(0, 0, 8, 0)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("IP ADDRESS")))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SAssignNew(Row->IPInput, SEditableTextBox)
                        .HintText(FText::FromString(TEXT("192.168.50.x")))
                    ]
                ]

                // Ping button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Bottom)
                .Padding(0, 0, 4, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Ping")))
                    .OnClicked(this, &SHMCSetupPanel::OnPingDevice, Row)
                ]

                // Remove button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Bottom)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("✕")))
                    .OnClicked(this, &SHMCSetupPanel::OnRemovePendingRow, Row)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 6, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Name and IP are saved to the database on completion.")))
                .ColorAndOpacity(FSlateColor(ColMuted))
            ]
        ];
}

TSharedRef<SWidget> SHMCSetupPanel::BuildVitalCell(const FString& Label, const FString& Value,
                                                     const FLinearColor& ValueColor)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
            .ColorAndOpacity(FSlateColor(ColMuted))
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(Value))
            .ColorAndOpacity(FSlateColor(ValueColor))
        ];
}

FReply SHMCSetupPanel::OnAddDeviceClicked()
{
    TSharedPtr<FPendingDeviceRow> NewRow = MakeShared<FPendingDeviceRow>();
    PendingRows.Add(NewRow);
    AddPendingRow(NewRow);  // appends widget only — does not clear existing rows
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnSaveAndConnectClicked()
{
    UPCAPToolSubsystem* Sub = GetSubsystem();
    if (!Sub) return FReply::Handled();

    for (TSharedPtr<FPendingDeviceRow>& Row : PendingRows)
    {
        if (!Row->NameInput.IsValid() || !Row->IPInput.IsValid()) continue;

        const FString Name = Row->NameInput->GetText().ToString();
        const FString IP   = Row->IPInput->GetText().ToString();

        if (Name.IsEmpty() || IP.IsEmpty()) continue;

        FHMCDeviceConfig Config;
        Config.DeviceName        = Name;
        Config.IPAddress         = IP;
        Config.WebSocketEndpoint = FString::Printf(TEXT("ws://%s/ws"), *IP);

        Sub->RegisterDevice(Config);
    }

    PendingRows.Empty();
    Sub->ConnectAll();
    RefreshDeviceList();
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnRemovePendingRow(TSharedPtr<FPendingDeviceRow> Row)
{
    PendingRows.Remove(Row);
    RemovePendingRowWidget(Row);
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnDisconnectDevice(FString DeviceName)
{
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
    {
        Sub->DisconnectDevice(DeviceName);
        Sub->UnregisterDevice(DeviceName);
    }
    RefreshDeviceList();
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnPingDevice(TSharedPtr<FPendingDeviceRow> Row)
{
    if (!Row->NameInput.IsValid() || !Row->IPInput.IsValid()) return FReply::Handled();

    const FString Name = Row->NameInput->GetText().ToString();
    const FString IP   = Row->IPInput->GetText().ToString();
    if (Name.IsEmpty() || IP.IsEmpty()) return FReply::Handled();

    UPCAPToolSubsystem* Sub = GetSubsystem();
    if (!Sub) return FReply::Handled();

    // Register temporarily and connect — result shows up on next refresh
    FHMCDeviceConfig Config;
    Config.DeviceName        = Name;
    Config.IPAddress         = IP;
    Config.WebSocketEndpoint = FString::Printf(TEXT("ws://%s/ws"), *IP);
    Sub->RegisterDevice(Config);
    Sub->ConnectDevice(Name);

    return FReply::Handled();
}

// ── Color helpers ─────────────────────────────────────────────────────────────

FLinearColor SHMCSetupPanel::VoltageColor(float V)
{
    if (V <= 0.f) return FLinearColor(0.4f, 0.4f, 0.4f);
    if (V > 14.f) return FLinearColor(0.137f, 0.467f, 0.220f);
    if (V > 12.f) return FLinearColor(0.800f, 0.600f, 0.0f);
    return FLinearColor(0.800f, 0.133f, 0.133f);
}

FLinearColor SHMCSetupPanel::StorageColor(float MB)
{
    const float GB = MB / 1024.f;
    if (GB > 50.f) return FLinearColor(0.137f, 0.467f, 0.220f);
    if (GB > 10.f) return FLinearColor(0.800f, 0.600f, 0.0f);
    return FLinearColor(0.800f, 0.133f, 0.133f);
}

FLinearColor SHMCSetupPanel::CPUColor(float Pct)
{
    if (Pct < 60.f) return FLinearColor(0.137f, 0.467f, 0.220f);
    if (Pct < 80.f) return FLinearColor(0.800f, 0.600f, 0.0f);
    return FLinearColor(0.800f, 0.133f, 0.133f);
}

FLinearColor SHMCSetupPanel::TempColor(float C)
{
    if (C < 40.f) return FLinearColor(0.137f, 0.467f, 0.220f);
    if (C < 50.f) return FLinearColor(0.800f, 0.600f, 0.0f);
    return FLinearColor(0.800f, 0.133f, 0.133f);
}
