#include "SPCAPCallSheetPanel.h"
#include "SPCAPActorDatabasePanel.h"
#include "SPCAPPropDatabasePanel.h"
#include "SPCAPStageDatabasePanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"

#define LOCTEXT_NAMESPACE "PCAPCallSheet"

void SPCAPCallSheetPanel::Construct(const FArguments& InArgs)
{
    // Zero-setup: ensure a database exists in the PCAP tool area on first open.
    if (UPCAPToolSettings* S = UPCAPToolSettings::Get()) { S->GetOrCreateDatabase(); }

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
        case ESection::Project:  return BuildProjectSection();
        case ESection::ShootDay:
        default:                 return BuildShootDaySection();
    }
}

UMocapDatabase* SPCAPCallSheetPanel::GetDB() const
{
    UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetDatabase() : nullptr;
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildProjectSection()
{
    UMocapDatabase* DB = GetDB();
    const FString Active = DB ? DB->ActiveProductionCode : FString();

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("PickProject", "Active project")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Active.IsEmpty() ? TEXT("(none)") : Active)) ]
            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(true, nullptr);
                if (UMocapDatabase* D = GetDB())
                {
                    TArray<FProduction> Prods = D->Productions;   // sorted copy — alphabetical
                    Prods.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
                    for (const FProduction& P : Prods)
                    {
                        const FString Code = P.ProjectCode;
                        MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s — %s"), *Code, *P.ProductionName)),
                            FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Code]()
                            {
                                if (UMocapDatabase* D2 = GetDB()) { D2->ActiveProductionCode = Code; }
                                SelectSection(ESection::Project);
                            })));
                    }
                }
                return MB.MakeWidget();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("NewProject", "New project (code)")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SEditableTextBox)
            .HintText(LOCTEXT("NewProjHint", "+ project code  ↵"))
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type CommitType)
            {
                if (CommitType != ETextCommit::OnEnter) return;
                const FString Code = T.ToString().TrimStartAndEnd();
                if (Code.IsEmpty()) return;
                if (UMocapDatabase* D = GetDB())
                {
                    if (!D->GetProductionByCode(Code))
                    {
                        FProduction P; P.ProjectCode = Code; P.ProductionName = Code;
                        D->Productions.Add(P);
                        D->MarkPackageDirty();
                    }
                    D->ActiveProductionCode = Code;
                    SelectSection(ESection::Project);
                }
            })
        ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildShootDaySection()
{
    UMocapDatabase* DB = GetDB();
    const FString Active = DB ? DB->ActiveDayID : FString();

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("PickDay", "Active shoot day")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Active.IsEmpty() ? TEXT("(none)") : (TEXT("Day_") + Active))) ]
            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(true, nullptr);
                if (UMocapDatabase* D = GetDB())
                {
                    if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                    {
                        TArray<FShootDay> Days = P->Days;   // sorted copy — alphabetical
                        Days.Sort([](const FShootDay& A, const FShootDay& B){ return A.DayID < B.DayID; });
                        for (const FShootDay& Day : Days)
                        {
                            const FString Id = Day.DayID;
                            MB.AddMenuEntry(FText::FromString(TEXT("Day_") + Id), FText::GetEmpty(), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda([this, Id]()
                                {
                                    if (UMocapDatabase* D2 = GetDB()) { D2->ActiveDayID = Id; }
                                    SelectSection(ESection::ShootDay);
                                })));
                        }
                    }
                }
                return MB.MakeWidget();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("NewDay", "New shoot day (id, e.g. 002)")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SEditableTextBox)
            .HintText(LOCTEXT("NewDayHint", "+ day id  ↵"))
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type CommitType)
            {
                if (CommitType != ETextCommit::OnEnter) return;
                const FString Id = T.ToString().TrimStartAndEnd();
                if (Id.IsEmpty()) return;
                if (UMocapDatabase* D = GetDB())
                {
                    if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                    {
                        if (!D->GetDay(D->ActiveProductionCode, Id))
                        {
                            FShootDay Day; Day.DayID = Id;
                            P->Days.Add(Day);
                            D->MarkPackageDirty();
                        }
                        D->ActiveDayID = Id;
                    }
                    SelectSection(ESection::ShootDay);
                }
            })
        ];
}

#undef LOCTEXT_NAMESPACE
