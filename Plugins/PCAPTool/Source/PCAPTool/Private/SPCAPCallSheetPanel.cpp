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

#include "PCAPSlateCsv.h"
#include "PCAPToolStatics.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
        + SVerticalBox::Slot().AutoHeight()[ BuildRailItem(ESection::Shots,    LOCTEXT("Shots",    "Shots")) ]
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
        case ESection::Shots:  return BuildShotsSection();
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

    TArray<FString> ReadyIssues;
    const bool bReady = DB->GetActiveDayReadiness(ReadyIssues);
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FText ReadyText = bReady
        ? LOCTEXT("Ready", "Ready to shoot")
        : FText::FromString(FString::Printf(TEXT("Not ready — missing: %s"), *FString::Join(ReadyIssues, TEXT(", "))));

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 8.f))
          [ SNew(STextBlock).Text(ReadyText).ColorAndOpacity(FSlateColor(bReady ? ColGreen : ColAmber)) ]
        ]

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

// ── Shots — the day's slate list (CSV import/export, adopted from Epic's Slates) ─────

FString SPCAPCallSheetPanel::NormalizeSlot(const FString& Slot)
{
    const FString Trim = Slot.TrimStartAndEnd();
    if (Trim.IsEmpty()) return Trim;
    // Slot-only 3-digit ShotID convention: pad pure-digit slots ("3" -> "003", "901" stays).
    // Only digits are padded — IsNumeric() would also accept "3.5"/"-1" and Atoi would mangle them.
    bool bAllDigits = true;
    for (const TCHAR C : Trim) { if (!FChar::IsDigit(C)) { bAllDigits = false; break; } }
    if (bAllDigits) return FString::Printf(TEXT("%03d"), FCString::Atoi(*Trim));
    return Trim;
}

EShotType SPCAPCallSheetPanel::ShotTypeFor(const FString& TypeText, const FString& Slot)
{
    const FString T = TypeText.TrimStartAndEnd().ToLower();
    if (T.Contains(TEXT("cal")))      return EShotType::Calibration;
    if (T.Contains(TEXT("test")))     return EShotType::TestShot;
    if (T.Contains(TEXT("retarget"))) return EShotType::Retargeting;
    if (!T.IsEmpty())                 return EShotType::Production;
    // No explicit type — infer from the reserved slots (901/902/903).
    if (Slot == TEXT("901")) return EShotType::Calibration;
    if (Slot == TEXT("902")) return EShotType::TestShot;
    if (Slot == TEXT("903")) return EShotType::Retargeting;
    return EShotType::Production;
}

FString SPCAPCallSheetPanel::ShotTypeName(EShotType Type)
{
    switch (Type)
    {
        case EShotType::Calibration: return TEXT("Calibration");
        case EShotType::TestShot:    return TEXT("Test");
        case EShotType::Retargeting: return TEXT("Retargeting");
        default:                     return TEXT("Production");
    }
}

FSession* SPCAPCallSheetPanel::EnsureActiveSession(bool bCreate)
{
    UMocapDatabase* DB = GetDB();
    if (!DB) return nullptr;
    FShootDay* Day = DB->GetDay(DB->ActiveProductionCode, DB->ActiveDayID);
    if (!Day) return nullptr;

    if (Day->Sessions.Num() == 0)
    {
        if (!bCreate) return nullptr;
        FSession S; S.SessionID = TEXT("S01"); S.Label = TEXT("Main");
        Day->Sessions.Add(S);
        DB->MarkPackageDirty();
    }
    if (DB->ActiveSessionID.IsEmpty())
        DB->ActiveSessionID = Day->Sessions[0].SessionID;

    for (FSession& S : Day->Sessions)
        if (S.SessionID == DB->ActiveSessionID) return &S;
    return &Day->Sessions[0];
}

void SPCAPCallSheetPanel::AddShotBySlot(const FString& Slot)
{
    const FString ShotID = NormalizeSlot(Slot);
    if (ShotID.IsEmpty()) return;
    FSession* Sess = EnsureActiveSession(true);
    if (!Sess) return;
    for (const FShot& S : Sess->Shots) if (S.ShotID == ShotID) return;   // no duplicates
    FShot New;
    New.ShotID   = ShotID;
    New.ShotType = ShotTypeFor(FString(), ShotID);
    Sess->Shots.Add(New);
    if (UMocapDatabase* DB = GetDB()) DB->MarkPackageDirty();
    SelectSection(ESection::Shots);
}

