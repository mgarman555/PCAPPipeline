#include "SPCAPToolPanel.h"
#include "SHMCSetupPanel.h"
#include "SHMCPreviewPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

void SPCAPToolPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // ── Top bar ────────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(8.f, 6.f))
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(0, 0, 4, 0)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("HMC SETUP")))
                        .OnClicked(this, &SPCAPToolPanel::OnSetupClicked)
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("HMC PREVIEW")))
                        .OnClicked(this, &SPCAPToolPanel::OnPreviewClicked)
                    ]
                ]
            ]

            // ── Content area ───────────────────────────────────────────────
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SAssignNew(ModeSwitcher, SWidgetSwitcher)
                .WidgetIndex(1)   // open in Preview by default

                // Slot 0 — Setup
                + SWidgetSwitcher::Slot()
                [
                    SNew(SHMCSetupPanel)
                ]

                // Slot 1 — Preview
                + SWidgetSwitcher::Slot()
                [
                    SNew(SHMCPreviewPanel)
                ]
            ]
        ]
    ];
}

FReply SPCAPToolPanel::OnSetupClicked()
{
    ActiveMode = 0;
    if (ModeSwitcher.IsValid()) ModeSwitcher->SetActiveWidgetIndex(0);
    return FReply::Handled();
}

FReply SPCAPToolPanel::OnPreviewClicked()
{
    ActiveMode = 1;
    if (ModeSwitcher.IsValid()) ModeSwitcher->SetActiveWidgetIndex(1);
    return FReply::Handled();
}
