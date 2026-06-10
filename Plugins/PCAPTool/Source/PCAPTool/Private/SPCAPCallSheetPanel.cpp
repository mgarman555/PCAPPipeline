#include "SPCAPCallSheetPanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PCAPCallSheet"

void SPCAPCallSheetPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            // Header
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SNew(STextBlock).Text(LOCTEXT("Title", "CALL SHEET")).ColorAndOpacity(FSlateColor(ColGreen)) ]
            ]

            // Body — rail + content
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(168.f)
                    [
                        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(6.f))
                        [ SAssignNew(RailBox, SBox) ]
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(6.f, 0.f, 0.f, 0.f))
                [ SAssignNew(ContentBox, SBox) ]
            ]
        ]
    ];

    SelectSection(Current);
}

void SPCAPCallSheetPanel::SelectSection(ESection S)
{
    Current = S;
    if (RailBox.IsValid())    RailBox->SetContent(BuildRail());
    if (ContentBox.IsValid()) ContentBox->SetContent(BuildSectionContent(S));
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildRail()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 2.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("Context", "Context")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Project,  LOCTEXT("Project",  "Project")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Stages,   LOCTEXT("Stages",   "Stages")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::ShootDay, LOCTEXT("ShootDay", "Shoot day")) ]
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 10.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("CalledOut", "Called out")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Actors, LOCTEXT("Actors", "Actors")) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Props,  LOCTEXT("Props",  "Props")) ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildRailItem(ESection S, const FText& Label)
{
    const bool bActive = (S == Current);
    return SNew(SButton)
        .ButtonStyle(FAppStyle::Get(), "NoBorder")
        .ContentPadding(FMargin(8.f, 6.f))
        .HAlign(HAlign_Left)
        .OnClicked_Lambda([this, S]() { SelectSection(S); return FReply::Handled(); })
        [
            SNew(STextBlock)
            .Text(Label)
            .ColorAndOpacity(FSlateColor(bActive ? ColGreen : FLinearColor::White))
        ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildSectionContent(ESection S)
{
    switch (S)
    {
        case ESection::Stages: return SNew(SPCAPStageDatabasePanel);
        case ESection::Actors: return SNew(SPCAPActorDatabasePanel);
        case ESection::Props:  return SNew(SPCAPPropDatabasePanel);
        case ESection::Project:
            return SNew(STextBlock).Text(LOCTEXT("ProjStub", "Project picker — Task 4")).ColorAndOpacity(FSlateColor(ColText2));
        case ESection::ShootDay:
        default:
            return SNew(STextBlock).Text(LOCTEXT("DayStub", "Shoot day picker — Task 4")).ColorAndOpacity(FSlateColor(ColText2));
    }
}

#undef LOCTEXT_NAMESPACE
