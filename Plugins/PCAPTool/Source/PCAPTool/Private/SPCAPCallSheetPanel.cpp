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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

TSharedRef<SWidget> SPCAPCallSheetPanel::MakeAddButton(const FText& HintText, TFunction<void(const FString&)> OnCreate)
{
    return SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(TEXT("+"))) ]
        .OnGetMenuContent_Lambda([HintText, OnCreate]() -> TSharedRef<SWidget>
        {
            // Build the field first so the Create button + auto-focus timer can reference it.
            TSharedRef<SEditableTextBox> Box = SNew(SEditableTextBox)
                .HintText(HintText)
                .MinDesiredWidth(170.f)
                .OnTextCommitted_Lambda([OnCreate](const FText& T, ETextCommit::Type C)
                {
                    if (C != ETextCommit::OnEnter) return;            // Enter in the field commits
                    const FString N = T.ToString().TrimStartAndEnd();
                    if (N.IsEmpty()) return;
                    FSlateApplication::Get().DismissAllMenus();        // close popup BEFORE rebuilding the sheet
                    OnCreate(N);
                });

            // Combo popups don't auto-focus their content — grab keyboard focus the instant
            // the popup appears so typing just works.
            Box->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
                [BoxWeak = TWeakPtr<SEditableTextBox>(Box)](double, float)
                {
                    if (TSharedPtr<SEditableTextBox> B = BoxWeak.Pin())
                        FSlateApplication::Get().SetKeyboardFocus(B, EFocusCause::SetDirectly);
                    return EActiveTimerReturnType::Stop;
                }));

            return SNew(SBox).Padding(6.f).MinDesiredWidth(230.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ Box ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
                [ SNew(SButton)
                  .Text(LOCTEXT("CreateEntry", "Create"))
                  .OnClicked_Lambda([OnCreate, BoxWeak = TWeakPtr<SEditableTextBox>(Box)]()
                  {
                      if (TSharedPtr<SEditableTextBox> B = BoxWeak.Pin())
                      {
                          const FString N = B->GetText().ToString().TrimStartAndEnd();
                          if (!N.IsEmpty()) { FSlateApplication::Get().DismissAllMenus(); OnCreate(N); }
                      }
                      return FReply::Handled();
                  }) ]
            ];
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
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildHeader() ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildStageArea() ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)[ BuildHMCDayToggle() ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)
        [ BuildCallSection(LOCTEXT("CalledActors", "Called actors"), GatherActors(),
            [this](const FString& Id){ UMocapDatabase* D = GetDB(); return D && D->IsActorCalled(Id); },
            [this](const FString& Id, bool b){ if (UMocapDatabase* D = GetDB()) { D->SetActorCalled(Id, b); D->MarkPackageDirty(); } },
            [this](const FString& Id)
            {
                CreateAssetIn(UActorRosterEntry::StaticClass(), PCAPPaths::ActorsDir(), Id,
                    [Id](UObject* O){ if (UActorRosterEntry* E = Cast<UActorRosterEntry>(O)) E->ActorID = Id; });
                if (UMocapDatabase* D = GetDB()) { D->SetActorCalled(Id, true); D->MarkPackageDirty(); }
                RebuildSheet();
            }) ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)
        [ BuildCallSection(LOCTEXT("CalledProps", "Called props"), GatherProps(),
            [this](const FString& Id){ UMocapDatabase* D = GetDB(); return D && D->IsPropCalled(Id); },
            [this](const FString& Id, bool b){ if (UMocapDatabase* D = GetDB()) { D->SetPropCalled(Id, b); D->MarkPackageDirty(); } },
            [this](const FString& Id)
            {
                CreateAssetIn(UPropRosterEntry::StaticClass(), PCAPPaths::PropsDir(), Id,
                    [Id](UObject* O){ if (UPropRosterEntry* E = Cast<UPropRosterEntry>(O)) E->PropID = Id; });
                if (UMocapDatabase* D = GetDB()) { D->SetPropCalled(Id, true); D->MarkPackageDirty(); }
                RebuildSheet();
            }) ]
        + SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 12.f)
        [ BuildCallSection(LOCTEXT("CalledVCam", "Called vcam"), GatherVCams(),
            [this](const FString& Id){ UMocapDatabase* D = GetDB(); return D && D->IsVCamCalled(Id); },
            [this](const FString& Id, bool b){ if (UMocapDatabase* D = GetDB()) { D->SetVCamCalled(Id, b); D->MarkPackageDirty(); } },
            [this](const FString& Id)
            {
                CreateAssetIn(UPCAPVCamConfig::StaticClass(), PCAPPaths::VCamsDir(), Id);
                if (UMocapDatabase* D = GetDB()) { D->SetVCamCalled(Id, true); D->MarkPackageDirty(); }
                RebuildSheet();
            }) ]
        + SScrollBox::Slot()[ BuildShotsSection() ];
}

