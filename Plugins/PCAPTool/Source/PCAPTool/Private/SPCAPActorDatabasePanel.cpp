#include "SPCAPActorDatabasePanel.h"
#include "ActorRosterEntry.h"
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

#define LOCTEXT_NAMESPACE "PCAPActorDatabase"

#include "SPCAPPanelStyle.h"

void SPCAPActorDatabasePanel::Construct(const FArguments& InArgs)
{
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

    ChildSlot
    [
        SNew(SOverlay)

        // ── Gallery ────────────────────────────────────────────────────────
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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "ACTOR DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search actors…")).OnTextChanged(this, &SPCAPActorDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(160.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new actorID  ↵")).OnTextCommitted(this, &SPCAPActorDatabasePanel::OnNewActorCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SAssignNew(TileView, STileView<TWeakObjectPtr<UActorRosterEntry>>)
                    .ListItemsSource(&FilteredActors)
                    .OnGenerateTile(this, &SPCAPActorDatabasePanel::OnGenerateTile)
                    .OnSelectionChanged(this, &SPCAPActorDatabasePanel::OnSelectionChanged)
                    .ItemWidth(132.f)
                    .ItemHeight(154.f)
                    .SelectionMode(ESelectionMode::Single)
                ]
            ]
        ]

        // ── Detail popup (scrim + centered card) ───────────────────────────
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedActor.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(372.f).MaxDesiredHeight(448.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadActors();
}

// ── Data ────────────────────────────────────────────────────────────────────

void SPCAPActorDatabasePanel::ReloadActors()
{
    AllActors.Reset();
    TileThumbnails.Empty();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
        if (UActorRosterEntry* Entry = Cast<UActorRosterEntry>(AD.GetAsset()))
            AllActors.Add(Entry);

    AllActors.Sort([](const TWeakObjectPtr<UActorRosterEntry>& A, const TWeakObjectPtr<UActorRosterEntry>& B)
    { return A.IsValid() && B.IsValid() && A->ActorID < B->ActorID; });

    ApplyFilter();
}

void SPCAPActorDatabasePanel::ApplyFilter()
{
    FilteredActors.Reset();
    for (const TWeakObjectPtr<UActorRosterEntry>& Ptr : AllActors)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty() || Ptr->ActorID.Contains(FilterText) || Ptr->FirstName.Contains(FilterText) || Ptr->LastName.Contains(FilterText))
            FilteredActors.Add(Ptr);
    }
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

UActorRosterEntry* SPCAPActorDatabasePanel::CreateActorAsset(const FString& ActorID)
{
    if (ActorID.IsEmpty()) return nullptr;
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::ActorsDir(), *ActorID);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UActorRosterEntry* Entry = NewObject<UActorRosterEntry>(Package, FName(*ActorID), RF_Public | RF_Standalone | RF_Transactional);
    Entry->ActorID = ActorID;
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPActorDatabasePanel::SaveActorAsset(UActorRosterEntry* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPActorDatabasePanel::DeleteActorAsset(UActorRosterEntry* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

UObject* SPCAPActorDatabasePanel::ResolvePreview(UActorRosterEntry* Entry)
{
    if (!Entry) return nullptr;
    UObject* P = Entry->Headshot.LoadSynchronous();
    if (!P) P = Entry->MetaHuman.LoadSynchronous();
    if (!P) P = Entry->FaceScan.LoadSynchronous();
    return P;
}

// ── Gallery tiles ─────────────────────────────────────────────────────────

TSharedRef<ITableRow> SPCAPActorDatabasePanel::OnGenerateTile(TWeakObjectPtr<UActorRosterEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    UActorRosterEntry* E = Item.Get();
    const FString IDText   = E ? E->ActorID : TEXT("(missing)");
    const FString NameText = E ? FString::Printf(TEXT("%s %s"), *E->FirstName, *E->LastName).TrimStartAndEnd() : FString();

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (UObject* Asset = ResolvePreview(E))
    {
        TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item);
        if (!Thumb.IsValid())
        {
            Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
            TileThumbnails.Add(Item, Thumb);
        }
        ThumbWidget = Thumb->MakeThumbnailWidget();
    }

    return SNew(STableRow<TWeakObjectPtr<UActorRosterEntry>>, Owner)
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

void SPCAPActorDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UActorRosterEntry> Item, ESelectInfo::Type)
{
    SelectedActor = Item;
    if (Item.IsValid() && DetailBox.IsValid())
        DetailBox->SetContent(BuildDetailFor(Item.Get()));
}

void SPCAPActorDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPActorDatabasePanel::OnNewActorCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString ActorID = Text.ToString().TrimStartAndEnd();
    if (ActorID.IsEmpty()) return;
    if (UActorRosterEntry* Created = CreateActorAsset(ActorID))
    {
        ReloadActors();
        if (TileView.IsValid()) TileView->SetSelection(TWeakObjectPtr<UActorRosterEntry>(Created));
    }
}

