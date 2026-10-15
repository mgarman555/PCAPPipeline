#include "SPCAPCallSheetPanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"
#include "VCamConfig.h"
#include "PCAPToolTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "PCAPToolPaths.h"
#include "FileHelpers.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

#include "PCAPSlateCsv.h"
#include "PCAPToolStatics.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "PCAPMocapData.h"
#include "PCAPPerformerExtension.h"
#include "HMCRigEntry.h"
#include "PCAPStringPick.h"

#include "PCAPVolumeVisualizer.h"   // spawn target — header only, never edited here
#include "Editor.h"                 // GEditor
#include "Engine/World.h"           // UWorld::SpawnActor

#define LOCTEXT_NAMESPACE "PCAPCallSheet"

void SPCAPCallSheetPanel::Construct(const FArguments& InArgs)
{
    if (UPCAPToolSettings* S = UPCAPToolSettings::Get()) { S->GetOrCreateDatabase(); }

    {
        FPropertyEditorModule& PEM = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        FDetailsViewArgs Args;
        Args.bAllowSearch = false;
        Args.bHideSelectionTip = true;
        Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
        StageDetailsView = PEM.CreateDetailView(Args);
    }

    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SNew(STextBlock).Text(LOCTEXT("Title", "CALL SHEET")).ColorAndOpacity(FSlateColor(ColGreen)) ]
            ]
            + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
            [ SAssignNew(SheetBox, SBox) ]
        ]
    ];

    RebuildSheet();
}

namespace
{
    // Latest take time across a day (0 if nothing recorded) — drives the "last shot" label.
    FDateTime PCAPDayLastShot(const FShootDay& Day)
    {
        FDateTime Latest(0);
        for (const FSession& S : Day.Sessions)
            for (const FShot& Sh : S.Shots)
                for (const FTake& T : Sh.Takes)
                    if (T.RecordedAt > Latest) Latest = T.RecordedAt;
        return Latest;
    }
    FString PCAPDayShotSuffix(const FShootDay& Day)
    {
        const FDateTime L = PCAPDayLastShot(Day);
        return (L == FDateTime(0)) ? FString(TEXT("not shot yet"))
                                   : FString::Printf(TEXT("last shot %s"), *L.ToString(TEXT("%Y-%m-%d")));
    }

    // The Epic performer asset (Actor Database) named Id, if any.
    TOptional<FPCAPPerformerInfo> CallSheetFindPerformer(const FString& Id)
    {
        for (const FPCAPPerformerInfo& P : UPCAPMocapData::GetAllPerformers())
            if (P.PerformerName.ToString() == Id) return P;
        return {};
    }

    UPCAPPerformerExtension* CallSheetFindExtension(const FString& Id)
    {
        const TOptional<FPCAPPerformerInfo> P = CallSheetFindPerformer(Id);
        return P.IsSet() ? UPCAPMocapData::FindPerformerExtension(P->AssetUID) : nullptr;
    }

    // HMC rigs (helmets) from the HMC Database, by display name.
    TMap<FString, FSoftObjectPath> CallSheetHelmets()
    {
        TMap<FString, FSoftObjectPath> Out;
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> F;
        ARM.Get().GetAssetsByClass(UHMCRigEntry::StaticClass()->GetClassPathName(), F, false);
        for (const FAssetData& AD : F)
            if (const UHMCRigEntry* R = Cast<UHMCRigEntry>(AD.GetAsset()))
                Out.Add(R->RigName.IsEmpty() ? AD.AssetName.ToString() : R->RigName, AD.GetSoftObjectPath());
        return Out;
    }

    FString CallSheetHelmetName(const TSoftObjectPtr<UHMCRigEntry>& Helmet)
    {
        if (Helmet.IsNull()) return FString();
        if (const UHMCRigEntry* R = Helmet.LoadSynchronous())
            if (!R->RigName.IsEmpty()) return R->RigName;
        return Helmet.GetAssetName();
    }
}

UMocapDatabase* SPCAPCallSheetPanel::GetDB() const
{
    UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetDatabase() : nullptr;
}

UObject* SPCAPCallSheetPanel::CreateAssetIn(UClass* Class, const FString& Dir, const FString& Id, TFunction<void(UObject*)> Init)
{
    if (Id.IsEmpty() || !Class) return nullptr;
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *Dir, *Id);
    const FString ObjectPath  = FString::Printf(TEXT("%s.%s"), *PackageName, *Id);
    if (FPackageName::DoesPackageExist(PackageName))
        return StaticLoadObject(Class, nullptr, *ObjectPath);   // already in the library — resolve & return it
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UObject* Obj = NewObject<UObject>(Package, Class, FName(*Id), RF_Public | RF_Standalone | RF_Transactional);
    if (Init) Init(Obj);                                        // set type fields BEFORE the save so they persist
    FAssetRegistryModule::AssetCreated(Obj);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Obj;
}

