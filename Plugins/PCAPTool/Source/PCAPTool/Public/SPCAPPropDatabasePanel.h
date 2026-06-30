#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"
#include "PCAPMocapData.h"   // FPCAPPropInfo

class UPCAPPropExtension;
class STableViewBase;
class ITableRow;
class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Prop Database — a card gallery over Epic's UPCapPropDataAsset (canonical), read
 * by reflection via UPCAPMocapData. Epic owns name / Live Link subject / meshes;
 * the paired UPCAPPropExtension holds PCAPTool extras (history, status, notes).
 * Mirrors the Performer Database.
 */
class PCAPTOOL_API SPCAPPropDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPPropDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    using FPropPtr = TSharedPtr<FPCAPPropInfo>;

    TArray<FPropPtr> AllProps;
    TArray<FPropPtr> FilteredProps;
    FPropPtr SelectedProp;
    FString FilterText;

    TSharedPtr<STileView<FPropPtr>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<FGuid, TSharedPtr<FAssetThumbnail>> TileThumbnails;   // keyed by prop AssetUID
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadProps();
    void ApplyFilter();

    TSharedRef<ITableRow> OnGenerateTile(FPropPtr Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(FPropPtr Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewPropCommitted(const FText& Text, ETextCommit::Type CommitType);

    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(FPropPtr Info);
    static UObject* ResolvePreview(const FPCAPPropInfo& Info);

    static const TCHAR* PropPackageDir() { return TEXT("/Game/PCAPTool/PCap/Props"); }

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
