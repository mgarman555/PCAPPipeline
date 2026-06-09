#include "SHMCSetupPanel.h"
#include "PCAPToolSubsystem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"

UPCAPToolSubsystem* SHMCSetupPanel::GetSubsystem()
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
}

FHMCDeviceStatus SHMCSetupPanel::GetStatus(const FString& DeviceName) const
{
    UPCAPToolSubsystem* Sub = GetSubsystem();
    return Sub ? Sub->GetDeviceStatus(DeviceName) : FHMCDeviceStatus();
}

void SHMCSetupPanel::Construct(const FArguments& InArgs)
{
    // Starter actor list — these come from the production database later.
    ActorOptions.Add(MakeShared<FString>(TEXT("kevinDorman")));
    ActorOptions.Add(MakeShared<FString>(TEXT("madiGarman")));
    ActorOptions.Add(MakeShared<FString>(TEXT("mannyTester")));

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // ── Header ─────────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 10.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("HMC SETUP")))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
                + SHorizontalBox::Slot().AutoWidth()
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
                    SAssignNew(DeviceListBox, SVerticalBox)
                ]
            ]

            // ── Bottom bar ─────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(12.f, 8.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Assign an actor to each headset, then mark it prepped.")))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Prepped for Preview")))
                        .OnClicked(this, &SHMCSetupPanel::OnPreppedClicked)
                    ]
                ]
            ]
        ]
    ];

    RefreshDeviceList();

    // Auto-connect saved devices (idempotent) so vitals/feed are live immediately.
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
        Sub->ConnectAll();

    RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SHMCSetupPanel::OnRefreshTimer));
}

EActiveTimerReturnType SHMCSetupPanel::OnRefreshTimer(double CurrentTime, float DeltaTime)
{
    RefreshDeviceList();   // cheap — only rebuilds when the device set changes
    return EActiveTimerReturnType::Continue;
}

void SHMCSetupPanel::RefreshDeviceList()
{
    if (!DeviceListBox.IsValid()) return;
    UPCAPToolSubsystem* Sub = GetSubsystem();

    TArray<FString> Names;
    if (Sub)
        for (const FHMCDeviceConfig& C : Sub->GetRegisteredDevices())
            Names.Add(C.DeviceName);
    Names.Sort();

    // Rebuild ONLY when the set changes — rows bind to live status otherwise.
    if (Names == BuiltDeviceNames) return;
    BuiltDeviceNames = Names;

    DeviceListBox->ClearChildren();

    if (Names.Num() == 0)
    {
        DeviceListBox->AddSlot().AutoHeight().Padding(24.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No devices — tap + Add Device.")))
            .ColorAndOpacity(FSlateColor(ColMuted))
        ];
        return;
    }

    for (const FString& Name : Names)
    {
        DeviceListBox->AddSlot().AutoHeight().Padding(FMargin(8.f, 4.f))
        [
            BuildDeviceRow(Name)
        ];
    }
}

TSharedRef<SWidget> SHMCSetupPanel::BuildDeviceRow(const FString& DeviceName)
{
    const FHMCDeviceStatus Snap = GetStatus(DeviceName);

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(FMargin(12.f, 10.f))
        [
            SNew(SVerticalBox)

            // Name → actor dropdown ........ state  [Disconnect]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(DeviceName))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("→")))   // →
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    BuildActorDropdown(DeviceName)
                ]

                + SHorizontalBox::Slot().FillWidth(1.f)

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, DeviceName]()
                    {
                        switch (GetStatus(DeviceName).ConnectionState)
                        {
                            case EHMCConnectionState::Connected: return FText::FromString(TEXT("Connected"));
                            case EHMCConnectionState::Offline:   return FText::FromString(TEXT("Offline"));
                            default:                             return FText::FromString(TEXT("Disconnected"));
                        }
                    })
                    .ColorAndOpacity_Lambda([this, DeviceName]()
                    {
                        switch (GetStatus(DeviceName).ConnectionState)
                        {
                            case EHMCConnectionState::Connected: return FSlateColor(ColGreen);
                            case EHMCConnectionState::Offline:   return FSlateColor(ColRed);
                            default:                             return FSlateColor(ColGray);
                        }
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Disconnect")))
                    .OnClicked(this, &SHMCSetupPanel::OnDisconnectDevice, DeviceName)
                ]
            ]

            // IP below
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 4.f, 0.f, 0.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Snap.IPAddress))
                .ColorAndOpacity(FSlateColor(ColMuted))
            ]
        ];
}