TSharedRef<SWidget> SPCAPCallSheetPanel::MakeAddPopup(const TArray<FText>& Hints, TFunction<void(const TArray<FString>&)> OnCreate)
{
    // A plain "+" (no down-arrow) that opens a small popup window anchored at the button.
    // The popup is a compact panel of one field per hint + a Create button — not a dropdown
    // list, and it doesn't shift the header row.
    return SNew(SComboButton)
        .HasDownArrow(false)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(TEXT("+"))) ]
        .OnGetMenuContent_Lambda([Hints, OnCreate]() -> TSharedRef<SWidget>
        {
            TSharedRef<TArray<TSharedPtr<SEditableTextBox>>> Boxes = MakeShared<TArray<TSharedPtr<SEditableTextBox>>>();

            auto Commit = [Boxes, OnCreate]()
            {
                TArray<FString> Vals;
                for (const TSharedPtr<SEditableTextBox>& B : *Boxes)
                    Vals.Add(B.IsValid() ? B->GetText().ToString().TrimStartAndEnd() : FString());
                FSlateApplication::Get().DismissAllMenus();   // close the popup before the sheet rebuilds
                OnCreate(Vals);
            };

            TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
            for (int32 i = 0; i < Hints.Num(); ++i)
            {
                TSharedRef<SEditableTextBox> B = SNew(SEditableTextBox)
                    .HintText(Hints[i])
                    .MinDesiredWidth(170.f)
                    .OnTextCommitted_Lambda([Commit](const FText&, ETextCommit::Type C){ if (C == ETextCommit::OnEnter) Commit(); });
                Boxes->Add(B);
                Fields->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[ B ];
            }

            // Focus the first field the instant the popup appears.
            if (Boxes->Num() > 0)
            {
                (*Boxes)[0]->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
                    [BoxWeak = TWeakPtr<SEditableTextBox>((*Boxes)[0])](double, float)
                    {
                        if (TSharedPtr<SEditableTextBox> B = BoxWeak.Pin())
                            FSlateApplication::Get().SetKeyboardFocus(B, EFocusCause::SetDirectly);
                        return EActiveTimerReturnType::Stop;
                    }));
            }

            Fields->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
                [ SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("CreateEntry", "Create"))
                  .OnClicked_Lambda([Commit]() { Commit(); return FReply::Handled(); }) ];

            return SNew(SBox).Padding(8.f).MinDesiredWidth(190.f)[ Fields ];
        });
}

void SPCAPCallSheetPanel::RebuildSheet()
{
    SaveDB();   // persist any pending master-DB edits (productions/days/calls) before re-rendering
    if (SheetBox.IsValid()) SheetBox->SetContent(BuildSheet());
}

void SPCAPCallSheetPanel::SaveDB()
{
    // Productions / days / sessions / call-outs live in the master DB asset. Unlike the
    // roster libraries (separate assets saved on create), DB edits only MarkPackageDirty,
    // so without this they'd never reach disk. Save when dirty so setup actually persists.
    UMocapDatabase* D = GetDB();
    if (!D) return;
    UPackage* Pkg = D->GetPackage();
    if (Pkg && Pkg->IsDirty())
        FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
}

void SPCAPCallSheetPanel::SaveDayConfiguration()
{
    // Persist everything the day depends on: the master DB (production/day/sessions/calls/
    // flags/shots) and the called stage's config asset (the volume setup, edited inline —
    // that one isn't covered by the auto-save in RebuildSheet).
    TArray<UPackage*> Pkgs;
    if (UMocapDatabase* D = GetDB())
    {
        if (UPackage* P = D->GetPackage()) Pkgs.Add(P);
        if (UStageConfigAsset* S = D->GetActiveStageConfig())
            if (UPackage* P = S->GetPackage()) Pkgs.AddUnique(P);
    }
    if (Pkgs.Num() > 0)
        FEditorFileUtils::PromptForCheckoutAndSave(Pkgs, /*bCheckDirty*/ false, /*bPromptToSave*/ false);

    FNotificationInfo Info(LOCTEXT("DaySaved", "Day configuration saved."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildSaveBar()
{
    return SNew(SBox).HAlign(HAlign_Right).Padding(0.f, 4.f, 0.f, 0.f)
    [
        SNew(SButton)
        .Text(LOCTEXT("SaveDay", "Save day configuration"))
        .ToolTipText(LOCTEXT("SaveDayTip", "Save the master database + the called stage's setup for this shoot day"))
        .OnClicked_Lambda([this]() { SaveDayConfiguration(); return FReply::Handled(); })
    ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildSheet()
{
    UMocapDatabase* DB = GetDB();
    const bool bHasDay = DB && !DB->ActiveProductionCode.IsEmpty()
        && DB->GetDay(DB->ActiveProductionCode, DB->ActiveDayID) != nullptr;

    if (!bHasDay)
    {
        return SNew(SScrollBox)
            + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildHeader() ]
            + SScrollBox::Slot()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(24.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [ SNew(STextBlock).AutoWrapText(true)
                    .Text(LOCTEXT("PickFirst", "Pick a production and shoot day above to start calling out the day."))
                    .ColorAndOpacity(FSlateColor(ColText2)) ]
            ];
    }

    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildHeader() ]            // Production
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildStageArea() ]         // Stage Setup
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)
        [ BuildCallSection(LOCTEXT("CalledActors", "Called actors"), GatherActors(),
            [this](const FString& Id){ UMocapDatabase* D = GetDB(); return D && D->IsActorCalled(Id); },
            [this](const FString& Id, bool b)
            {
                UMocapDatabase* D = GetDB();
                if (!D) return;
                D->SetActorCalled(Id, b);
                // A newly called actor gets their own neutral-face take for the day.
                if (b)
                    if (FShootDay* Dy = D->GetActiveDay())
                        UPCAPToolStatics::AddCalibrationShots(*Dy, UPCAPToolStatics::ResolveCalibrationNeeds(*Dy, D->GetActiveStageConfig()));
                D->MarkPackageDirty();
            }) ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildCaptureSetupSection() ]   // per performer, per helmet
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)
        [ BuildCallSection(LOCTEXT("CalledProps", "Called props"), GatherProps(),
            [this](const FString& Id){ UMocapDatabase* D = GetDB(); return D && D->IsPropCalled(Id); },
            [this](const FString& Id, bool b){ if (UMocapDatabase* D = GetDB()) { D->SetPropCalled(Id, b); D->MarkPackageDirty(); } }) ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildCallToggles() ]        // Call?
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildShotsSection() ]        // Shot imports
        + SScrollBox::Slot()[ BuildSaveBar() ];                                         // Save day
}

