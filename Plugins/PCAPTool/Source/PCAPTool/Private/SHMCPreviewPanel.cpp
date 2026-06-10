#include "SHMCPreviewPanel.h"
#include "PCAPToolSubsystem.h"
#include "PCAPToolStatics.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateBrush.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Engine/Texture2D.h"

UPCAPToolSubsystem* SHMCPreviewPanel::GetSubsystem()
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
}

void SHMCPreviewPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SVerticalBox)

        // ── Header ────────────────────────────────────────────────────────
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.f, 10.f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("HMC PREVIEW")))
                .ColorAndOpacity(FSlateColor(ColMuted))
            ]
            // Legend removed — the green/red feed bar is self-explanatory.
        ]

        // ── Cards ─────────────────────────────────────────────────────────
        + SVerticalBox::Slot()
        .FillHeight(1.f)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SAssignNew(CardContainer, SVerticalBox)
            ]
        ]
    ];

    // Auto-connect saved devices so Preview shows live data when opened on its own
    // (without first visiting Setup). Idempotent — already-polling devices skip.
    if (UPCAPToolSubsystem* Sub = GetSubsystem())
        Sub->ConnectAll();

    RefreshCards();
    // Structure + vitals refresh (cards rebuilt) — modest rate is fine for telemetry.
    RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SHMCPreviewPanel::OnRefreshTimer));
    // Fast repaint (~30fps) so the in-place-updated feed textures show new frames.
    // Returning Continue also keeps the editor un-throttled → faster frame pulls.
    RegisterActiveTimer(1.0f / 30.0f, FWidgetActiveTimerDelegate::CreateSP(
        this, &SHMCPreviewPanel::OnFastRepaint));
}

EActiveTimerReturnType SHMCPreviewPanel::OnRefreshTimer(double CurrentTime, float DeltaTime)
{
    RefreshCards();
    return EActiveTimerReturnType::Continue;
}

EActiveTimerReturnType SHMCPreviewPanel::OnFastRepaint(double CurrentTime, float DeltaTime)
{
    // Feed textures are updated in place by the subsystem; just force a repaint.
    Invalidate(EInvalidateWidgetReason::Paint);
    return EActiveTimerReturnType::Continue;
}

FHMCDeviceStatus SHMCPreviewPanel::GetStatus(const FString& DeviceName) const
{
    UPCAPToolSubsystem* Sub = GetSubsystem();
    return Sub ? Sub->GetDeviceStatus(DeviceName) : FHMCDeviceStatus();
}

