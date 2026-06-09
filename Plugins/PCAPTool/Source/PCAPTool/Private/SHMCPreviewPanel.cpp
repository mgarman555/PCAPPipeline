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

            // Legend
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(8.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                [
                    SNew(SBorder)
                    .Padding(FMargin(10.f, 10.f))
                    .BorderBackgroundColor(FLinearColor(0.137f, 0.467f, 0.220f))
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Clear"))).ColorAndOpacity(FSlateColor(ColMuted)) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                [
                    SNew(SBorder)
                    .Padding(FMargin(10.f, 10.f))
                    .BorderBackgroundColor(FLinearColor(0.800f, 0.133f, 0.133f))
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Issue / Disconnected"))).ColorAndOpacity(FSlateColor(ColMuted)) ]
            ]
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

void SHMCPreviewPanel::RefreshCards()
{
    if (!CardContainer.IsValid()) return;
    UPCAPToolSubsystem* Sub = GetSubsystem();

    CardContainer->ClearChildren();
    // Old cards are gone — safe to release last cycle's frame brushes.
    FeedBrushes.Reset();

    if (!Sub || Sub->GetAllDeviceStatuses().Num() == 0)
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

    TSharedRef<SWrapBox> WrapBox = SNew(SWrapBox)
        .UseAllottedSize(true)
        .InnerSlotPadding(FVector2D(8.f, 8.f));

    for (const FHMCDeviceStatus& Status : Sub->GetAllDeviceStatuses())
    {
        WrapBox->AddSlot()
        .ForceNewLine(false)
        [
            SNew(SBox)
            .WidthOverride(440.f)
            [
                BuildDeviceCard(Status)
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

TSharedRef<SWidget> SHMCPreviewPanel::BuildDeviceCard(const FHMCDeviceStatus& Status)
{
    const bool bConnected  = Status.ConnectionState == EHMCConnectionState::Connected;
    const bool bRecording  = Status.bIsRecording;
    const bool bOffline    = Status.ConnectionState != EHMCConnectionState::Connected;

    const FLinearColor StripColor = CardStatusColor(Status);

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(0)
        .ColorAndOpacity(bOffline ? FLinearColor(1,1,1,0.4f) : FLinearColor::White)
        [
            SNew(SVerticalBox)

            // ── Status strip ──────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .Padding(FMargin(0.f, 4.f))
                .BorderBackgroundColor(StripColor)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
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
                        SNew(STextBlock)
                        .Text(FText::FromString(Status.DeviceName))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Status.ActorName.IsEmpty()
                            ? TEXT("No actor assigned") : Status.ActorName))
                        .ColorAndOpacity(FSlateColor(ColMuted))
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Status.IPAddress))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
            ]

            // ── Feed placeholders ─────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0,0,4,0)
                [ BuildFeed(Status, 0, TEXT("TOP")) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ BuildFeed(Status, 1, TEXT("BOT")) ]
            ]

            // ── Vitals bar ────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [ BuildVitalBar(Status) ]

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
                    .BorderBackgroundColor(bRecording
                        ? FLinearColor(0.8f, 0.1f, 0.1f)
                        : FLinearColor(0.25f, 0.25f, 0.25f))
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(bRecording ? TEXT("RECORDING") : TEXT("Standby")))
                    .ColorAndOpacity(FSlateColor(bRecording ? ColRed : ColMuted))
                ]

                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FormatSecondsSince(Status.LastUpdateTime)))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
            ]

            // ── Status quip ───────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(12.f, 4.f, 12.f, 10.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(bOffline
                    ? TEXT("OFFLINE")
                    : (Status.StatusMessage.IsEmpty()
                        ? TEXT("Waiting for data...")
                        : Status.StatusMessage)))
                .ColorAndOpacity(FSlateColor(bOffline ? ColRed : ColMuted))
            ]
        ];
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildVitalBar(const FHMCDeviceStatus& Status)
{
    auto VitalCell = [this](const FString& Label, const FString& Value, const FLinearColor& Color)
        -> TSharedRef<SWidget>
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
                    SNew(STextBlock).Text(FText::FromString(Value))
                    .ColorAndOpacity(FSlateColor(Color))
                ]
            ];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("BAT"), FString::Printf(TEXT("%.1fV"), Status.BatteryVoltage),
            VoltageColor(Status.BatteryVoltage)) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("SPC"), FormatStorage(Status.AvailableStorageMB),
            StorageColor(Status.AvailableStorageMB)) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("CPU"), FString::Printf(TEXT("%.0f%%"), Status.CPUUsagePercent),
            CPUColor(Status.CPUUsagePercent)) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("TEMP"), FString::Printf(TEXT("%.0f°C"), Status.TemperatureCelsius),
            TempColor(Status.TemperatureCelsius)) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("CLIP"),
            Status.LastClipStatus.IsEmpty() ? TEXT("—") : Status.LastClipStatus,
            Status.LastClipStatus == TEXT("Ready") ? ColGreen : ColYellow) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ VitalCell(TEXT("DROP"),
            FString::Printf(TEXT("%d"), Status.DroppedFrames0 + Status.DroppedFrames1),
            (Status.DroppedFrames0 + Status.DroppedFrames1) == 0 ? ColGreen : ColYellow) ];
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildFeedPlaceholder(const FString& Label)
{
    return SNew(SBorder)
        .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f))
        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
        .Padding(0)
        [
            SNew(SBox)
            .HeightOverride(120.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("No Feed")))
                    .ColorAndOpacity(FSlateColor(ColGray))
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,4)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .ColorAndOpacity(FSlateColor(ColMuted))
                ]
            ]
        ];
}

