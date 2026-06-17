#include "SPCAPHMCDatabasePanel.h"
#include "HMCRigEntry.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Engine/Texture2D.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "PCAPHMCDatabase"

#include "SPCAPPanelStyle.h"

namespace
{
    // Short card summary: the rig's type (= capture config) + its IP.
    FString RigSummary(UHMCRigEntry* R)
    {
        if (!R) return FString();
        const FString TypeStr = StaticEnum<ECaptureConfiguration>()->GetDisplayNameTextByValue((int64)R->Type).ToString();
        const FString IPStr   = R->IPAddress.IsEmpty() ? FString(TEXT("no IP")) : R->IPAddress;
        return FString::Printf(TEXT("%s · %s"), *TypeStr, *IPStr);
    }
}

void SPCAPHMCDatabasePanel::Construct(const FArguments& InArgs)
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
                        [ SNew(STextBlock).Text(LOCTEXT("Title", "HMC DATABASE")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f).VAlign(VAlign_Center)
                        [ SNew(SSearchBox).HintText(LOCTEXT("Filter", "Search rigs…")).OnTextChanged(this, &SPCAPHMCDatabasePanel::OnFilterChanged) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SBox).WidthOverride(170.f)
                          [ SNew(SEditableTextBox).HintText(LOCTEXT("NewHint", "+ new rig name  ↵")).OnTextCommitted(this, &SPCAPHMCDatabasePanel::OnNewRigCommitted) ] ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(6.f))
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(TileView, STileView<TWeakObjectPtr<UHMCRigEntry>>)
                        .ListItemsSource(&FilteredRigs)
                        .OnGenerateTile(this, &SPCAPHMCDatabasePanel::OnGenerateTile)
                        .OnSelectionChanged(this, &SPCAPHMCDatabasePanel::OnSelectionChanged)
                        .ItemWidth(150.f)
                        .ItemHeight(160.f)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("EmptyRigs", "No HMC rigs yet — type a name in the box above to create one."))
                        .ColorAndOpacity(FSlateColor(ColText2))
                        .Visibility_Lambda([this]() { return FilteredRigs.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                    ]
                ]
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(&ScrimBrush)
            .Visibility_Lambda([this]() { return SelectedRig.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseDetail(); return FReply::Handled(); })
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(400.f).MaxDesiredHeight(470.f)
                [ SAssignNew(DetailBox, SBox) ]
            ]
        ]
    ];

    ReloadRigs();
}

void SPCAPHMCDatabasePanel::ReloadRigs()
{
    AllRigs.Reset();
    TileThumbnails.Empty();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UHMCRigEntry::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Found)
        if (UHMCRigEntry* Entry = Cast<UHMCRigEntry>(AD.GetAsset()))
            AllRigs.Add(Entry);

    AllRigs.Sort([](const TWeakObjectPtr<UHMCRigEntry>& A, const TWeakObjectPtr<UHMCRigEntry>& B)
    { return A.IsValid() && B.IsValid() && A->RigName < B->RigName; });

    ApplyFilter();
}

void SPCAPHMCDatabasePanel::ApplyFilter()
{
    FilteredRigs.Reset();
    for (const TWeakObjectPtr<UHMCRigEntry>& Ptr : AllRigs)
    {
        if (!Ptr.IsValid()) continue;
        if (FilterText.IsEmpty() || Ptr->RigName.Contains(FilterText)) FilteredRigs.Add(Ptr);
    }
    if (TileView.IsValid()) TileView->RequestListRefresh();
}

UHMCRigEntry* SPCAPHMCDatabasePanel::CreateRigAsset(const FString& RigName)
{
    if (RigName.IsEmpty()) return nullptr;

    FString AssetName = RigName;
    AssetName.ReplaceInline(TEXT(" "), TEXT("_"));
    AssetName = ObjectTools::SanitizeObjectName(AssetName);

    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PCAPPaths::HMCRigsDir(), *AssetName);
    if (FPackageName::DoesPackageExist(PackageName)) return nullptr;

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) return nullptr;
    UHMCRigEntry* Entry = NewObject<UHMCRigEntry>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
    Entry->RigName = RigName;
    FAssetRegistryModule::AssetCreated(Entry);
    Package->MarkPackageDirty();
    FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    return Entry;
}