void SHMCPreviewPanel::RefreshCards()
{
    if (!CardContainer.IsValid()) return;
    UPCAPToolSubsystem* Sub = GetSubsystem();

    // Current device set (sorted for a stable comparison).
    TArray<FString> Names;
    if (Sub)
        for (const FHMCDeviceStatus& S : Sub->GetAllDeviceStatuses())
            Names.Add(S.DeviceName);
    Names.Sort();

    // Rebuild ONLY when the set changes. Otherwise the cards update themselves via
    // their bound lambdas + the 30fps repaint — tearing them down is what blinked.
    if (Names == BuiltDeviceNames) return;
    BuiltDeviceNames = Names;

    CardContainer->ClearChildren();

    if (Names.Num() == 0)
    {
        CardContainer->AddSlot()
        .AutoHeight()
        .Padding(24.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No devices registered. Go to HMC SETUP to add devices.")))
            .ColorAndOpacity(FSlateColor(ColMuted))
        ];
        return;
    }

    // Card width scales with headcount so the grid densifies as the stage fills up:
    // a couple of HMCs get large, detailed cards; a full shoot gets a compact grid
    // that fits everyone with minimal scrolling. SWrapBox still wraps to the panel
    // width, so narrower cards simply mean more columns per row.
    const int32 DeviceCount = Names.Num();
    const float CardW =
        DeviceCount >= 9 ? 300.f :   // full stage → dense grid
        DeviceCount >= 5 ? 350.f :
        DeviceCount >= 3 ? 400.f :
                           440.f;    // 1–2 performers → large cards

    TSharedRef<SWrapBox> WrapBox = SNew(SWrapBox)
        .UseAllottedSize(true)
        .InnerSlotPadding(FVector2D(8.f, 8.f));

    for (const FString& Name : Names)
    {
        WrapBox->AddSlot()
        .ForceNewLine(false)
        [
            SNew(SBox)
            .WidthOverride(CardW)
            [
                BuildDeviceCard(Name)
            ]
        ];
    }

    CardContainer->AddSlot()
    .AutoHeight()
    .Padding(8.f)
    [
        WrapBox
    ];
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildDeviceCard(const FString& DeviceName)
{
    const FHMCDeviceStatus Snapshot = GetStatus(DeviceName);   // for static-only values

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(0)
        .ColorAndOpacity_Lambda([this, DeviceName]()
        {
            const bool bOff = GetStatus(DeviceName).ConnectionState != EHMCConnectionState::Connected;
            return bOff ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : FLinearColor::White;
        })
        [
            SNew(SVerticalBox)

            // ── Status strip ──────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .Padding(FMargin(0.f, 4.f))
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this, DeviceName]()
                {
                    return FSlateColor(CardStatusColor(GetStatus(DeviceName)));
                })
            ]

            // ── Device header ─────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 8.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).Text(FText::FromString(DeviceName))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .ColorAndOpacity(FSlateColor(ColMuted))
                        .Text_Lambda([this, DeviceName]()
                        {
                            const FString A = GetStatus(DeviceName).ActorID;
                            return FText::FromString(A.IsEmpty() ? TEXT("No actor assigned") : A);
                        })
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Snapshot.IPAddress))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
            ]

            // ── Feeds ─────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0,0,4,0)
                [ BuildFeed(DeviceName, 0, TEXT("TOP")) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ BuildFeed(DeviceName, 1, TEXT("BOT")) ]
            ]

            // ── Vitals bar ────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [ BuildVitalBar(DeviceName) ]

            // ── Recording indicator ───────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                [
                    SNew(SBorder)
                    .Padding(FMargin(6.f))
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor_Lambda([this, DeviceName]()
                    {
                        return FSlateColor(GetStatus(DeviceName).bIsRecording
                            ? FLinearColor(0.8f, 0.1f, 0.1f) : FLinearColor(0.25f, 0.25f, 0.25f));
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, DeviceName]()
                    {
                        return FText::FromString(GetStatus(DeviceName).bIsRecording ? TEXT("RECORDING") : TEXT("Standby"));
                    })
                    .ColorAndOpacity_Lambda([this, DeviceName]()
                    {
                        return FSlateColor(GetStatus(DeviceName).bIsRecording ? ColRed : ColMuted);
                    })
                ]

                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .ColorAndOpacity(FSlateColor(ColMuted))
                    .Text_Lambda([this, DeviceName]()
                    {
                        return FText::FromString(FormatSecondsSince(GetStatus(DeviceName).LastUpdateTime));
                    })
                ]
            ]

            // ── Status quip ───────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 4.f, 12.f, 10.f)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                // The error moved out of the camera frame to here: red while any issue is
                // active (matching the red feed border), muted status message otherwise.
                .Text_Lambda([this, DeviceName]()
                {
                    const FString Err = DeviceErrorText(DeviceName);
                    if (!Err.IsEmpty())
                        return FText::FromString(Err);
                    if (FramingRefMissing(DeviceName))
                        return FText::FromString(TEXT("Framing reference not set — set it in Setup"));
                    const FHMCDeviceStatus S = GetStatus(DeviceName);
                    return FText::FromString(S.StatusMessage.IsEmpty() ? TEXT("Waiting for data...") : S.StatusMessage);
                })
                .ColorAndOpacity_Lambda([this, DeviceName]()
                {
                    return FSlateColor(DeviceErrorText(DeviceName).IsEmpty() ? ColMuted : ColRed);
                })
            ]
        ];
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildVitalBar(const FString& DeviceName)
{
    // Each cell binds its value + colour to lambdas so it updates live (no rebuild).
    auto VitalCell = [this](const FString& Label, TFunction<FString()> ValueFn,
        TFunction<FLinearColor()> ColorFn) -> TSharedRef<SWidget>
    {
        return SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(8.f, 4.f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(FText::FromString(Label))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text_Lambda([ValueFn]() { return FText::FromString(ValueFn()); })
                    .ColorAndOpacity_Lambda([ColorFn]() { return FSlateColor(ColorFn()); })
                ]
            ];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("BAT"),
            [this, DeviceName]() { return FString::Printf(TEXT("%.1fV"), GetStatus(DeviceName).BatteryVoltage); },
            [this, DeviceName]() { return VoltageColor(GetStatus(DeviceName).BatteryVoltage); }) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("SPC"),
            [this, DeviceName]() { return FormatStorage(GetStatus(DeviceName).AvailableStorageMB); },
            [this, DeviceName]() { return StorageColor(GetStatus(DeviceName).AvailableStorageMB); }) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("CPU"),
            [this, DeviceName]() { return FString::Printf(TEXT("%.0f%%"), GetStatus(DeviceName).CPUUsagePercent); },
            [this, DeviceName]() { return CPUColor(GetStatus(DeviceName).CPUUsagePercent); }) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("TEMP"),
            [this, DeviceName]() { return FString::Printf(TEXT("%.0f°C"), GetStatus(DeviceName).TemperatureCelsius); },
            [this, DeviceName]() { return TempColor(GetStatus(DeviceName).TemperatureCelsius); }) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("CLIP"),
            [this, DeviceName]() { const FString C = GetStatus(DeviceName).LastClipStatus; return C.IsEmpty() ? FString(TEXT("—")) : C; },
            [this, DeviceName]() { return GetStatus(DeviceName).LastClipStatus == TEXT("Ready") ? ColGreen : ColYellow; }) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("DROP"),
            [this, DeviceName]() { const FHMCDeviceStatus S = GetStatus(DeviceName); return FString::Printf(TEXT("%d"), S.DroppedFrames0 + S.DroppedFrames1); },
            [this, DeviceName]() { const FHMCDeviceStatus S = GetStatus(DeviceName); return (S.DroppedFrames0 + S.DroppedFrames1) == 0 ? ColGreen : ColYellow; }) ];
}

