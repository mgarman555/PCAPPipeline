#include "SPCAPPropDatabasePanel.h"
#include "PropRosterEntry.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
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
#include "PropertyCustomizationHelpers.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPPropDatabase"

#include "SPCAPPanelStyle.h"

void SPCAPPropDatabasePanel::Construct(const FArguments& InArgs)
{
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(24);

    ChildSlot
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
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh")).OnClicked(this, &SPCAPPropDatabasePanel::OnRefreshClicked) ]
                ]
            ]

            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(0.42f).Padding(FMargin(6.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Filter props…")).OnTextChanged(this, &SPCAPPropDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
                        [
                            SNew(SBox).WidthOverride(160.f)
                            [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new propID  ↵")).OnTextCommitted(this, &SPCAPPropDatabasePanel::OnNewPropCommitted) ]
                        ]
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)
                    [
                        SAssignNew(ListView, SListView<TWeakObjectPtr<UPropRosterEntry>>)
                        .ListItemsSource(&FilteredProps)
                        .OnGenerateRow(this, &SPCAPPropDatabasePanel::OnGenerateRow)
                        .OnSelectionChanged(this, &SPCAPPropDatabasePanel::OnSelectionChanged)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                ]

                + SHorizontalBox::Slot().FillWidth(0.58f).Padding(FMargin(6.f))
                [
                    SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(10.f))
                    [ SAssignNew(FormContainer, SBox) ]
                ]
            ]
        ]
    ];

    ReloadProps();
    RebuildForm();
}

void SPCAPPropDatabasePanel::ReloadProps()
{
    AllProps.Reset();
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
    {
        if (UPropRosterEntry* Entry = Cast<UPropRosterEntry>(AD.GetAsset())) AllProps.Add(Entry);
    }
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
        {
            FilteredProps.Add(Ptr);
        }
    }
    if (ListView.IsValid()) ListView->RequestListRefresh();
}

UPropRosterEntry* SPCAPPropDatabasePanel::CreatePropAsset(const FString& PropID)
{
    if (PropID.IsEmpty()) return nullptr;
    const FString PackageName = FString::Printf(TEXT("/Game/Mocap/_Roster/Props/%s"), *PropID);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UPropRosterEntry* Entry = NewObject<UPropRosterEntry>(Package, FName(*PropID), RF_Public | RF_Standalone | RF_Transactional);
    Entry->PropID = PropID;
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, false, false);
    return Entry;
}

void SPCAPPropDatabasePanel::SavePropAsset(UPropRosterEntry* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, false, false);
    }
}

bool SPCAPPropDatabasePanel::DeletePropAsset(UPropRosterEntry* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPPropDatabasePanel::OnGenerateRow(TWeakObjectPtr<UPropRosterEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    UPropRosterEntry* E = Item.Get();
    const FString IDText   = E ? E->PropID : TEXT("(missing)");
    const FString NameText = E ? E->DisplayName : FString();
    return SNew(STableRow<TWeakObjectPtr<UPropRosterEntry>>, Owner)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 4.f, 2.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(IDText)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 2.f, 4.f)
        [ SNew(STextBlock).Text(FText::FromString(NameText)).ColorAndOpacity(FSlateColor(ColText2)) ]
    ];
}

void SPCAPPropDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UPropRosterEntry> Item, ESelectInfo::Type)
{
    SelectedProp = Item;
    RebuildForm();
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
        SelectedProp = Created;
        if (ListView.IsValid()) ListView->SetSelection(TWeakObjectPtr<UPropRosterEntry>(Created));
        RebuildForm();
    }
}

FReply SPCAPPropDatabasePanel::OnRefreshClicked()
{
    ReloadProps();
    RebuildForm();
    return FReply::Handled();
}

void SPCAPPropDatabasePanel::RebuildForm()
{
    if (FormContainer.IsValid()) FormContainer->SetContent(BuildFormFor(SelectedProp.Get()));
}

TSharedRef<SWidget> SPCAPPropDatabasePanel::BuildFormFor(UPropRosterEntry* Entry)
{
    if (!Entry)
    {
        return SNew(STextBlock).Text(LOCTEXT("NoSel", "Select a prop, or + New to create one.")).ColorAndOpacity(FSlateColor(ColText2));
    }

    TWeakObjectPtr<UPropRosterEntry> Weak(Entry);

    // Real mesh thumbnail (FAssetThumbnail), or a placeholder when no asset is set.
    TSharedRef<SWidget> ThumbWidget =
        SNew(SBox).WidthOverride(130.f).HeightOverride(130.f)
        [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
          [ SNew(STextBlock).Text(LOCTEXT("NoMesh", "no mesh")).ColorAndOpacity(FSlateColor(ColLabel)) ] ];
    if (UObject* AssetObj = Entry->PropAsset.LoadSynchronous())
    {
        CurrentThumbnail = MakeShared<FAssetThumbnail>(AssetObj, 128, 128, ThumbnailPool);
        ThumbWidget = SNew(SBox).WidthOverride(130.f).HeightOverride(130.f)[ CurrentThumbnail->MakeThumbnailWidget() ];
    }

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   (id locked)"), *Entry->PropID))).ColorAndOpacity(FSlateColor(ColGreen)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Disp", "Display name")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text(FText::FromString(Entry->DisplayName)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->DisplayName = T.ToString(); }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f) [ ThumbWidget ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                [ SNew(STextBlock).Text(LOCTEXT("Mesh", "Prop mesh / asset")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UObject::StaticClass())
                    .DisplayThumbnail(false)
                    .ObjectPath_Lambda([Weak]() { return Weak.IsValid() ? Weak->PropAsset.ToString() : FString(); })
                    .OnObjectChanged_Lambda([this, Weak](const FAssetData& AD) { if (Weak.IsValid()) { Weak->PropAsset = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); RebuildForm(); } })
                ]
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
        [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Entry->Notes)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->Notes = T.ToString(); }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SavePropAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
            [ SNew(SButton).Text(LOCTEXT("OpenFull", "Open full asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]() { if (Weak.IsValid() && DeletePropAsset(Weak.Get())) { SelectedProp = nullptr; ReloadProps(); RebuildForm(); } return FReply::Handled(); }) ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
