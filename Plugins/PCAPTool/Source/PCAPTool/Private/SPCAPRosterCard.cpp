#include "SPCAPRosterCard.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PCAPRosterCard"

void SPCAPRosterCard::Construct(const FArguments& InArgs)
{
    OnClicked = InArgs._OnClicked;
    const FLinearColor Dim(0.478f, 0.541f, 0.502f);

    // Thumbnail, or an accent dot when the entry has no preview asset (e.g. a vcam).
    TSharedRef<SWidget> Thumb = InArgs._Thumbnail.IsValid()
        ? InArgs._Thumbnail.ToSharedRef()
        : StaticCastSharedRef<SWidget>(
            SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("●")))
                .ColorAndOpacity(FSlateColor(InArgs._Accent)) ]);

    TSharedRef<SWidget> CalledRow = InArgs._bShowCalled
        ? StaticCastSharedRef<SWidget>(
            SNew(SCheckBox)
            .IsChecked(InArgs._bCalled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged(InArgs._OnCalledChanged)
            [ SNew(STextBlock).Text(LOCTEXT("Called", "called")).ColorAndOpacity(FSlateColor(Dim)) ])
        : SNullWidget::NullWidget;

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(FMargin(6.f))
        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
        {
            OnClicked.ExecuteIfBound();
            return FReply::Handled();
        })
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SBox).HeightOverride(84.f)[ Thumb ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(InArgs._Title).Justification(ETextJustify::Center) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(InArgs._Subtitle).ColorAndOpacity(FSlateColor(Dim))
                .Justification(ETextJustify::Center).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
            [ CalledRow ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