void SPCAPHMCDatabasePanel::SaveRigAsset(UHMCRigEntry* Entry)
{
    if (!Entry) return;
    if (UPackage* Package = Entry->GetPackage())
    {
        Package->MarkPackageDirty();
        FEditorFileUtils::PromptForCheckoutAndSave({ Package }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
    }
}

bool SPCAPHMCDatabasePanel::DeleteRigAsset(UHMCRigEntry* Entry)
{
    if (!Entry) return false;
    return ObjectTools::DeleteSingleObject(Entry, /*bPerformReferenceCheck*/ false);
}

TSharedRef<ITableRow> SPCAPHMCDatabasePanel::OnGenerateTile(TWeakObjectPtr<UHMCRigEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
    UHMCRigEntry* E = Item.Get();
    const FString NameText = E ? E->RigName : TEXT("(missing)");
    const FString Summary  = RigSummary(E);

    TSharedRef<SWidget> ThumbWidget =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];

    if (UObject* Asset = (E ? E->Thumbnail.LoadSynchronous() : nullptr))
    {
        TSharedPtr<FAssetThumbnail> Thumb = TileThumbnails.FindRef(Item);
        if (!Thumb.IsValid())
        {
            Thumb = MakeShared<FAssetThumbnail>(Asset, 96, 96, ThumbnailPool);
            TileThumbnails.Add(Item, Thumb);
        }
        ThumbWidget = Thumb->MakeThumbnailWidget();
    }

    return SNew(STableRow<TWeakObjectPtr<UHMCRigEntry>>, Owner)
        .Padding(4.f)
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).HeightOverride(86.f)[ ThumbWidget ] ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f, 0.f, 0.f)
                [ SNew(STextBlock).Text(FText::FromString(NameText)) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Summary)).ColorAndOpacity(FSlateColor(ColText2)).AutoWrapText(true).Justification(ETextJustify::Center) ]
            ]
        ];
}

void SPCAPHMCDatabasePanel::OnSelectionChanged(TWeakObjectPtr<UHMCRigEntry> Item, ESelectInfo::Type)
{
    SelectedRig = Item;
    if (Item.IsValid() && DetailBox.IsValid())
        DetailBox->SetContent(BuildDetailFor(Item.Get()));
}

void SPCAPHMCDatabasePanel::OnFilterChanged(const FText& Text)
{
    FilterText = Text.ToString();
    ApplyFilter();
}

void SPCAPHMCDatabasePanel::OnNewRigCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType != ETextCommit::OnEnter) return;
    const FString Name = Text.ToString().TrimStartAndEnd();
    if (Name.IsEmpty()) return;
    if (UHMCRigEntry* Created = CreateRigAsset(Name))
    {
        ReloadRigs();
        if (TileView.IsValid()) TileView->SetSelection(TWeakObjectPtr<UHMCRigEntry>(Created));
    }
}

void SPCAPHMCDatabasePanel::CloseDetail()
{
    SelectedRig = nullptr;
    if (TileView.IsValid()) TileView->ClearSelection();
}

TSharedRef<SWidget> SPCAPHMCDatabasePanel::MakeEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set)
{
    const FText CurrentText = EnumPtr ? EnumPtr->GetDisplayNameTextByValue((int64)Current) : FText::GetEmpty();
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(ColLabel)) ]
        + SHorizontalBox::Slot().FillWidth(0.6f)
        [
            SNew(SComboButton)
            .ButtonContent()[ SNew(STextBlock).Text(CurrentText) ]
            .OnGetMenuContent_Lambda([this, EnumPtr, Set]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(/*bCloseAfterSelection*/ true, nullptr);
                if (EnumPtr)
                    for (int32 i = 0; i < EnumPtr->NumEnums() - 1; ++i)   // skip the implicit _MAX
                    {
                        const int32 Val = (int32)EnumPtr->GetValueByIndex(i);
                        MB.AddMenuEntry(EnumPtr->GetDisplayNameTextByIndex(i), FText::GetEmpty(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Set, Val]()
                            {
                                Set(Val);
                                if (SelectedRig.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedRig.Get()));
                                if (TileView.IsValid()) TileView->RequestListRefresh();
                            })));
                    }
                return MB.MakeWidget();
            })
        ];
}

