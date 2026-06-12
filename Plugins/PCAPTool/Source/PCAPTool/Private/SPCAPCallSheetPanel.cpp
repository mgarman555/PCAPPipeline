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
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"
#include "PCAPToolTypes.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "PCAPCallSheet"

void SPCAPCallSheetPanel::Construct(const FArguments& InArgs)
{
    // Zero-setup: ensure a database exists in the PCAP tool area on first open.
    if (UPCAPToolSettings* S = UPCAPToolSettings::Get()) { S->GetOrCreateDatabase(); }

    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

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
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ BuildRailItem(ESection::Overview, LOCTEXT("Overview", "Overview")) ]
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
        case ESection::Overview: return BuildOverviewSection();
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

// ── Overview — the day's callout at a glance ────────────────────────────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildOverviewSection()
{
    OverviewThumbnails.Reset();

    UMocapDatabase* DB = GetDB();
    FShootDay* Day = DB ? DB->GetDay(DB->ActiveProductionCode, DB->ActiveDayID) : nullptr;

    if (!DB || DB->ActiveProductionCode.IsEmpty() || !Day)
    {
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(24.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [ SNew(STextBlock).Text(LOCTEXT("OvEmptyHdr", "No shoot day set")).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(LOCTEXT("OvEmpty", "Pick a Project and Shoot day, then call actors and props — they'll show up here.")).ColorAndOpacity(FSlateColor(ColText2)) ]
        ];
    }

    UStageConfigAsset* Stage = DB->GetActiveStageConfig();
    const FString StageName = Stage ? Stage->ConfigName : FString(TEXT("(no stage)"));

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TMap<FString, UActorRosterEntry*> ActorByID;
    {
        TArray<FAssetData> F;
        ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), F, false);
        for (const FAssetData& AD : F) if (UActorRosterEntry* E = Cast<UActorRosterEntry>(AD.GetAsset())) ActorByID.Add(E->ActorID, E);
    }
    TMap<FString, UPropRosterEntry*> PropByID;
    {
        TArray<FAssetData> F;
        ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), F, false);
        for (const FAssetData& AD : F) if (UPropRosterEntry* E = Cast<UPropRosterEntry>(AD.GetAsset())) PropByID.Add(E->PropID, E);
    }

    auto MiniCard = [this](const FString& Name, UObject* Preview) -> TSharedRef<SWidget>
    {
        TSharedRef<SWidget> Thumb =
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(LOCTEXT("Dash", "—")).ColorAndOpacity(FSlateColor(ColText2)) ];
        if (Preview && ThumbnailPool.IsValid())
        {
            TSharedPtr<FAssetThumbnail> T = MakeShared<FAssetThumbnail>(Preview, 56, 56, ThumbnailPool);
            OverviewThumbnails.Add(T);
            Thumb = T->MakeThumbnailWidget();
        }
        return SNew(SBox).WidthOverride(76.f).Padding(4.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [ SNew(SBox).WidthOverride(56.f).HeightOverride(56.f)[ Thumb ] ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FSlateColor(ColText2)).AutoWrapText(true).Justification(ETextJustify::Center) ]
        ];
    };

    TSharedRef<SWrapBox> ActorWrap = SNew(SWrapBox).UseAllottedSize(true);
    for (const FString& ID : Day->CalledActorIDs)
    {
        UActorRosterEntry* E = ActorByID.FindRef(ID);
        UObject* Preview = nullptr;
        if (E) { Preview = E->Headshot.LoadSynchronous(); if (!Preview) Preview = E->MetaHuman.LoadSynchronous(); if (!Preview) Preview = E->FaceScan.LoadSynchronous(); }
        ActorWrap->AddSlot()[ MiniCard(ID, Preview) ];
    }

    TSharedRef<SWrapBox> PropWrap = SNew(SWrapBox).UseAllottedSize(true);
    for (const FString& ID : Day->CalledPropIDs)
    {
        UPropRosterEntry* E = PropByID.FindRef(ID);
        PropWrap->AddSlot()[ MiniCard(ID, E ? E->PropAsset.LoadSynchronous() : nullptr) ];
    }

    const int32 NumActors = Day->CalledActorIDs.Num();
    const int32 NumProps  = Day->CalledPropIDs.Num();

    TArray<FString> Systems;
    if (Stage)
    {
        if (Stage->BodySystem  != EBodySystem::None)  Systems.Add(StaticEnum<EBodySystem>()->GetDisplayNameTextByValue((int64)Stage->BodySystem).ToString());
        if (Stage->FaceSystem  != EFaceSystem::None)  Systems.Add(StaticEnum<EFaceSystem>()->GetDisplayNameTextByValue((int64)Stage->FaceSystem).ToString());
        if (Stage->AudioSystem != EAudioSystem::None) Systems.Add(StaticEnum<EAudioSystem>()->GetDisplayNameTextByValue((int64)Stage->AudioSystem).ToString());
        if (Stage->VCamSystem  != EVCamSystem::None)  Systems.Add(StaticEnum<EVCamSystem>()->GetDisplayNameTextByValue((int64)Stage->VCamSystem).ToString());
    }
    const FString SystemsText = Systems.Num() > 0 ? FString::Join(Systems, TEXT("  ·  ")) : FString(TEXT("no systems set"));

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
          [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   ·   %s   ·   Day_%s"), *DB->ActiveProductionCode, *StageName, *DB->ActiveDayID))).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d actors  ·  %d props  ·  %s"), NumActors, NumProps, *SystemsText))).ColorAndOpacity(FSlateColor(ColText2)) ]
          ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Called actors  (%d)"), NumActors))).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            (NumActors > 0)
            ? StaticCastSharedRef<SWidget>(ActorWrap)
            : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NoActors", "No actors called yet — open Actors to call talent to this day.")).ColorAndOpacity(FSlateColor(ColText2)))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Called props  (%d)"), NumProps))).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            (NumProps > 0)
            ? StaticCastSharedRef<SWidget>(PropWrap)
            : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NoProps", "No props called yet — open Props to call props to this day.")).ColorAndOpacity(FSlateColor(ColText2)))
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