// ── Header — production / day / stage pickers + readiness + spawn-viz ─────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildHeader()
{
    UMocapDatabase* DB = GetDB();
    const FString Prod = DB ? DB->ActiveProductionCode : FString();
    const FString Day  = DB ? DB->ActiveDayID : FString();

    TArray<FString> Issues;
    const bool bReady = DB && DB->GetActiveDayReadiness(Issues);
    const FLinearColor ColAmber(0.878f, 0.627f, 0.188f);
    const FText ReadyText = bReady
        ? LOCTEXT("Ready", "Ready to shoot")
        : FText::FromString(Issues.Num() > 0 ? FString::Printf(TEXT("Not ready — %s"), *FString::Join(Issues, TEXT(", "))) : FString(TEXT("Not ready")));

    // Day button shows the day id + its shot status (derived from recorded takes).
    FString DayLabel = TEXT("(pick day)");
    if (!Day.IsEmpty())
    {
        FString Suffix = TEXT("not shot yet");
        if (DB) if (FShootDay* Dy = DB->GetActiveDay()) Suffix = PCAPDayShotSuffix(*Dy);
        DayLabel = FString::Printf(TEXT("%s · %s"), *Day, *Suffix);
    }

    // Production button shows "Name - Number" for the active production.
    FString ProdLabel = TEXT("(pick production)");
    if (DB)
        if (FProduction* P = DB->GetProductionByCode(Prod))
            ProdLabel = FString::Printf(TEXT("%s - %s"), *P->ProductionName, *P->ProjectCode);

    TSharedRef<SComboButton> ProdPick = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(ProdLabel)).ColorAndOpacity(FSlateColor(ColGreen)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* D = GetDB())
            {
                TArray<FProduction> P = D->Productions;
                P.Sort([](const FProduction& A, const FProduction& B){ return A.ProductionName < B.ProductionName; });
                for (const FProduction& Pr : P)
                {
                    const FString C = Pr.ProjectCode;
                    MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s - %s"), *Pr.ProductionName, *Pr.ProjectCode)), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, C]() { if (UMocapDatabase* D2 = GetDB()) { D2->ActiveProductionCode = C; } RebuildSheet(); })));
                }
            }
            return MB.MakeWidget();
        });

    TSharedRef<SComboButton> DayPick = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(DayLabel)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* D = GetDB())
                if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                {
                    TArray<FShootDay> Days = P->Days;
                    Days.Sort([](const FShootDay& A, const FShootDay& B){ return A.DayID < B.DayID; });
                    for (const FShootDay& Dy : Days)
                    {
                        const FString Id = Dy.DayID;
                        MB.AddMenuEntry(FText::FromString(Id), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Id]() { if (UMocapDatabase* D2 = GetDB()) { D2->ActiveDayID = Id; } RebuildSheet(); })));
                    }
                }
            return MB.MakeWidget();
        });

    auto Dot = [this]() { return SNew(STextBlock).Text(FText::FromString(TEXT("·"))).ColorAndOpacity(FSlateColor(ColText2)); };

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("SecProduction", "Production")).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ProdPick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
            [ MakeAddPopup({ LOCTEXT("ProdNameHint", "name"), LOCTEXT("ProdNumHint", "number") },
              [this](const TArray<FString>& V)
              {
                  const FString Name = V.IsValidIndex(0) ? V[0] : FString();
                  const FString Num  = V.IsValidIndex(1) ? V[1] : FString();
                  if (!Name.IsEmpty() && !Num.IsEmpty())
                      if (UMocapDatabase* D = GetDB())
                      {
                          if (!D->GetProductionByCode(Num)) { FProduction P; P.ProjectCode = Num; P.ProductionName = Name; D->Productions.Add(P); D->MarkPackageDirty(); }
                          D->ActiveProductionCode = Num;
                      }
                  RebuildSheet();
              }) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)[ Dot() ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ DayPick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
            [ MakeAddPopup({ LOCTEXT("DayIdHint", "day id") },
              [this](const TArray<FString>& V)
              {
                  const FString Id = V.IsValidIndex(0) ? V[0] : FString();
                  if (!Id.IsEmpty())
                      if (UMocapDatabase* D = GetDB())
                          if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                          {
                              if (!D->GetDay(D->ActiveProductionCode, Id))
                              {
                                  FShootDay Dy; Dy.DayID = Id; Dy.CalendarDate = FDateTime::Today();
                                  // New days start with the calibration shots (audio / facial / body),
                                  // from the production's stage when it has one.
                                  const UStageConfigAsset* Stage = P->ActiveStageConfig.LoadSynchronous();
                                  UPCAPToolStatics::AddCalibrationShots(Dy, UPCAPToolStatics::ResolveCalibrationNeeds(Dy, Stage));
                                  P->Days.Add(Dy);
                                  D->MarkPackageDirty();
                              }
                              D->ActiveDayID = Id;
                          }
                  RebuildSheet();
              }) ]
            + SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(ReadyText).ColorAndOpacity(FSlateColor(bReady ? ColGreen : ColAmber)) ]
        ]
    ];
}