FReply SPCAPCallSheetPanel::OnRemoveShot(FString ShotID)
{
    if (FSession* Sess = EnsureActiveSession(false))
    {
        Sess->Shots.RemoveAll([&ShotID](const FShot& S){ return S.ShotID == ShotID; });
        if (UMocapDatabase* DB = GetDB()) DB->MarkPackageDirty();
        SelectSection(ESection::Shots);
    }
    return FReply::Handled();
}

void SPCAPCallSheetPanel::ApplyRowsToActiveDay(const TArray<FSlateCsvRow>& Rows)
{
    UMocapDatabase* DB = GetDB();
    FSession* Sess = EnsureActiveSession(true);
    if (!DB || !Sess) return;

    // Resolve the roster once (id -> entry) so imported actors/props carry their stream defaults.
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

    for (const FSlateCsvRow& R : Rows)
    {
        const FString ShotID = NormalizeSlot(R.Slot);
        if (ShotID.IsEmpty()) continue;

        FShot* Existing = Sess->Shots.FindByPredicate([&ShotID](const FShot& S){ return S.ShotID == ShotID; });
        FShot NewShot;
        FShot& T = Existing ? *Existing : NewShot;   // mutate in place (existing) or build fresh

        T.ShotID      = ShotID;
        T.ShotType    = ShotTypeFor(R.Type, ShotID);
        T.Description = R.Description;
        T.Notes       = R.Notes;

        // Overwrite the planning fields from the CSV; existing shots keep their recorded Takes.
        T.Subjects.Reset();
        for (const FString& A : R.Actors)
        {
            FShotSubject Subj;
            if (UActorRosterEntry* E = ActorByID.FindRef(A)) Subj = UPCAPToolStatics::MakeShotSubjectFromRoster(E);
            else                                             Subj.ActorID = A;
            Subj.bIsActive = true;
            T.Subjects.Add(Subj);
        }
        T.Props.Reset();
        for (const FString& P : R.Props)
        {
            FPropEntry Pr;
            if (UPropRosterEntry* E = PropByID.FindRef(P)) { Pr.PropID = E->PropID; Pr.bIsTracked = E->bIsTracked; Pr.LiveLinkSubjectName = E->DefaultLiveLinkName; }
            else                                           { Pr.PropID = P; }
            T.Props.Add(Pr);
        }

        if (!Existing) Sess->Shots.Add(MoveTemp(NewShot));
    }
    DB->MarkPackageDirty();
}

FSlateCsvRow SPCAPCallSheetPanel::RowFromShot(const FShot& Shot) const
{
    FSlateCsvRow R;
    R.Slot        = Shot.ShotID;
    R.Type        = ShotTypeName(Shot.ShotType);
    R.Description = Shot.Description;
    for (const FShotSubject& S : Shot.Subjects) R.Actors.Add(S.ActorID);
    for (const FPropEntry& P : Shot.Props)      R.Props.Add(P.PropID);
    R.Notes       = Shot.Notes;
    return R;
}

void SPCAPCallSheetPanel::ImportShotsCsv()
{
    IDesktopPlatform* DP = FDesktopPlatformModule::Get();
    if (!DP) return;
    const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    TArray<FString> Files;
    const bool bPicked = DP->OpenFileDialog(
        Parent, TEXT("Import shot list (CSV)"), FPaths::ProjectContentDir(), TEXT(""),
        TEXT("CSV files (*.csv)|*.csv|All files (*.*)|*.*"), EFileDialogFlags::None, Files);
    if (!bPicked || Files.Num() == 0) return;

    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Files[0]))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Shot CSV import: could not read %s"), *Files[0]);
        return;
    }
    TArray<FSlateCsvRow> Rows; FString Err;
    if (!FPCAPSlateCsv::Parse(Text, Rows, Err))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Shot CSV import failed: %s"), *Err);
        return;
    }
    ApplyRowsToActiveDay(Rows);
    UE_LOG(LogTemp, Log, TEXT("[PCAP] Imported %d shot(s) from %s"), Rows.Num(), *Files[0]);
    SelectSection(ESection::Shots);
}

