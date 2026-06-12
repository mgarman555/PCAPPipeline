#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UStageConfigAsset;
class STableViewBase;
class ITableRow;
class SBox;
class UEnum;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Stage Database — a card gallery of UStageConfigAsset assets. Each card shows the
 * stage reference mesh (the physical layout) + the stage name + a tool summary, so
 * you can see at a glance which stage uses which capture systems. Clicking a card
 * opens a detail panel: the mesh, the systems (body/face/audio/vcam), timecode, etc.
 */
class PCAPTOOL_API SPCAPStageDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPStageDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TWeakObjectPtr<UStageConfigAsset>> AllStages;
    TArray<TWeakObjectPtr<UStageConfigAsset>> FilteredStages;
    TWeakObjectPtr<UStageConfigAsset> SelectedStage;
    FString FilterText;

    TSharedPtr<STileView<TWeakObjectPtr<UStageConfigAsset>>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<TWeakObjectPtr<UStageConfigAsset>, TSharedPtr<FAssetThumbnail>> TileThumbnails;
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadStages();
    void ApplyFilter();
    static UStageConfigAsset* CreateStageAsset(const FString& StageName);
    static void SaveStageAsset(UStageConfigAsset* Entry);
    static bool DeleteStageAsset(UStageConfigAsset* Entry);

    TSharedRef<ITableRow> OnGenerateTile(TWeakObjectPtr<UStageConfigAsset> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UStageConfigAsset> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewStageCommitted(const FText& Text, ETextCommit::Type CommitType);

    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(UStageConfigAsset* Entry);
    TSharedRef<SWidget> MakeEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
