#include "SPCAPProductionDatabasePanel.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "StageConfigAsset.h"
#include "SPCAPRosterCard.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "PCAPProductionDatabase"

// Helpers are prefixed per-panel: under UE's unity (combined-translation-unit) builds
// every anonymous namespace in the blob is the same namespace, so a bare name shared
// with a sibling panel is a redefinition (see the note in SPCAPPanelStyle.h).
namespace
{
    // Every stage config in the project, ordered the way the Call Sheet's stage picker
    // orders them. Stages are made in the Stage Database — this only picks from them.
    TArray<UStageConfigAsset*> PCAPProdDBAllStages()
    {
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> Found;
        ARM.Get().GetAssetsByClass(UStageConfigAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);

        TArray<UStageConfigAsset*> Stages;
        for (const FAssetData& AD : Found)
            if (UStageConfigAsset* S = Cast<UStageConfigAsset>(AD.GetAsset())) Stages.Add(S);
        Stages.Sort([](const UStageConfigAsset& A, const UStageConfigAsset& B){ return A.ConfigName < B.ConfigName; });
        return Stages;
    }

    // A stage's display label — its ConfigName, falling back to the asset name. Same rule
    // the Call Sheet's picker uses, so the two read as the same stage.
    FString PCAPProdDBStageLabel(const UStageConfigAsset* Stage)
    {
        if (!Stage) return FString(TEXT("(none)"));
        return Stage->ConfigName.IsEmpty() ? Stage->GetName() : Stage->ConfigName;
    }
}

UMocapDatabase* SPCAPProductionDatabasePanel::GetDB() const
{
    UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetDatabase() : nullptr;
}

void SPCAPProductionDatabasePanel::Construct(const FArguments& InArgs)
{
    if (UPCAPToolSettings* S = UPCAPToolSettings::Get()) { S->GetOrCreateDatabase(); }

    ChildSlot
    [
        SNew(SOverlay)

        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "PRODUCTION DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search productions…")).OnTextChanged(this, &SPCAPProductionDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(170.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new project code  ↵")).OnTextCommitted(this, &SPCAPProductionDatabasePanel::OnNewCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<TSharedPtr<FString>>)
                        .ListItemsSource(&FilteredCodes)
                        .OnGenerateTile(this, &SPCAPProductionDatabasePanel::OnGenerateTile)
                        .ItemWidth(132.f)
                        .ItemHeight(154.f)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("Empty", "No productions yet — type a project code above to create one."))
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredCodes.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedCode.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(372.f).MaxDesiredHeight(430.f)   // + the default-stage row
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    Reload();
}

void SPCAPProductionDatabasePanel::Reload()
{
    Codes.Reset();
    if (UMocapDatabase* DB = GetDB())
    {
        TArray<FProduction> Sorted = DB->Productions;   // sorted copy — alphabetical
        Sorted.Sort([](const FProduction& A, const FProduction& B){ return A.ProjectCode < B.ProjectCode; });
        for (const FProduction& P : Sorted) Codes.Add(MakeShared<FString>(P.ProjectCode));
    }
    ApplyFilter();
}