void SPCAPCallSheetPanel::ExportShotsCsv()
{
    UMocapDatabase* DB = GetDB();
    FSession* Sess = EnsureActiveSession(false);
    if (!DB || !Sess) return;

    TArray<FSlateCsvRow> Rows;
    for (const FShot& S : Sess->Shots) Rows.Add(RowFromShot(S));
    const FString Csv = FPCAPSlateCsv::Format(Rows);

    IDesktopPlatform* DP = FDesktopPlatformModule::Get();
    if (!DP) return;
    const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    const FString DefaultName = FString::Printf(TEXT("shots_%s_Day_%s.csv"), *DB->ActiveProductionCode, *DB->ActiveDayID);
    TArray<FString> Out;
    const bool bPicked = DP->SaveFileDialog(
        Parent, TEXT("Export shot list (CSV)"), FPaths::ProjectContentDir(), DefaultName,
        TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, Out);
    if (!bPicked || Out.Num() == 0) return;

    FString Path = Out[0];
    if (!Path.EndsWith(TEXT(".csv"))) Path += TEXT(".csv");
    FFileHelper::SaveStringToFile(Csv, *Path);
    UE_LOG(LogTemp, Log, TEXT("[PCAP] Exported %d shot(s) to %s"), Rows.Num(), *Path);
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildShotsSection()
{
    UMocapDatabase* DB = GetDB();
    if (!DB || DB->ActiveProductionCode.IsEmpty() || DB->ActiveDayID.IsEmpty())
    {
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(24.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ShotsNoDay", "Pick a Project and Shoot day first — then build the day's shot list here."))
            .ColorAndOpacity(FSlateColor(ColText2))
        ];
    }

    // Read-only here: viewing the Shots tab must not mutate the DB. Add/Import create
    // the session on demand.
    FSession* Sess = EnsureActiveSession(/*bCreate=*/false);

    TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("ImportCsv", "Import CSV")).ToolTipText(LOCTEXT("ImportCsvTip", "Bulk-create the day's shots from a CSV (slot,type,description,actors,props,notes)")).OnClicked_Lambda([this](){ ImportShotsCsv(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("ExportCsv", "Export CSV")).ToolTipText(LOCTEXT("ExportCsvTip", "Write this day's shot list to a CSV")).OnClicked_Lambda([this](){ ExportShotsCsv(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
        [
            SNew(SEditableTextBox)
            .HintText(LOCTEXT("AddShotHint", "+ shot slot (e.g. 004)  ↵"))
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type C)
            {
                if (C == ETextCommit::OnEnter) { const FString S = T.ToString().TrimStartAndEnd(); if (!S.IsEmpty()) AddShotBySlot(S); }
            })
        ];

    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    if (Sess && Sess->Shots.Num() > 0)
    {
        TArray<FShot> Sorted = Sess->Shots;   // sorted copy — alphabetical by slot
        Sorted.Sort([](const FShot& A, const FShot& B){ return A.ShotID < B.ShotID; });
        for (const FShot& S : Sorted)
        {
            const FString ShotID = S.ShotID;
            const FString Desc   = S.Description;
            const FString Meta   = FString::Printf(TEXT("%s  ·  %d actors  ·  %d props"),
                *ShotTypeName(S.ShotType), S.Subjects.Num(), S.Props.Num());
            List->AddSlot().AutoHeight().Padding(0.f, 2.f)
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f, 6.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
                    [ SNew(STextBlock).Text(FText::FromString(ShotID)).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString(Desc)) ]
                        + SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString(Meta)).ColorAndOpacity(FSlateColor(ColText2)) ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton).Text(FText::FromString(TEXT("✕"))).ToolTipText(LOCTEXT("RemoveShot", "Remove this shot")).OnClicked(this, &SPCAPCallSheetPanel::OnRemoveShot, ShotID) ]
                ]
            ];
        }
    }
    else
    {
        List->AddSlot().AutoHeight().Padding(12.f, 8.f)
        [ SNew(STextBlock).Text(LOCTEXT("NoShots", "No shots yet — add a slot above, or Import CSV to build the whole day's shot list at once.")).ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    const FString SessLabel = Sess ? FString::Printf(TEXT("Session %s"), *Sess->SessionID) : FString(TEXT("(no session)"));

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Shot list — Day_%s · %s"), *DB->ActiveDayID, *SessLabel))).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Toolbar ]
        + SVerticalBox::Slot().FillHeight(1.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ List ] ];
}

#undef LOCTEXT_NAMESPACE