TSharedRef<SWidget> SHMCPreviewPanel::BuildFeed(const FHMCDeviceStatus& Status, int32 CameraIndex, const FString& Label)
{
    UPCAPToolSubsystem* Sub = GetSubsystem();
    UTexture2D* Frame = Sub ? Sub->GetLastFrame(Status.DeviceName, CameraIndex) : nullptr;
    const bool bConnected = Status.ConnectionState == EHMCConnectionState::Connected;

    // Size the cell to the camera's display aspect so the whole frame shows without
    // crop or distortion. Prefer the decoded frame's true pixels; fall back to
    // control.json geometry (rotation-aware: a 2048x1536 sensor at 90° = portrait).
    float AspectWH = 0.75f;   // ORION rotated-portrait default
    if (Frame && Frame->GetSizeY() > 0)
    {
        AspectWH = (float)Frame->GetSizeX() / (float)Frame->GetSizeY();
    }
    else if (Status.FrameWidth > 0 && Status.FrameHeight > 0)
    {
        const int32 Rot   = (CameraIndex == 0) ? Status.Rotation0 : Status.Rotation1;
        const bool  bSwap = (((Rot % 180) + 180) % 180) == 90;   // 90/270 → portrait
        const float DW = bSwap ? (float)Status.FrameHeight : (float)Status.FrameWidth;
        const float DH = bSwap ? (float)Status.FrameWidth  : (float)Status.FrameHeight;
        if (DH > 0.f) AspectWH = DW / DH;
    }
    // Feed occupies ~half the 440px card; derive height from the camera aspect.
    const float FeedH = FMath::Clamp(212.f / FMath::Max(0.2f, AspectWH), 110.f, 380.f);

    // Issue-driven border + banner (hardware ∪ manual flags → severity → color/text).
    const int32 Flags = Sub ? Sub->GetEffectiveIssueFlags(Status.DeviceName, CameraIndex) : 0;
    const EHMCIssueSeverity Sev = UPCAPToolStatics::GetIssueSeverity(Flags);
    const FLinearColor BorderCol =
        !bConnected                       ? ColGray :
        (Sev == EHMCIssueSeverity::Red)   ? ColRed :
        (Sev == EHMCIssueSeverity::Amber) ? ColYellow : ColGreen;
    const FString Banner = bConnected ? UPCAPToolStatics::GetIssueBannerText(Flags) : TEXT("OFFLINE");

    // Inner: live frame (scaled to fit, aspect preserved) or "No Feed".
    TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
    if (Frame)
    {
        FSlateBrush B;
        B.SetResourceObject(Frame);
        B.ImageSize = FVector2D(Frame->GetSizeX(), Frame->GetSizeY());
        B.DrawAs    = ESlateBrushDrawType::Image;

        TSharedRef<FDeferredCleanupSlateBrush> Brush = FDeferredCleanupSlateBrush::CreateBrush(B);
        FeedBrushes.Add(Brush);

        Inner = SNew(SScaleBox)
            .Stretch(EStretch::ScaleToFit)
            [
                SNew(SImage).Image(Brush->GetSlateBrush())
            ];
    }
    else
    {
        Inner = SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(bConnected ? TEXT("No Feed") : TEXT("—")))
                .ColorAndOpacity(FSlateColor(ColGray))
            ];
    }

    return SNew(SBorder)                              // colored issue border
        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
        .BorderBackgroundColor(BorderCol)
        .Padding(2.f)
        [
            SNew(SBox).HeightOverride(FeedH)
            [
                SNew(SBorder)                         // black feed background
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(BgFeed)
                .Padding(0.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot() [ Inner ]

                    // Issue banner across the top
                    + SOverlay::Slot()
                    .VAlign(VAlign_Top)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(BorderCol.R, BorderCol.G, BorderCol.B, 0.65f))
                        .Padding(FMargin(4.f, 2.f))
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(Banner))
                            .ColorAndOpacity(FSlateColor(ColWhite))
                        ]
                    ]

                    // View label, bottom-right
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