TSharedRef<SWidget> SPCAPHMCDatabasePanel::BuildDetailFor(UHMCRigEntry* Entry)
{
    if (!Entry) return SNew(SBox);

    TWeakObjectPtr<UHMCRigEntry> Weak(Entry);

    TSharedRef<SWidget> Thumb =
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("NoImg2", "no image")).ColorAndOpacity(FSlateColor(ColLabel)) ];
    if (UObject* Asset = Entry->Thumbnail.LoadSynchronous())
    {
        DetailThumbnail = MakeShared<FAssetThumbnail>(Asset, 110, 110, ThumbnailPool);
        Thumb = DetailThumbnail->MakeThumbnailWidget();
    }

    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(14.f)
    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })   // consume — don't close when clicking the card
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("RigHdr", "HMC Rig")).ColorAndOpacity(FSlateColor(ColGreen)) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
                  .OnClicked_Lambda([this]() { CloseDetail(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("Close", "X")).ColorAndOpacity(FSlateColor(ColText2)) ] ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
                [ SNew(SBox).WidthOverride(120.f).HeightOverride(120.f)[ Thumb ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("RigName", "Rig name")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SEditableTextBox).Text(FText::FromString(Entry->RigName)).OnTextCommitted_Lambda([this, Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) { Weak->RigName = T.ToString(); if (TileView.IsValid()) TileView->RequestListRefresh(); } }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
                    [ SNew(STextBlock).Text(LOCTEXT("Img", "Card image")).ColorAndOpacity(FSlateColor(ColLabel)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SObjectPropertyEntryBox).AllowedClass(UTexture2D::StaticClass()).DisplayThumbnail(false)
                      .ObjectPath_Lambda([Weak]() { return Weak.IsValid() ? Weak->Thumbnail.ToString() : FString(); })
                      .OnObjectChanged_Lambda([this, Weak](const FAssetData& AD) { if (Weak.IsValid()) { Weak->Thumbnail = TSoftObjectPtr<UTexture2D>(AD.GetSoftObjectPath()); if (SelectedRig.IsValid() && DetailBox.IsValid()) DetailBox->SetContent(BuildDetailFor(SelectedRig.Get())); } }) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 6.f)
            [ SNew(STextBlock).Text(LOCTEXT("RigCfg", "Rig setup")).ColorAndOpacity(FSlateColor(ColGreen)) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
            [ MakeEnumRow(TEXT("Type"), StaticEnum<ECaptureConfiguration>(), (int32)Entry->Type, [Weak](int32 V){ if (Weak.IsValid()) Weak->Type = (ECaptureConfiguration)V; }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(LOCTEXT("IP", "IP address")).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SEditableTextBox).Text(FText::FromString(Entry->IPAddress)).OnTextCommitted_Lambda([this, Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) { Weak->IPAddress = T.ToString(); if (TileView.IsValid()) TileView->RequestListRefresh(); } }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
            [ SNew(STextBlock).Text(LOCTEXT("Notes", "Notes")).ColorAndOpacity(FSlateColor(ColLabel)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SMultiLineEditableTextBox).Text(FText::FromString(Entry->Notes)).OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type){ if (Weak.IsValid()) Weak->Notes = T.ToString(); }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([Weak]() { if (Weak.IsValid()) SaveRigAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(SButton).Text(LOCTEXT("Open", "Open asset")).OnClicked_Lambda([Weak]() { if (Weak.IsValid() && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Weak.Get()); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Delete", "Delete")).OnClicked_Lambda([this, Weak]()
                  { if (Weak.IsValid() && DeleteRigAsset(Weak.Get())) { CloseDetail(); ReloadRigs(); } return FReply::Handled(); }) ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