// ── Stage area — the called stage's setup (dropdown is in the header) ─────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildStageArea()
{
    UMocapDatabase* DB = GetDB();
    UStageConfigAsset* Stage = DB ? DB->GetActiveStageConfig() : nullptr;
    if (StageDetailsView.IsValid()) StageDetailsView->SetObject(Stage);
    const FString StageName = Stage ? (Stage->ConfigName.IsEmpty() ? Stage->GetName() : Stage->ConfigName) : FString(TEXT("(pick stage)"));

    // Read-only summary of the called stage's configured systems.
    TArray<FString> Systems;
    if (Stage)
    {
        if (!Stage->MocapSystem.IsEmpty())     Systems.Add(Stage->MocapSystem);
        if (!Stage->FacialSystem.IsEmpty())    Systems.Add(PCAPStringPick::FacialModeLabel(Stage->FacialSystem, Stage->bFacialMono, Stage->bFacialStereo));
        if (!Stage->AudioSystemName.IsEmpty()) Systems.Add(Stage->AudioSystemName);
        if (Stage->VCamSystem  != EVCamSystem::None)  Systems.Add(StaticEnum<EVCamSystem>()->GetDisplayNameTextByValue((int64)Stage->VCamSystem).ToString());
    }
    const FString SystemsText = Systems.Num() > 0 ? FString::Join(Systems, TEXT("  ·  ")) : FString(TEXT("no systems set"));

    // Stage picker — pick an existing stage (new stages are made in the Stage Database).
    TSharedRef<SComboButton> StagePick = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(StageName)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
            TArray<FAssetData> F;
            ARM.Get().GetAssetsByClass(UStageConfigAsset::StaticClass()->GetClassPathName(), F, false);
            TArray<UStageConfigAsset*> St;
            for (const FAssetData& AD : F) if (UStageConfigAsset* S = Cast<UStageConfigAsset>(AD.GetAsset())) St.Add(S);
            St.Sort([](const UStageConfigAsset& A, const UStageConfigAsset& B){ return A.ConfigName < B.ConfigName; });
            for (UStageConfigAsset* S : St)
            {
                TWeakObjectPtr<UStageConfigAsset> W(S);
                const FString L = S->ConfigName.IsEmpty() ? S->GetName() : S->ConfigName;
                MB.AddMenuEntry(FText::FromString(L), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, W]()
                    {
                        UMocapDatabase* D = GetDB();
                        FShootDay* Dy = D ? D->GetDay(D->ActiveProductionCode, D->ActiveDayID) : nullptr;
                        if (Dy && W.IsValid()) { Dy->ActiveStageConfig = W.Get(); D->MarkPackageDirty(); }
                        RebuildSheet();
                    })));
            }
            return MB.MakeWidget();
        });

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("SecStage", "Stage Setup")).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ StagePick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(FText::FromString(SystemsText)).ColorAndOpacity(FSlateColor(ColText2)) ]
            + SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SButton)
              .Text(LOCTEXT("SpawnViz", "Spawn volume visualizer"))
              .ToolTipText(LOCTEXT("SpawnVizTip", "Drop a Volume Visualizer into the level for this day's stage"))
              .IsEnabled(Stage != nullptr)
              .OnClicked_Lambda([this]() { SpawnVolumeVisualizer(); return FReply::Handled(); }) ]
        ];

    if (Stage)
        Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[ StageDetailsView.ToSharedRef() ];

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [ Body ];
}

// ── HMC day flag — a single day-level "HMCs used today?" switch ───────────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildHMCDayToggle()
{
    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(FMargin(12.f, 8.f))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]()
                {
                    UMocapDatabase* D = GetDB();
                    return (D && D->IsHMCDay()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                {
                    if (UMocapDatabase* D = GetDB())
                    {
                        D->SetHMCDay(S == ECheckBoxState::Checked);
                        D->MarkPackageDirty();
                    }
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock).Text(LOCTEXT("HMCsUsedToday", "HMCs used today?")) ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("HMCsUsedHint", "Mark the day for head-mounted facial capture. The HMC tool calls the actors checked below."))
                .ColorAndOpacity(FSlateColor(ColText2)) ]
        ];
}