// ── Header — production / day / stage pickers + readiness + spawn-viz ─────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildHeader()
{
    UMocapDatabase* DB = GetDB();
    const FString Prod = DB ? DB->ActiveProductionCode : FString();
    const FString Day  = DB ? DB->ActiveDayID : FString();
    UStageConfigAsset* Stage = DB ? DB->GetActiveStageConfig() : nullptr;
    const FString StageName = Stage ? (Stage->ConfigName.IsEmpty() ? Stage->GetName() : Stage->ConfigName) : FString(TEXT("(no stage)"));

    TArray<FString> Issues;
    const bool bReady = DB && DB->GetActiveDayReadiness(Issues);
    const FLinearColor ColAmber(0.878f, 0.627f, 0.188f);
    const FText ReadyText = bReady
        ? LOCTEXT("Ready", "Ready to shoot")
        : FText::FromString(Issues.Num() > 0 ? FString::Printf(TEXT("Not ready — %s"), *FString::Join(Issues, TEXT(", "))) : FString(TEXT("Not ready")));

    TArray<FString> Systems;
    if (Stage)
    {
        if (Stage->BodySystem  != EBodySystem::None)  Systems.Add(StaticEnum<EBodySystem>()->GetDisplayNameTextByValue((int64)Stage->BodySystem).ToString());
        if (Stage->FaceSystem  != EFaceSystem::None)  Systems.Add(StaticEnum<EFaceSystem>()->GetDisplayNameTextByValue((int64)Stage->FaceSystem).ToString());
        if (Stage->AudioSystem != EAudioSystem::None) Systems.Add(StaticEnum<EAudioSystem>()->GetDisplayNameTextByValue((int64)Stage->AudioSystem).ToString());
        if (Stage->VCamSystem  != EVCamSystem::None)  Systems.Add(StaticEnum<EVCamSystem>()->GetDisplayNameTextByValue((int64)Stage->VCamSystem).ToString());
    }
    const FString SystemsText = Systems.Num() > 0 ? FString::Join(Systems, TEXT("  ·  ")) : FString(TEXT("no systems set"));

    TSharedRef<SComboButton> ProdPick = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Prod.IsEmpty() ? TEXT("(pick production)") : Prod)).ColorAndOpacity(FSlateColor(ColGreen)) ]
        .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
        {
            FMenuBuilder MB(true, nullptr);
            if (UMocapDatabase* D = GetDB())
            {
                TArray<FProduction> P = D->Productions;
                P.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
                for (const FProduction& Pr : P)
                {
                    const FString C = Pr.ProjectCode;
                    MB.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s — %s"), *C, *Pr.ProductionName)), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, C]() { if (UMocapDatabase* D2 = GetDB()) { D2->ActiveProductionCode = C; } RebuildSheet(); })));
                }
            }
            return MB.MakeWidget();
        });

    TSharedRef<SComboButton> DayPick = SNew(SComboButton)
        .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(Day.IsEmpty() ? TEXT("(pick day)") : (TEXT("Day_") + Day))) ]
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
                        MB.AddMenuEntry(FText::FromString(TEXT("Day_") + Id), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Id]() { if (UMocapDatabase* D2 = GetDB()) { D2->ActiveDayID = Id; } RebuildSheet(); })));
                    }
                }
            return MB.MakeWidget();
        });

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

    auto Dot = [this]() { return SNew(STextBlock).Text(FText::FromString(TEXT("·"))).ColorAndOpacity(FSlateColor(ColText2)); };

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ProdPick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
            [ MakeAddButton(LOCTEXT("AddProd", "+ production code  ↵"), [this](const FString& Code)
              {
                  if (UMocapDatabase* D = GetDB())
                  {
                      if (!D->GetProductionByCode(Code)) { FProduction P; P.ProjectCode = Code; P.ProductionName = Code; D->Productions.Add(P); D->MarkPackageDirty(); }
                      D->ActiveProductionCode = Code;
                  }
                  RebuildSheet();
              }) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)[ Dot() ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ DayPick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
            [ MakeAddButton(LOCTEXT("AddDay", "+ day id  ↵"), [this](const FString& Id)
              {
                  if (UMocapDatabase* D = GetDB())
                      if (FProduction* P = D->GetProductionByCode(D->ActiveProductionCode))
                      {
                          if (!D->GetDay(D->ActiveProductionCode, Id)) { FShootDay Dy; Dy.DayID = Id; P->Days.Add(Dy); D->MarkPackageDirty(); }
                          D->ActiveDayID = Id;
                      }
                  RebuildSheet();
              }) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)[ Dot() ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ StagePick ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
            [ MakeAddButton(LOCTEXT("AddStage", "+ stage name  ↵"), [this](const FString& Name)
              {
                  UStageConfigAsset* S = Cast<UStageConfigAsset>(CreateAssetIn(UStageConfigAsset::StaticClass(), PCAPPaths::StagesDir(), Name,
                      [Name](UObject* O){ if (UStageConfigAsset* SC = Cast<UStageConfigAsset>(O)) SC->ConfigName = Name; }));
                  if (S)
                      if (UMocapDatabase* D = GetDB())
                          if (FShootDay* Dy = D->GetDay(D->ActiveProductionCode, D->ActiveDayID)) { Dy->ActiveStageConfig = S; D->MarkPackageDirty(); }
                  RebuildSheet();
              }) ]
            + SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(ReadyText).ColorAndOpacity(FSlateColor(bReady ? ColGreen : ColAmber)) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(SystemsText)).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [
            SNew(SButton)
            .Text(LOCTEXT("SpawnViz", "Spawn volume visualizer"))
            .ToolTipText(LOCTEXT("SpawnVizTip", "Drop a Volume Visualizer into the level for this day's stage"))
            .IsEnabled(Stage != nullptr)
            .OnClicked_Lambda([this]() { SpawnVolumeVisualizer(); return FReply::Handled(); })
        ]
    ];
}