FLinearColor SHMCPreviewPanel::FeedBorderColor(const FString& DeviceName, int32 CameraIndex) const
{
    // Binary by design: GREEN = good to shoot, RED = look here (any issue, amber
    // severity included, or disconnected). No amber/grey bar — the bar is meant to
    // read at a glance with no legend.
    if (GetStatus(DeviceName).ConnectionState != EHMCConnectionState::Connected)
        return ColRed;
    UPCAPToolSubsystem* Sub = GetSubsystem();
    const int32 Flags = Sub ? Sub->GetEffectiveIssueFlags(DeviceName, CameraIndex) : 0;
    return UPCAPToolStatics::GetIssueSeverity(Flags) == EHMCIssueSeverity::None
        ? ColGreen : ColRed;
}

FString SHMCPreviewPanel::DeviceErrorText(const FString& DeviceName) const
{
    // Aggregated, human-readable error for the card's status line. Empty => all clear.
    // Rendered in red (see the status-line lambdas) so it matches the red feed border,
    // and it persists until the underlying issue clears because it reads live status.
    if (GetStatus(DeviceName).ConnectionState != EHMCConnectionState::Connected)
        return TEXT("OFFLINE");

    UPCAPToolSubsystem* Sub = GetSubsystem();
    if (!Sub) return FString();

    // Per-camera issue text, but only when that camera's severity is actually an issue
    // (GetIssueBannerText is total — it returns "All clear" for 0 flags — so gate on severity).
    auto CamErr = [Sub, &DeviceName](int32 Cam) -> FString
    {
        const int32 Flags = Sub->GetEffectiveIssueFlags(DeviceName, Cam);
        return UPCAPToolStatics::GetIssueSeverity(Flags) == EHMCIssueSeverity::None
            ? FString()
            : UPCAPToolStatics::GetIssueBannerText(Flags);
    };

    const FString Top = CamErr(0);
    const FString Bot = CamErr(1);
    FString Out;
    if (!Top.IsEmpty()) Out = FString::Printf(TEXT("TOP: %s"), *Top);
    if (!Bot.IsEmpty())
    {
        if (!Out.IsEmpty()) Out += TEXT("     ");
        Out += FString::Printf(TEXT("BOT: %s"), *Bot);
    }
    return Out;
}