void SPCAPProductionDatabasePanel::ApplyFilter()
{
    FilteredCodes.Reset();
    UMocapDatabase* DB = GetDB();
    for (const TSharedPtr<FString>& C : Codes)
    {
        if (!C.IsValid()) continue;
        if (FilterText.IsEmpty()) { FilteredCodes.Add(C); continue; }
        FString Name;
        if (DB) if (FProduction* P = DB->GetProductionByCode(*C)) Name = P->ProductionName;
        if (C->Contains(FilterText) || Name.Contains(FilterText)) FilteredCodes.Add(C);
    }
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

TSharedRef<ITableRow> SPCAPProductionDatabasePanel::OnGenerateTile(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
{
    const FString Code = Item.IsValid() ? *Item : FString();
    FString Name = Code;
    if (UMocapDatabase* DB = GetDB()) if (FProduction* P = DB->GetProductionByCode(Code)) Name = P->ProductionName;

    return SNew(STableRow<TSharedPtr<FString>>, Owner)
        .Padding(4.f)
        [
            SNew(SPCAPRosterCard)
            .Title(FText::FromString(Name))
            .Subtitle(FText::FromString(Code))
            .Accent(ColGreen)
            .OnClicked(FSimpleDelegate::CreateLambda([this, Code]() { OpenDetail(Code); }))
        ];
}

void SPCAPProductionDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPProductionDatabasePanel::OnNewCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString Code = Text.ToString().TrimStartAndEnd();
    if (Code.IsEmpty()) return;
    if (UMocapDatabase* DB = GetDB())
    {
        if (!DB->GetProductionByCode(Code))
        {
            FProduction P; P.ProjectCode = Code; P.ProductionName = Code;
            DB->Productions.Add(P);
            DB->MarkPackageDirty();
        }
        Reload();
        OpenDetail(Code);
    }
}

void SPCAPProductionDatabasePanel::OpenDetail(const FString& Code)
{
    SelectedCode = MakeShared<FString>(Code);
    if (DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(Code));
}

void SPCAPProductionDatabasePanel::CloseDetail()
{
    SelectedCode.Reset();
}

TSharedRef<SWidget> SPCAPProductionDatabasePanel::BuildDetailFor(const FString& Code)
{
    UMocapDatabase* DB = GetDB();
    FProduction* P = DB ? DB->GetProductionByCode(Code) : nullptr;
    if (!P) return SNew(SBox);

    const int32 DayCount = P->Days.Num();
    const bool bActive = (DB->ActiveProductionCode == Code);

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(P->ProductionName)).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
              .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
              [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Name", "Production name")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text(FText::FromString(P->ProductionName))
          .OnTextCommitted_Lambda([this, Code](const FText& T, ETextCommit::Type)
          {
              if (UMocapDatabase* D = GetDB()) if (FProduction* Pr = D->GetProductionByCode(Code))
              { Pr->ProductionName = T.ToString(); D->MarkPackageDirty(); Reload(); }
          }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Number", "Production number")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text(FText::FromString(P->ProjectCode))
          .OnTextCommitted_Lambda([this, Code](const FText& T, ETextCommit::Type CommitType)
          {
              if (CommitType != ETextCommit::OnEnter) return;
              const FString NewCode = T.ToString().TrimStartAndEnd();
              if (NewCode.IsEmpty() || NewCode == Code) return;
              UMocapDatabase* D = GetDB();
              if (!D) return;
              if (D->GetProductionByCode(NewCode))   // collision — another production already uses it
              {
                  FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Number \"%s\" is already in use."), *NewCode)));
                  Info.ExpireDuration = 3.0f;
                  FSlateNotificationManager::Get().AddNotification(Info);
                  Reload();   // revert the field
                  return;
              }
              // Safe re-key: only FProduction.ProjectCode and the active selection reference the
              // number persistently (days/sessions/shots/takes are nested inside the production).
              if (FProduction* Pr = D->GetProductionByCode(Code))
              {
                  Pr->ProjectCode = NewCode;
                  if (D->ActiveProductionCode == Code) D->ActiveProductionCode = NewCode;
                  D->MarkPackageDirty();
              }
              Reload();
              OpenDetail(NewCode);   // reopen the detail under the new number
          }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("RenumberNote", "Renumber before shooting — once takes are recorded, their folders stay under the old number."))
            .ColorAndOpacity(FSlateColor(ColText2)) ]

        // The production-wide stage. UMocapDatabase::GetActiveStageConfig resolves "day
        // override, else production", but only the day half had a writer (the Call Sheet's
        // picker), so the production fallback could never fire and every freshly added day
        // read "No stage set" until someone re-picked the stage for it. Set it here once and
        // days inherit it. The Stage Database's delete guard already lists this as "the
        // production's stage", so a stage set here can't be deleted out from under the show.
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Stage", "Default stage")).ColorAndOpacity(FSlateColor(ColText2)) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(FText::FromString(PCAPProdDBStageLabel(P->ActiveStageConfig.LoadSynchronous()))) ]
            .OnGetMenuContent_Lambda([this, Code]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);
                MB.AddMenuEntry(LOCTEXT("StageNone", "(none)"), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Code]()
                    {
                        if (UMocapDatabase* D = GetDB()) if (FProduction* Pr = D->GetProductionByCode(Code))
                        { Pr->ActiveStageConfig = TSoftObjectPtr<UStageConfigAsset>(); D->MarkPackageDirty(); }   // back to unset
                        OpenDetail(Code);
                    })));
                for (UStageConfigAsset* S : PCAPProdDBAllStages())
                {
                    TWeakObjectPtr<UStageConfigAsset> W(S);
                    MB.AddMenuEntry(FText::FromString(PCAPProdDBStageLabel(S)), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Code, W]()
                        {
                            if (!W.IsValid()) return;
                            if (UMocapDatabase* D = GetDB()) if (FProduction* Pr = D->GetProductionByCode(Code))
                            { Pr->ActiveStageConfig = W.Get(); D->MarkPackageDirty(); }
                            OpenDetail(Code);
                        })));
                }
                return MB.MakeWidget();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
        [ SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("StageNote", "Days inherit this until the Call Sheet sets a stage for the day."))
            .ColorAndOpacity(FSlateColor(ColText2)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d shoot day%s  ·  manage days in the Call Sheet"), DayCount, DayCount == 1 ? TEXT("") : TEXT("s")))).ColorAndOpacity(FSlateColor(ColText2)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton)
              .IsEnabled(!bActive)
              .Text(bActive ? LOCTEXT("IsActive", "Active") : LOCTEXT("SetActive", "Set as active production"))
              .OnClicked_Lambda([this, Code]() { if (UMocapDatabase* D = GetDB()) { D->ActiveProductionCode = Code; D->MarkPackageDirty(); } OpenDetail(Code); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Delete", "Delete"))
              .OnClicked_Lambda([this, Code]()
              {
                  if (UMocapDatabase* D = GetDB())
                  {
                      D->Productions.RemoveAll([&Code](const FProduction& Pr){ return Pr.ProjectCode == Code; });
                      if (D->ActiveProductionCode == Code) D->ActiveProductionCode.Reset();
                      D->MarkPackageDirty();
                  }
                  CloseDetail();
                  Reload();
                  return FReply::Handled();
              }) ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