// ── Call? — day-level HMC / VCam / Audio "used today?" toggles ────────────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildCallToggles()
{
    auto Toggle = [this](const FText& Label, bool FShootDay::* Flag) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SCheckBox)
              .IsChecked_Lambda([this, Flag]()
              {
                  UMocapDatabase* D = GetDB();
                  FShootDay* Dy = D ? D->GetActiveDay() : nullptr;
                  return (Dy && Dy->*Flag) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
              })
              .OnCheckStateChanged_Lambda([this, Flag](ECheckBoxState S)
              {
                  UMocapDatabase* D = GetDB();
                  if (FShootDay* Dy = D ? D->GetActiveDay() : nullptr)
                  {
                      Dy->*Flag = (S == ECheckBoxState::Checked);
                      D->MarkPackageDirty();
                      RebuildSheet();
                  }
              }) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 20.f, 0.f)
            [ SNew(STextBlock).Text(Label) ];
    };

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("SecCall", "Call?")).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ Toggle(LOCTEXT("CallHMC", "HMC"), &FShootDay::bHMCsUsed) ]
            + SHorizontalBox::Slot().AutoWidth()[ Toggle(LOCTEXT("CallVCam", "VCam"), &FShootDay::bVCamUsed) ]
            + SHorizontalBox::Slot().AutoWidth()[ Toggle(LOCTEXT("CallAudio", "Audio"), &FShootDay::bAudioUsed) ]
        ]
    ];
}

// ── Called section — chips of called items + a "+ call" checklist picker ──────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildCallSection(const FText& Title,
    const TArray<TPair<FString, FString>>& Items,
    TFunction<bool(const FString&)> IsCalled,
    TFunction<void(const FString&, bool)> SetCalled)
{
    TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true);
    int32 Count = 0;
    for (const TPair<FString, FString>& It : Items)
    {
        if (!IsCalled(It.Key)) continue;
        ++Count;
        const FString Id = It.Key;
        Chips->AddSlot().Padding(3.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 4.f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(STextBlock).Text(FText::FromString(Id)) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7.f, 0.f, 0.f, 0.f)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this, Id, SetCalled]() { SetCalled(Id, false); RebuildSheet(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("✕"))).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]
        ];
    }
    if (Count == 0)
    {
        Chips->AddSlot().Padding(3.f)[ SNew(STextBlock).Text(LOCTEXT("NoneCalled", "none called")).ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    TSharedRef<SComboButton> AddBtn = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("CallAdd", "+ call")) ]
        .OnMenuOpenChanged_Lambda([this](bool bOpen) { if (!bOpen) RebuildSheet(); })   // refresh chips when the picker closes
        .OnGetMenuContent_Lambda([Items, IsCalled, SetCalled]() -> TSharedRef<SWidget>
        {
            TSharedRef<FString> Filter = MakeShared<FString>();
            TSharedRef<SBox> ListHost = SNew(SBox);

            // Shared rebuild closure — re-renders the (filtered) checklist into ListHost.
            TSharedRef<TFunction<void()>> Rebuild = MakeShared<TFunction<void()>>();
            *Rebuild = [Items, IsCalled, SetCalled, Filter, ListHostWeak = TWeakPtr<SBox>(ListHost)]()
            {
                TSharedPtr<SBox> LH = ListHostWeak.Pin();
                if (!LH.IsValid()) return;
                TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
                int32 Shown = 0;
                for (const TPair<FString, FString>& It : Items)
                {
                    if (!Filter->IsEmpty() && !It.Key.Contains(*Filter) && !It.Value.Contains(*Filter)) continue;
                    const FString Id = It.Key;
                    ++Shown;
                    List->AddSlot().AutoHeight().Padding(8.f, 3.f)
                    [
                        SNew(SCheckBox)
                        .IsChecked(IsCalled(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([Id, SetCalled](ECheckBoxState S) { SetCalled(Id, S == ECheckBoxState::Checked); })
                        [ SNew(STextBlock).Text(FText::FromString(Id)) ]
                    ];
                }
                if (Shown == 0)
                    List->AddSlot().Padding(8.f)[ SNew(STextBlock).Text(LOCTEXT("NoMatch", "(no matches)")) ];
                LH->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[ List ]);
            };
            (*Rebuild)();

            return SNew(SBox).MinDesiredWidth(240.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(4.f)
                [ SNew(SSearchBox).HintText(LOCTEXT("SearchLib", "search preset…"))
                  .OnTextChanged_Lambda([Filter, Rebuild](const FText& T) { *Filter = T.ToString(); (*Rebuild)(); }) ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).MaxDesiredHeight(320.f)[ ListHost ] ]
            ];
        });

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s · %d"), *Title.ToString(), Count))).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SHorizontalBox::Slot().AutoWidth()[ AddBtn ]
        ]
        + SVerticalBox::Slot().AutoHeight()[ Chips ]
    ];
}

TArray<TPair<FString, FString>> SPCAPCallSheetPanel::GatherActors() const
{
    TArray<TPair<FString, FString>> Out;
    // Performers from the Actor Database (Epic performer assets, called by performer name),
    // then any legacy roster actors not already listed.
    for (const FPCAPPerformerInfo& P : UPCAPMocapData::GetAllPerformers())
        if (!P.PerformerName.IsNone()) Out.Add({ P.PerformerName.ToString(), P.LiveLinkSubject.ToString() });
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> F;
    ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), F, false);
    for (const FAssetData& AD : F)
        if (UActorRosterEntry* E = Cast<UActorRosterEntry>(AD.GetAsset()))
            if (!Out.ContainsByPredicate([E](const TPair<FString, FString>& X){ return X.Key == E->ActorID; }))
                Out.Add({ E->ActorID, (E->FirstName + TEXT(" ") + E->LastName).TrimStartAndEnd() });
    Out.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B){ return A.Key < B.Key; });
    return Out;
}

