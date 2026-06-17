#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UHMCRigEntry;
class STableViewBase;
class ITableRow;
class SBox;
class UEnum;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * HMC Database — a card gallery of UHMCRigEntry assets (physical rigs). Each card shows
 * the rig name + type + IP; clicking a card opens a detail panel to edit them. The rig's
 * Type is its capture configuration, so it rides along when the rig is used. Mirrors
 * SPCAPStageDatabasePanel.
 */
class PCAPTOOL_API SPCAPHMCDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPHMCDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TWeakObjectPtr<UHMCRigEntry>> AllRigs;
    TArray<TWeakObjectPtr<UHMCRigEntry>> FilteredRigs;
    TWeakObjectPtr<UHMCRigEntry> SelectedRig;
    FString FilterText;

    TSharedPtr<STileView<TWeakObjectPtr<UHMCRigEntry>>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<TWeakObjectPtr<UHMCRigEntry>, TSharedPtr<FAssetThumbnail>> TileThumbnails;
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadRigs();
    void ApplyFilter();
    static UHMCRigEntry* CreateRigAsset(const FString& RigName);
    static void SaveRigAsset(UHMCRigEntry* Entry);
    static bool DeleteRigAsset(UHMCRigEntry* Entry);

    TSharedRef<ITableRow> OnGenerateTile(TWeakObjectPtr<UHMCRigEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UHMCRigEntry> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewRigCommitted(const FText& Text, ETextCommit::Type CommitType);

    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(UHMCRigEntry* Entry);
    TSharedRef<SWidget> MakeEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
