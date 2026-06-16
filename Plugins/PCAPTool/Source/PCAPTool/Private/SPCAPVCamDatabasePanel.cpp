#include "SPCAPVCamDatabasePanel.h"
#include "VCamConfig.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"
#include "SPCAPRosterCard.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"

#define LOCTEXT_NAMESPACE "PCAPVCamDatabase"

void SPCAPVCamDatabasePanel::Construct(const FArguments& InArgs)
{
    FPropertyEditorModule& PEM = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs Args;
    Args.bAllowSearch = false;
    Args.bHideSelectionTip = true;
    Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsView = PEM.CreateDetailView(Args);

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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "VCAM DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search vcams…")).OnTextChanged(this, &SPCAPVCamDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(160.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new vcamID  ↵")).OnTextCommitted(this, &SPCAPVCamDatabasePanel::OnNewVCamCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<TWeakObjectPtr<UPCAPVCamConfig>>)
                        .ListItemsSource(&FilteredVCams)
                        .OnGenerateTile(this, &SPCAPVCamDatabasePanel::OnGenerateTile)
                        .ItemWidth(132.f)
                        .ItemHeight(154.f)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("Empty", "No vcams yet — type an ID in the box above to create one."))
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredVCams.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedVCam.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(420.f).MaxDesiredHeight(520.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadVCams();
}

void SPCAPVCamDatabasePanel::ReloadVCams()
{
    AllVCams.Reset();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UPCAPVCamConfig::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
        if (UPCAPVCamConfig* E = Cast<UPCAPVCamConfig>(AD.GetAsset()))
            AllVCams.Add(E);

    AllVCams.Sort([](const TWeakObjectPtr<UPCAPVCamConfig>& A, const TWeakObjectPtr<UPCAPVCamConfig>& B)
    { return A.IsValid() && B.IsValid() && A->GetName() < B->GetName(); });

    ApplyFilter();
}

void SPCAPVCamDatabasePanel::ApplyFilter()
{
    FilteredVCams.Reset();
    for (const TWeakObjectPtr<UPCAPVCamConfig>& Ptr : AllVCams)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty() || Ptr->GetName().Contains(FilterText) || Ptr->LiveLinkSubjectName.ToString().Contains(FilterText))
            FilteredVCams.Add(Ptr);
    }
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

UPCAPVCamConfig* SPCAPVCamDatabasePanel::CreateVCamAsset(const FString& VCamID)
{
    if (VCamID.IsEmpty()) return nullptr;
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::VCamsDir(), *VCamID);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UPCAPVCamConfig* Entry = NewObject<UPCAPVCamConfig>(Package, FName(*VCamID), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPVCamDatabasePanel::SaveVCamAsset(UPCAPVCamConfig* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPVCamDatabasePanel::DeleteVCamAsset(UPCAPVCamConfig* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPVCamDatabasePanel::OnGenerateTile(TWeakObjectPtr<UPCAPVCamConfig> Item, const TSharedRef<STableViewBase>& Owner)
{
    UPCAPVCamConfig* E = Item.Get();
    const FText Title    = FText::FromString(E ? E->GetName() : TEXT("(missing)"));
    const FText Subtitle = E ? FText::FromName(E->LiveLinkSubjectName) : FText::GetEmpty();

    return SNew(STableRow<TWeakObjectPtr<UPCAPVCamConfig>>, Owner)
        .Padding(4.f)
        [
            SNew(SPCAPRosterCard)
            .Title(Title)
            .Subtitle(Subtitle)
            .Accent(ColGreen)
            .OnClicked(FSimpleDelegate::CreateLambda([this, Item]() { OpenDetail(Item.Get()); }))
        ];
}

void SPCAPVCamDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPVCamDatabasePanel::OnNewVCamCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString VCamID = Text.ToString().TrimStartAndEnd();
    if (VCamID.IsEmpty()) return;
    if (UPCAPVCamConfig* Created = CreateVCamAsset(VCamID))
    {
        ReloadVCams();
        OpenDetail(Created);
    }
}

void SPCAPVCamDatabasePanel::OpenDetail(UPCAPVCamConfig* Entry)
{
    SelectedVCam = Entry;
    if (DetailsView.IsValid()) DetailsView->SetObject(Entry);
    if (DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(Entry));
}

void SPCAPVCamDatabasePanel::CloseDetail()
{
    SelectedVCam = nullptr;
    if (DetailsView.IsValid()) DetailsView->SetObject(nullptr);
}

TSharedRef<SWidget> SPCAPVCamDatabasePanel::BuildDetailFor(UPCAPVCamConfig* Entry)
{
    if (!Entry || !DetailsView.IsValid()) return SNew(SBox);
    TWeakObjectPtr<UPCAPVCamConfig> Weak(Entry);

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Entry->GetName())).ColorAndOpacity(FSlateColor(ColGreen)) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
              .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
              [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [ SNew(SCheckBox)
          .IsChecked_Lambda([Weak]()
          {
              UPCAPToolSettings* S = UPCAPToolSettings::Get();
              UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
              const FString ID = Weak.IsValid() ? Weak->GetName() : FString();
              return (DB && DB->IsVCamCalled(ID)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
          })
          .OnCheckStateChanged_Lambda([Weak](ECheckBoxState State)
          {
              UPCAPToolSettings* Set = UPCAPToolSettings::Get();
              if (UMocapDatabase* DB = (Set ? Set->GetDatabase() : nullptr))
                  if (Weak.IsValid()) DB->SetVCamCalled(Weak->GetName(), State == ECheckBoxState::Checked);
          })
          [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ]

        + SVerticalBox::Slot().FillHeight(1.f)
        [ SNew(SScrollBox) + SScrollBox::Slot()[ DetailsView.ToSharedRef() ] ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SaveVCamAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("Open", "Open asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]()
              { if (Weak.IsValid() && DeleteVCamAsset(Weak.Get())) { CloseDetail(); ReloadVCams(); } return FReply::Handled(); }) ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