bool SHMCPreviewPanel::FramingRefMissing(const FString& DeviceName) const
{
    UPCAPToolSubsystem* Sub = GetSubsystem();
    if (!Sub) return false;
    if (GetStatus(DeviceName).ConnectionState != EHMCConnectionState::Connected) return false;
    // Inactive only while NEITHER camera has a reference — once one is set, drift is checked.
    return !Sub->GetFramingRef(DeviceName, 0).bSet && !Sub->GetFramingRef(DeviceName, 1).bSet;
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildFeed(const FString& DeviceName, int32 CameraIndex, const FString& Label)
{
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);

    // Feed height from the camera's display aspect (rotation-aware). ORION's rotated
    // 2048x1536 reads as portrait 0.75; default covers the pre-first-poll case.
    const FHMCDeviceStatus Snap = GetStatus(DeviceName);
    float AspectWH = 0.75f;
    if (Snap.FrameWidth > 0 && Snap.FrameHeight > 0)
    {
        const int32 Rot   = (CameraIndex == 0) ? Snap.Rotation0 : Snap.Rotation1;
        const bool  bSwap = (((Rot % 180) + 180) % 180) == 90;
        const float DW = bSwap ? (float)Snap.FrameHeight : (float)Snap.FrameWidth;
        const float DH = bSwap ? (float)Snap.FrameWidth  : (float)Snap.FrameHeight;
        if (DH > 0.f) AspectWH = DW / DH;
    }
    const float FeedH = FMath::Clamp(212.f / FMath::Max(0.2f, AspectWH), 110.f, 380.f);

    // Persistent image: a brush (held in FeedBrushPersist) repointed to the latest
    // stable texture each paint. The texture is reused in place, so this never blinks.
    TSharedRef<SImage> Img = SNew(SImage)
        .Image_Lambda([this, DeviceName, CameraIndex, Key]() -> const FSlateBrush*
        {
            TSharedPtr<FSlateBrush>& B = FeedBrushPersist.FindOrAdd(Key);
            if (!B.IsValid()) B = MakeShared<FSlateBrush>();
            UPCAPToolSubsystem* Sub = GetSubsystem();
            UTexture2D* Tex = Sub ? Sub->GetLastFrame(DeviceName, CameraIndex) : nullptr;
            if (Tex)
            {
                B->SetResourceObject(Tex);
                B->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
                B->DrawAs    = ESlateBrushDrawType::Image;
            }
            else
            {
                B->SetResourceObject(nullptr);
            }
            return B.Get();
        });

    return SNew(SBorder)                               // colored issue border (live)
        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
        .Padding(2.f)
        .BorderBackgroundColor_Lambda([this, DeviceName, CameraIndex]()
        {
            return FSlateColor(FeedBorderColor(DeviceName, CameraIndex));
        })
        [
            SNew(SBox).HeightOverride(FeedH)
            [
                SNew(SBorder)                          // black feed background
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(BgFeed)
                .Padding(0.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SNew(SScaleBox).Stretch(EStretch::ScaleToFit) [ Img ]
                    ]

                    // "No Feed" / "—" only while there is no frame
                    + SOverlay::Slot()
                    .VAlign(VAlign_Center).HAlign(HAlign_Center)
                    [
                        SNew(STextBlock)
                        .ColorAndOpacity(FSlateColor(ColGray))
                        .Visibility_Lambda([this, DeviceName, CameraIndex]()
                        {
                            UPCAPToolSubsystem* Sub = GetSubsystem();
                            const bool bHas = Sub && Sub->GetLastFrame(DeviceName, CameraIndex) != nullptr;
                            return bHas ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                        })
                        .Text_Lambda([this, DeviceName]()
                        {
                            const bool bConn = GetStatus(DeviceName).ConnectionState == EHMCConnectionState::Connected;
                            return FText::FromString(bConn ? TEXT("No Feed") : TEXT("—"));
                        })
                    ]

                    // (Issue banner removed — error text now lives in the card's status
                    //  line below the vitals, in red, matching this feed's red border.)

                    // View label, bottom-right (static)
                    + SOverlay::Slot()
                    .VAlign(VAlign_Bottom).HAlign(HAlign_Right)
                    .Padding(FMargin(0.f, 0.f, 4.f, 2.f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Label))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                ]
            ]
        ];
}