// ── Stage area — the called stage's setup (dropdown is in the header) ─────────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildStageArea()
{
    UMocapDatabase* DB = GetDB();
    UStageConfigAsset* Stage = DB ? DB->GetActiveStageConfig() : nullptr;
    if (StageDetailsView.IsValid()) StageDetailsView->SetObject(Stage);

    if (!Stage)
    {
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
        [ SNew(STextBlock).Text(LOCTEXT("NoStage", "No stage called — pick one in the header (manage the library in the Stage Database).")).ColorAndOpacity(FSlateColor(ColText2)) ];
    }

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(12.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(LOCTEXT("StageSetup", "Stage setup — edits update this stage's preset")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()[ StageDetailsView.ToSharedRef() ]
    ];
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

// ── Called section — chips of called items + a "+ call" checklist picker ──────

TSharedRef<SWidget> SPCAPCallSheetPanel::BuildCallSection(const FText& Title,
    const TArray<TPair<FString, FString>>& Items,
    TFunction<bool(const FString&)> IsCalled,
    TFunction<void(const FString&, bool)> SetCalled,
    TFunction<void(const FString&)> CreateNew)
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
        .OnGetMenuContent_Lambda([Items, IsCalled, SetCalled, CreateNew]() -> TSharedRef<SWidget>
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
                [ SNew(SEditableTextBox).HintText(LOCTEXT("CreateNewHint", "+ new (creates in database)  ↵"))
                  .OnTextCommitted_Lambda([CreateNew](const FText& T, ETextCommit::Type C)
                  {
                      if (C != ETextCommit::OnEnter) return;
                      const FString N = T.ToString().TrimStartAndEnd();
                      if (!N.IsEmpty()) CreateNew(N);
                  }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(4.f)
                [ SNew(SSearchBox).HintText(LOCTEXT("SearchLib", "search…"))
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
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> F;
    ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), F, false);
    for (const FAssetData& AD : F) if (UActorRosterEntry* E = Cast<UActorRosterEntry>(AD.GetAsset())) Out.Add({ E->ActorID, (E->FirstName + TEXT(" ") + E->LastName).TrimStartAndEnd() });
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