TSharedRef<SWidget> SHMCSetupPanel::BuildActorDropdown(const FString& DeviceName)
{
    return SNew(SComboButton)
        .ButtonContent()
        [
            SNew(STextBlock)
            .Text_Lambda([this, DeviceName]()
            {
                const FString A = GetStatus(DeviceName).ActorName;
                return FText::FromString(A.IsEmpty() ? TEXT("Assign actor…") : A);
            })
        ]
        .OnGetMenuContent_Lambda([this, DeviceName]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
            for (const TSharedPtr<FString>& Opt : ActorOptions)
            {
                if (!Opt.IsValid()) continue;
                const FString ActorName = *Opt;
                MB.AddMenuEntry(
                    FText::FromString(ActorName),
                    FText::GetEmpty(),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateSP(
                        const_cast<SHMCSetupPanel*>(this), &SHMCSetupPanel::OnActorChosen, DeviceName, ActorName)));
            }
            return MB.MakeWidget();
        });
}

void SHMCSetupPanel::OnActorChosen(FString DeviceName, FString ActorName)
{
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
        Sub->AssignActor(DeviceName, ActorName);
}

// ── Add Device modal ────────────────────────────────────────────────────────

FReply SHMCSetupPanel::OnAddDeviceClicked()
{
    ModalWindow = SNew(SWindow)
        .Title(FText::FromString(TEXT("Add HMC Device")))
        .ClientSize(FVector2D(380.f, 230.f))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(16.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Name"))) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
                [ SAssignNew(ModalNameInput, SEditableTextBox).HintText(FText::FromString(TEXT("ORION"))) ]

                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("IP Address"))) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
                [ SAssignNew(ModalIPInput, SEditableTextBox).HintText(FText::FromString(TEXT("192.168.50.117"))) ]

                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Name and IP address are saved to the database upon completion.")))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                    .AutoWrapText(true)
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(SButton)
                        .HAlign(HAlign_Center)
                        .Text(FText::FromString(TEXT("Connect")))
                        .OnClicked(this, &SHMCSetupPanel::OnModalConnectClicked)
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("X")))
                        .OnClicked(this, &SHMCSetupPanel::OnModalCancelClicked)
                    ]
                ]
            ]
        ];

    FSlateApplication::Get().AddModalWindow(ModalWindow.ToSharedRef(), AsShared());
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnModalConnectClicked()
{
    const FString Name = ModalNameInput.IsValid() ? ModalNameInput->GetText().ToString().TrimStartAndEnd() : FString();
    const FString IP   = ModalIPInput.IsValid()   ? ModalIPInput->GetText().ToString().TrimStartAndEnd()   : FString();

    if (!Name.IsEmpty() && !IP.IsEmpty())
    {
        if (UPCAPToolSubsystem* Sub = GetSubsystem())
        {
            FHMCDeviceConfig Config;
            Config.DeviceName = Name;
            Config.IPAddress  = IP;
            // ActorName + WebSocketEndpoint intentionally empty.
            Sub->RegisterDevice(Config);   // persists to HMCConfig.json
        }
        RefreshDeviceList();
    }

    if (ModalWindow.IsValid())
    {
        FSlateApplication::Get().RequestDestroyWindow(ModalWindow.ToSharedRef());
        ModalWindow.Reset();
    }
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnModalCancelClicked()
{
    if (ModalWindow.IsValid())
    {
        FSlateApplication::Get().RequestDestroyWindow(ModalWindow.ToSharedRef());
        ModalWindow.Reset();
    }
    return FReply::Handled();
}

// ── Bottom actions ──────────────────────────────────────────────────────────

FReply SHMCSetupPanel::OnPreppedClicked()
{
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
    {
        Sub->ConnectAll();
        Sub->SaveConfig();
    }
    return FReply::Handled();
}

FReply SHMCSetupPanel::OnDisconnectDevice(FString DeviceName)
{
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
        Sub->DisconnectDevice(DeviceName);
    return FReply::Handled();
}

// ── Colour helpers (used by the Increment-2 control/vital panel) ────────────

FLinearColor SHMCSetupPanel::VoltageColor(float V)
{
    if (V <= 0.f)   return FLinearColor(0.4f, 0.4f, 0.4f);        // no reading
    if (V > 14.0f)  return FLinearColor(0.137f, 0.467f, 0.220f);  // green  > 14.0V
    if (V > 13.6f)  return FLinearColor(0.800f, 0.600f, 0.0f);    // yellow 13.6–14.0V
    return FLinearColor(0.800f, 0.133f, 0.133f);                  // red    <= 13.6V
}

FLinearColor SHMCSetupPanel::StorageColor(float MB)
{
    const float GB = MB / 1024.f;
    if (GB > 100.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  > 100 GB
    if (GB >= 50.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 50–100 GB
    return FLinearColor(0.800f, 0.133f, 0.133f);                 // red    < 50 GB
}

FLinearColor SHMCSetupPanel::CPUColor(float Pct)
{
    if (Pct < 60.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  < 60%
    if (Pct < 85.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 60–85%
    return FLinearColor(0.800f, 0.133f, 0.133f);                 // red    >= 85%
}

FLinearColor SHMCSetupPanel::TempColor(float C)
{
    if (C < 70.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  0–69°C
    if (C < 85.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 70–84°C
    return FLinearColor(0.800f, 0.133f, 0.133f);               // red    >= 85°C
}