FReply SPCAPActorDatabasePanel::OnRefreshClicked()
{
    ReloadActors();
    return FReply::Handled();
}

void SPCAPActorDatabasePanel::CloseDetail()
{
    SelectedActor = nullptr;
    if (TileView.IsValid()) TileView->ClearSelection();
}

// ── Detail card ───────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPActorDatabasePanel::BuildDetailFor(UActorRosterEntry* Entry)
{
    if (!Entry) return SNew(SBox);

    TWeakObjectPtr<UActorRosterEntry> Weak(Entry);

    auto Field = [](const FString& Label, const FString& Value, TFunction<void(const FString&)> Commit) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SEditableTextBox).Text(FText::FromString(Value)).OnTextCommitted_Lambda([Commit](const FText& T, ETextCommit::Type){ Commit(T.ToString()); }) ];
    };

    auto AssetSlot = [](const FString& Label, TFunction<FString()> Get, TFunction<void(const FAssetData&)> Set) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SObjectPropertyEntryBox).AllowedClass(UObject::StaticClass()).DisplayThumbnail(false)
              .ObjectPath_Lambda(MoveTemp(Get)).OnObjectChanged_Lambda(MoveTemp(Set)) ];
    };

    TSharedRef<SWidget> Head =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg2", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = ResolvePreview(Entry))
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 72, 72, ThumbnailPool);
        Head = DetailThumbnail->MakeThumbnailWidget();
    }

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(72.f).HeightOverride(72.f)[ Head ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Entry->ActorID)).ColorAndOpacity(FSlateColor(ColGreen)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(LOCTEXT("IdLocked", "id locked")).ColorAndOpacity(FSlateColor(ColText2)) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
                [ Field(TEXT("First name"), Entry->FirstName, [Weak](const FString& V){ if (Weak.IsValid()) Weak->FirstName = V; }) ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f, 0.f, 0.f)
                [ Field(TEXT("Last name"), Entry->LastName, [Weak](const FString& V){ if (Weak.IsValid()) Weak->LastName = V; }) ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [ AssetSlot(TEXT("MetaHuman"),
                [Weak]() { return Weak.IsValid() ? Weak->MetaHuman.ToString() : FString(); },
                [Weak](const FAssetData& AD) { if (Weak.IsValid()) Weak->MetaHuman = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); }) ]
            + SVerticalBox::Slot().AutoHeight()
            [ AssetSlot(TEXT("Face scan"),
                [Weak]() { return Weak.IsValid() ? Weak->FaceScan.ToString() : FString(); },
                [Weak](const FAssetData& AD) { if (Weak.IsValid()) Weak->FaceScan = TSoftObjectPtr<UObject>(AD.GetSoftObjectPath()); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
            [ SNew(SCheckBox)
              .IsChecked_Lambda([Weak]() { return (Weak.IsValid() && Weak->bUseFaceScanOnMetaHuman) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
              .OnCheckStateChanged_Lambda([Weak](ECheckBoxState S) { if (Weak.IsValid()) Weak->bUseFaceScanOnMetaHuman = (S == ECheckBoxState::Checked); })
              [ SNew(STextBlock).Text(LOCTEXT("UseFaceScan", "Use face scan on the MetaHuman")) ] ]
            + SVerticalBox::Slot().AutoHeight()
            [ AssetSlot(TEXT("Headshot (overrides preview)"),
                [Weak]() { return Weak.IsValid() ? Weak->Headshot.ToString() : FString(); },
                [this, Weak](const FAssetData& AD) { if (Weak.IsValid()) { Weak->Headshot = TSoftObjectPtr<UTexture2D>(AD.GetSoftObjectPath()); if (SelectedActor.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedActor.Get())); } }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
            [ SNew(SCheckBox)
              .IsChecked_Lambda([Weak]()
              {
                  UPCAPToolSettings* S = UPCAPToolSettings::Get();
                  UMocapDatabase* DB = S ? S->GetDatabase() : nullptr;
                  const FString ID = Weak.IsValid() ? Weak->ActorID : FString();
                  return (DB && DB->IsActorCalled(ID)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
              })
              .OnCheckStateChanged_Lambda([Weak](ECheckBoxState S)
              {
                  UPCAPToolSettings* Set = UPCAPToolSettings::Get();
                  if (UMocapDatabase* DB = (Set ? Set->GetDatabase() : nullptr))
                      if (Weak.IsValid()) DB->SetActorCalled(Weak->ActorID, S == ECheckBoxState::Checked);
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
                [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SaveActorAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Open", "Open asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]()
                  { if (Weak.IsValid() && DeleteActorAsset(Weak.Get())) { CloseDetail(); ReloadActors(); } return FReply::Handled(); }) ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