TArray<TPair<FString, FString>> SPCAPCallSheetPanel::GatherProps() const
{
    TArray<TPair<FString, FString>> Out;
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> F;
    ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), F, false);
    for (const FAssetData& AD : F) if (UPropRosterEntry* E = Cast<UPropRosterEntry>(AD.GetAsset())) Out.Add({ E->PropID, E->DisplayName });
    Out.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B){ return A.Key < B.Key; });
    return Out;
}

TArray<TPair<FString, FString>> SPCAPCallSheetPanel::GatherVCams() const
{
    TArray<TPair<FString, FString>> Out;
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> F;
    ARM.Get().GetAssetsByClass(UPCAPVCamConfig::StaticClass()->GetClassPathName(), F, false);
    for (const FAssetData& AD : F) if (UPCAPVCamConfig* E = Cast<UPCAPVCamConfig>(AD.GetAsset())) Out.Add({ E->GetName(), E->LiveLinkSubjectName.ToString() });
    Out.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B){ return A.Key < B.Key; });
    return Out;
}

// ── Shots — the day's slate list (CSV import/export, adopted from Epic's Slates) ─────

FString SPCAPCallSheetPanel::NormalizeSlot(const FString& Slot)
{
    const FString Trim = Slot.TrimStartAndEnd();
    if (Trim.IsEmpty()) return Trim;
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
    if (UPCAPToolStatics::IsCalibrationSlot(Slot)) return EShotType::Calibration;
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
    for (const FShot& S : Sess->Shots) if (S.ShotID == ShotID) return;
    FShot New;
    New.ShotID   = ShotID;
    New.ShotType = ShotTypeFor(FString(), ShotID);
    Sess->Shots.Add(New);
    if (UMocapDatabase* DB = GetDB()) DB->MarkPackageDirty();
    RebuildSheet();
}

FReply SPCAPCallSheetPanel::OnRemoveShot(FString ShotID)
{
    if (FSession* Sess = EnsureActiveSession(false))
    {
        Sess->Shots.RemoveAll([&ShotID](const FShot& S){ return S.ShotID == ShotID; });
        if (UMocapDatabase* DB = GetDB()) DB->MarkPackageDirty();
        RebuildSheet();
    }
    return FReply::Handled();
}