// ── Helpers ───────────────────────────────────────────────────────────────────

FLinearColor SHMCPreviewPanel::CardStatusColor(const FHMCDeviceStatus& Status)
{
    if (Status.ConnectionState != EHMCConnectionState::Connected)
        return FLinearColor(0.333f, 0.333f, 0.333f);
    if (Status.bIsRecording)
        return FLinearColor(0.800f, 0.133f, 0.133f);
    return FLinearColor(0.137f, 0.467f, 0.220f);
}

FLinearColor SHMCPreviewPanel::VoltageColor(float V)
{
    if (V <= 0.f)   return FLinearColor(0.4f, 0.4f, 0.4f);        // no reading
    if (V > 14.0f)  return FLinearColor(0.137f, 0.467f, 0.220f);  // green  > 14.0V
    if (V > 13.6f)  return FLinearColor(0.800f, 0.600f, 0.0f);    // yellow 13.6–14.0V
    return FLinearColor(0.800f, 0.133f, 0.133f);                  // red    <= 13.6V
}

FLinearColor SHMCPreviewPanel::StorageColor(float MB)
{
    const float GB = MB / 1024.f;
    if (GB > 100.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  > 100 GB
    if (GB >= 50.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 50–100 GB
    return FLinearColor(0.800f, 0.133f, 0.133f);                 // red    < 50 GB
}

FLinearColor SHMCPreviewPanel::CPUColor(float Pct)
{
    if (Pct < 60.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  < 60%
    if (Pct < 85.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 60–85%
    return FLinearColor(0.800f, 0.133f, 0.133f);                 // red    >= 85%
}

FLinearColor SHMCPreviewPanel::TempColor(float C)
{
    if (C < 70.f) return FLinearColor(0.137f, 0.467f, 0.220f); // green  0–69°C
    if (C < 85.f) return FLinearColor(0.800f, 0.600f, 0.0f);   // yellow 70–84°C
    return FLinearColor(0.800f, 0.133f, 0.133f);               // red    >= 85°C
}

FString SHMCPreviewPanel::FormatStorage(float MB)
{
    const float GB = MB / 1024.f;
    if (GB < 1.f)   return FString::Printf(TEXT("%.0fMB"), MB);
    if (GB < 100.f) return FString::Printf(TEXT("%.1fGB"), GB);
    return FString::Printf(TEXT("%.0fGB"), GB);   // compact for large drives (642GB)
}

FString SHMCPreviewPanel::FormatSecondsSince(const FDateTime& T)
{
    if (T.GetTicks() == 0) return TEXT("Never");
    const int32 Secs = static_cast<int32>(
        (FDateTime::UtcNow() - T).GetTotalSeconds());
    return Secs < 60
        ? FString::Printf(TEXT("Updated %ds ago"), Secs)
        : FString::Printf(TEXT("Updated %dm ago"), Secs / 60);
}
