#include "SPCAPPropDatabasePanel.h"
#include "PropRosterEntry.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolPaths.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPPropDatabase"

#include "SPCAPPanelStyle.h"

void SPCAPPropDatabasePanel::Construct(const FArguments& InArgs)
{
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "PROP DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search props…")).OnTextChanged(this, &SPCAPPropDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(160.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new propID  ↵")).OnTextCommitted(this, &SPCAPPropDatabasePanel::OnNewPropCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SAssignNew(TileView, STileView<TWeakObjectPtr<UPropRosterEntry>>)
                    .ListItemsSource(&FilteredProps)
                    .OnGenerateTile(this, &SPCAPPropDatabasePanel::OnGenerateTile)
                    .OnSelectionChanged(this, &SPCAPPropDatabasePanel::OnSelectionChanged)
                    .ItemWidth(132.f)
                    .ItemHeight(154.f)
                    .SelectionMode(ESelectionMode::Single)
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedProp.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(372.f).MaxDesiredHeight(448.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadProps();
}

void SPCAPPropDatabasePanel::ReloadProps()
{
    AllProps.Reset();
    TileThumbnails.Empty();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
        if (UPropRosterEntry* Entry = Cast<UPropRosterEntry>(AD.GetAsset()))
            AllProps.Add(Entry);

    AllProps.Sort([](const TWeakObjectPtr<UPropRosterEntry>& A, const TWeakObjectPtr<UPropRosterEntry>& B)
    { return A.IsValid() && B.IsValid() && A->PropID < B->PropID; });

    ApplyFilter();
}

void SPCAPPropDatabasePanel::ApplyFilter()
{
    FilteredProps.Reset();
    for (const TWeakObjectPtr<UPropRosterEntry>& Ptr : AllProps)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty() || Ptr->PropID.Contains(FilterText) || Ptr->DisplayName.Contains(FilterText))
            FilteredProps.Add(Ptr);
    }
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

UPropRosterEntry* SPCAPPropDatabasePanel::CreatePropAsset(const FString& PropID)
{
    if (PropID.IsEmpty()) return nullptr;
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::PropsDir(), *PropID);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UPropRosterEntry* Entry = NewObject<UPropRosterEntry>(Package, FName(*PropID), RF_Public | RF_Standalone | RF_Transactional);
    Entry->PropID = PropID;
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPPropDatabasePanel::SavePropAsset(UPropRosterEntry* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPPropDatabasePanel::DeletePropAsset(UPropRosterEntry* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPPropDatabasePanel::OnGenerateTile(TWeakObjectPtr<UPropRosterEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    UPropRosterEntry* E = Item.Get();
    const FString IDText   = E ? E->PropID : TEXT("(missing)");
    const FString NameText = E ? E->DisplayName : FString();

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh", "no mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (UObject* Asset = (E ? E->PropAsset.LoadSynchronous() : nullptr))
    {
        TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item);
        if (!Thumb.IsValid())
        {
            Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
            TileThumbnails.Add(Item, Thumb);
        }
        ThumbWidget = Thumb->MakeThumbnailWidget();
    }

    return SNew(STableRow<TWeakObjectPtr<UPropRosterEntry>>, Owner)
        .Padding(4.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).HeightOverride(88.f)[ ThumbWidget ] ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f, 0.f, 0.f)
                [ SNew(STextBlock).Text(FText::FromString(IDText)) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(NameText)).ColorAndOpacity(FSlateColor(ColText2)) ]
            ]
        ];
}

void SPCAPPropDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UPropRosterEntry> Item, ESelectInfo::Type)
{
    SelectedProp = Item;
    if (Item.IsValid() && DetailBox.IsValid())
        DetailBox->SetContent(BuildDetailFor(Item.Get()));
}

void SPCAPPropDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPPropDatabasePanel::OnNewPropCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString PropID = Text.ToString().TrimStartAndEnd();
    if (PropID.IsEmpty()) return;
    if (UPropRosterEntry* Created = CreatePropAsset(PropID))
    {
        ReloadProps();
        if (TileView.IsValid()) TileView->SetSelection(TWeakObjectPtr<UPropRosterEntry>(Created));
    }
}

void SPCAPPropDatabasePanel::CloseDetail()
{
    SelectedProp = nullptr;
    if (TileView.IsValid()) TileView->ClearSelection();
}

TSharedRef<SWidget> SPCAPPropDatabasePanel::BuildDetailFor(UPropRosterEntry* Entry)
{
    if (!Entry) return SNew(SBox);

    TWeakObjectPtr<UPropRosterEntry> Weak(Entry);

    TSharedRef<SWidget> Thumb =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoMesh2", "no mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = Entry->PropAsset.LoadSynchronous())
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
        Thumb = DetailThumbnail->MakeThumbnailWidget();
    }

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   (id locked)"), *Entry->PropID))).ColorAndOpacity(FSlateColor(ColGreen)) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(110.f).HeightOverride(110.f)[ Thumb ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("Disp", "Display name")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SEditableTextBox).Text(FText::FromString(Entry->DisplayName)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->DisplayName = T.ToString(); }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("Mesh", "Prop mesh / asset")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SObjectPropertyEntryBox).AllowedClass(UObject::StaticClass()).DisplayThumbnail(false)
                      .ObjectPath_Lambda([Weak]() { return Weak.IsValid() ? Weak->PropAsset.ToString() : FString(); })
                      .OnObjectChanged_Lambda([this, Weak](const FAssetData& AD) { if (Weak.IsValid()) { Weak->PropAsset = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); if (SelectedProp.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedProp.Get())); } }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
            [ SNew(SCheckBox)
              .IsChecked_Lambda([Weak]()
              {
                  UPCAPToolSettings* S = UPCAPToolSettings::Get();
                  UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
                  const FString ID = Weak.IsValid() ? Weak->PropID : FString();
                  return (DB && DB->IsPropCalled(ID)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
              })
              .OnCheckStateChanged_Lambda([Weak](ECheckBoxState S)
              {
                  UPCAPToolSettings* Set = UPCAPToolSettings::Get();
                  if (UMocapDatabase* DB = (Set ? Set->GetDatabase() : nullptr))
                      if (Weak.IsValid()) DB->SetPropCalled(Weak->PropID, S == ECheckBoxState::Checked);
              })
              [ SNew(STextBlock).Text(LOCTEXT("Call", "Called to today's shoot")) ] ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Entry->Notes)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->Notes = T.ToString(); }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SavePropAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Open", "Open asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]()
                  { if (Weak.IsValid() && DeletePropAsset(Weak.Get())) { CloseDetail(); ReloadProps(); } return FReply::Handled(); }) ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