void SPCAPCallSheetPanel::ApplyRowsToActiveDay(const TArray<FSlateCsvRow>& Rows)
{
    UMocapDatabase* DB = GetDB();
    FSession* Sess = EnsureActiveSession(true);
    if (!DB || !Sess) return;

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
        FShot& T = Existing ? *Existing : NewShot;

        T.ShotID      = ShotID;
        T.ShotType    = ShotTypeFor(R.Type, ShotID);
        T.Description = R.Description;
        T.Notes       = R.Notes;

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
    RebuildSheet();
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
    FSession* Sess = EnsureActiveSession(/*bCreate=*/false);

    TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("ImportCsv", "Import CSV")).ToolTipText(LOCTEXT("ImportCsvTip", "Bulk-create the day's shots from a CSV (slot,type,description,actors,props,notes)")).OnClicked_Lambda([this](){ ImportShotsCsv(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("ExportCsv", "Export CSV")).ToolTipText(LOCTEXT("ExportCsvTip", "Write this day's shot list to a CSV")).OnClicked_Lambda([this](){ ExportShotsCsv(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("AddCal", "+ Calibration shots"))
          .ToolTipText(LOCTEXT("AddCalTip", "Add any missing calibration shots for today's Capture Setup: 911 audio, 921 facial start, 922 facial end (stereo), 931 body, and 941+ one neutral-face take per facial actor. Never removes or duplicates a shot."))
          .OnClicked_Lambda([this]() { AddCalibrationShotsToActiveDay(); return FReply::Handled(); }) ]
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
        TArray<FShot> Sorted = Sess->Shots;
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

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Shots — %s"), *SessLabel))).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Toolbar ]
        + SVerticalBox::Slot().AutoHeight()[ List ]
    ];
}

// ── Capture Setup — per performer, per helmet ───────────────────────────────────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildCaptureSetupSection()
{
    UMocapDatabase* DB = GetDB();
    FShootDay* Day = DB ? DB->GetActiveDay() : nullptr;
    const UStageConfigAsset* Stage = DB ? DB->GetActiveStageConfig() : nullptr;

    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
    if (Day)
    {
        TArray<FString> Called = Day->CalledActorIDs;
        Called.Sort();
        bool bAdded = false;
        for (const FString& Id : Called)
        {
            if (!DB->GetCaptureSetup(Id))
            {
                DB->FindOrAddCaptureSetup(Id, UPCAPToolStatics::MakeDefaultCaptureSetup(Id, Stage, CallSheetFindExtension(Id)));
                bAdded = true;
            }
            Rows->AddSlot().AutoHeight().Padding(0.f, 2.f)[ BuildCaptureSetupRow(Id) ];
        }
        if (bAdded) DB->MarkPackageDirty();
        if (Called.Num() == 0)
            Rows->AddSlot().AutoHeight().Padding(4.f)
            [ SNew(STextBlock).Text(LOCTEXT("NoSetupRows", "Call actors above: each gets a row here for mocap, facial (mono or stereo), helmet and audio."))
              .ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    auto Header = [this](const FText& T, float W) -> TSharedRef<SWidget>
    {
        return SNew(SBox).WidthOverride(W)[ SNew(STextBlock).Text(T).ColorAndOpacity(FSlateColor(ColText2)) ];
    };

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [ SNew(STextBlock).Text(LOCTEXT("SecSetup", "Capture Setup · per performer, per helmet")).ColorAndOpacity(FSlateColor(ColGreen)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HActor", "Actor"), 120.f) ]
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HMocap", "Mocap"), 150.f) ]
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HFacial", "Facial"), 140.f) ]
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HMode", "Mode"), 130.f) ]
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HHelmet", "Helmet"), 140.f) ]
            + SHorizontalBox::Slot().AutoWidth()[ Header(LOCTEXT("HAudio", "Audio"), 120.f) ]
        ]
        + SVerticalBox::Slot().AutoHeight()[ Rows ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("SetupHint", "Rows start from the actor's saved default, else the day's stage. Changes here apply to this day only; \"Save as default\" writes the row back to the actor. Lists: Project Settings > PCAP > PCAP Tool > Capture Systems."))
          .ColorAndOpacity(FSlateColor(ColText2)) ]
    ];
}

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildCaptureSetupRow(const FString& ActorID)
{
    // Look the row up by id on every read/write: the day's array can reallocate.
    auto Row = [this, ActorID]() -> FPCAPPerformerCaptureSetup* { UMocapDatabase* D = GetDB(); return D ? D->GetCaptureSetup(ActorID) : nullptr; };
    auto Dirty = [this]() { if (UMocapDatabase* D = GetDB()) D->MarkPackageDirty(); };
    const UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    UMocapDatabase* DB = GetDB();
    const UStageConfigAsset* Stage = DB ? DB->GetActiveStageConfig() : nullptr;
    TWeakObjectPtr<const UStageConfigAsset> WeakStage(Stage);
    const bool bIsPerformer = CallSheetFindPerformer(ActorID).IsSet();

    auto ModeToggle = [this, Row, Dirty](bool bStereoButton) -> TSharedRef<SWidget>
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), "RadioButton")
            .IsChecked_Lambda([Row, bStereoButton]()
            {
                const FPCAPPerformerCaptureSetup* R = Row();
                return (R && R->bStereo == bStereoButton) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([Row, Dirty, bStereoButton](ECheckBoxState)
            {
                if (FPCAPPerformerCaptureSetup* R = Row()) { R->bStereo = bStereoButton; Dirty(); }
            })
            [ SNew(STextBlock).Text(bStereoButton ? LOCTEXT("Stereo", "Stereo") : LOCTEXT("Mono", "Mono")) ];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(120.f)[ SNew(STextBlock).Text(FText::FromString(ActorID)) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(150.f).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
          [ PCAPStringPick::Make(
              [Row]() { const FPCAPPerformerCaptureSetup* R = Row(); return R ? R->MocapSystem : FString(); },
              [Settings]() { return Settings ? Settings->GetMocapOptions() : TArray<FString>(); },
              [Row, Dirty](const FString& V) { if (FPCAPPerformerCaptureSetup* R = Row()) { R->MocapSystem = V; Dirty(); } },
              LOCTEXT("NoMocapRow", "(no mocap)"), LOCTEXT("NoneRow", "(none)")) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(140.f).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
          [ PCAPStringPick::Make(
              [Row]() { const FPCAPPerformerCaptureSetup* R = Row(); return R ? R->FacialSystem : FString(); },
              [Settings]() { return Settings ? Settings->GetFacialOptions() : TArray<FString>(); },
              [Row, Dirty, Settings](const FString& V)
              {
                  FPCAPPerformerCaptureSetup* R = Row();
                  if (!R) return;
                  R->FacialSystem = V;
                  if (Settings && !R->AudioSystem.IsEmpty() && !Settings->GetAudioOptions(V).Contains(R->AudioSystem))
                      R->AudioSystem.Empty();   // e.g. iPhone audio without the iPhone
                  Dirty();
              },
              LOCTEXT("NoFacialRow", "(no facial)"), LOCTEXT("NoneRow2", "(none)")) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(130.f)
          [
              SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[ ModeToggle(false) ]
              + SHorizontalBox::Slot().AutoWidth()[ ModeToggle(true) ]
              // Amber "!" when the row's mode isn't one the stage supports.
              + SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
              [ SNew(STextBlock).Text(FText::FromString(TEXT("!"))).ColorAndOpacity(FSlateColor(FLinearColor(0.878f, 0.627f, 0.188f)))
                .ToolTipText(LOCTEXT("ModeMismatch", "The day's stage doesn't list this facial mode (Stage Database > Mono / Stereo)."))
                .Visibility_Lambda([Row, WeakStage]()
                {
                    const FPCAPPerformerCaptureSetup* R = Row();
                    const UStageConfigAsset* S = WeakStage.Get();
                    if (!R || !S || R->FacialSystem.IsEmpty() || (!S->bFacialMono && !S->bFacialStereo)) return EVisibility::Collapsed;
                    const bool bOk = R->bStereo ? S->bFacialStereo : S->bFacialMono;
                    return bOk ? EVisibility::Collapsed : EVisibility::Visible;
                }) ]
          ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(140.f).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
          [ PCAPStringPick::Make(
              [Row]() { const FPCAPPerformerCaptureSetup* R = Row(); return R ? CallSheetHelmetName(R->Helmet) : FString(); },
              []() { TArray<FString> Names; CallSheetHelmets().GetKeys(Names); Names.Sort(); return Names; },
              [Row, Dirty](const FString& V)
              {
                  FPCAPPerformerCaptureSetup* R = Row();
                  if (!R) return;
                  const FSoftObjectPath* P = CallSheetHelmets().Find(V);
                  R->Helmet = P ? TSoftObjectPtr<UHMCRigEntry>(*P) : TSoftObjectPtr<UHMCRigEntry>();
                  Dirty();
              },
              LOCTEXT("NoHelmet", "(no helmet)"), LOCTEXT("NoneRow3", "(none)")) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(120.f).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
          [ PCAPStringPick::Make(
              [Row]() { const FPCAPPerformerCaptureSetup* R = Row(); return R ? R->AudioSystem : FString(); },
              [Row, Settings]() { const FPCAPPerformerCaptureSetup* R = Row(); return (Settings && R) ? Settings->GetAudioOptions(R->FacialSystem) : TArray<FString>(); },
              [Row, Dirty](const FString& V) { if (FPCAPPerformerCaptureSetup* R = Row()) { R->AudioSystem = V; Dirty(); } },
              LOCTEXT("NoAudioRow", "(no audio)"), LOCTEXT("NoneRow4", "(none)")) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SButton)
          .Text(LOCTEXT("SaveDefault", "Save as default"))
          .IsEnabled(bIsPerformer)
          .ToolTipText(bIsPerformer
              ? LOCTEXT("SaveDefaultTip", "Write this row back to the actor (Actor Database) so the next day pre-fills from it. Separate from today's override.")
              : LOCTEXT("SaveDefaultLegacy", "Only actors from the Actor Database can store defaults (this one is a legacy roster entry)."))
          .OnClicked_Lambda([this, ActorID]() { SaveRowAsPerformerDefault(ActorID); return FReply::Handled(); }) ];
}

void SPCAPCallSheetPanel::SaveRowAsPerformerDefault(const FString& ActorID)
{
    UMocapDatabase* DB = GetDB();
    const FPCAPPerformerCaptureSetup* R = DB ? DB->GetCaptureSetup(ActorID) : nullptr;
    const TOptional<FPCAPPerformerInfo> Perf = CallSheetFindPerformer(ActorID);
    if (!R || !Perf.IsSet()) return;

    UPCAPPerformerExtension* Ext = UPCAPMocapData::FindPerformerExtension(Perf->AssetUID);
    if (!Ext) Ext = UPCAPMocapData::EnsurePerformerExtension(Perf->Asset.LoadSynchronous());
    if (!Ext) return;

    Ext->DefaultMocapSystem  = R->MocapSystem;
    Ext->DefaultFacialSystem = R->FacialSystem;
    Ext->bDefaultStereo      = R->bStereo;
    Ext->DefaultAudioSystem  = R->AudioSystem;
    Ext->HMCRig              = R->Helmet;
    if (UPackage* Pkg = Ext->GetPackage())
    {
        Pkg->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }

    FNotificationInfo Info(FText::Format(LOCTEXT("SavedDefault", "Saved as {0}'s default setup."), FText::FromString(ActorID)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void SPCAPCallSheetPanel::AddCalibrationShotsToActiveDay()
{
    UMocapDatabase* DB = GetDB();
    FShootDay* Day = DB ? DB->GetActiveDay() : nullptr;
    if (!Day) return;
    const int32 Added = UPCAPToolStatics::AddCalibrationShots(*Day,
        UPCAPToolStatics::ResolveCalibrationNeeds(*Day, DB->GetActiveStageConfig()));
    if (Added > 0) DB->MarkPackageDirty();

    FNotificationInfo Info(Added > 0
        ? FText::Format(LOCTEXT("CalAdded", "Added {0} calibration shot(s)."), FText::AsNumber(Added))
        : LOCTEXT("CalNone", "Calibration shots already in place."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    RebuildSheet();
}

// ── Stage gap-fill: one-click Volume Visualizer for the active stage ─────────────────

void SPCAPCallSheetPanel::SpawnVolumeVisualizer()
{
    UMocapDatabase* DB = GetDB();
    UStageConfigAsset* Cfg = DB ? DB->GetActiveStageConfig() : nullptr;
    if (!Cfg)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Spawn Volume Visualizer: no active stage config — set a stage on the production/day first."));
        return;
    }
    if (!GEditor) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    APCAPVolumeVisualizer* Viz = World->SpawnActor<APCAPVolumeVisualizer>();
    if (!Viz) return;

    Viz->StageConfig = Cfg;
    Viz->RefreshFromStageConfig();
    Viz->SetActorLabel(FString::Printf(TEXT("VolumeViz_%s"), *Cfg->ConfigName));

    GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
    GEditor->SelectActor(Viz, /*bInSelected=*/true, /*bNotify=*/true);

    UE_LOG(LogTemp, Log, TEXT("[PCAP] Spawned Volume Visualizer for stage '%s'."), *Cfg->ConfigName);
}

#undef LOCTEXT_NAMESPACE
