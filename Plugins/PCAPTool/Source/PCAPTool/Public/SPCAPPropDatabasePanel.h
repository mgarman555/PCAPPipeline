#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UPropRosterEntry;
class STableViewBase;
class ITableRow;
class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Prop Database — a card gallery of UPropRosterEntry assets. Cards show the prop's
 * mesh thumbnail + propID; clicking a card brings its info up in a focused detail
 * panel. Mirrors the Actor/Stage database card + click-panel pattern.
 */
class PCAPTOOL_API SPCAPPropDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPPropDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TWeakObjectPtr<UPropRosterEntry>> AllProps;
    TArray<TWeakObjectPtr<UPropRosterEntry>> FilteredProps;
    TWeakObjectPtr<UPropRosterEntry> SelectedProp;
    FString FilterText;

    TSharedPtr<STileView<TWeakObjectPtr<UPropRosterEntry>>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<TWeakObjectPtr<UPropRosterEntry>, TSharedPtr<FAssetThumbnail>> TileThumbnails;
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadProps();
    void ApplyFilter();
    static UPropRosterEntry* CreatePropAsset(const FString& PropID);
    static void SavePropAsset(UPropRosterEntry* Entry);
    static bool DeletePropAsset(UPropRosterEntry* Entry);

    TSharedRef<ITableRow> OnGenerateTile(TWeakObjectPtr<UPropRosterEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UPropRosterEntry> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewPropCommitted(const FText& Text, ETextCommit::Type CommitType);

    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(UPropRosterEntry* Entry);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
