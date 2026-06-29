#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"
#include "PCAPMocapData.h"   // FPCAPPerformerInfo

class UPCAPPerformerExtension;
class STableViewBase;
class ITableRow;
class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Actor / Performer Database — a card gallery over Epic's Performance Capture
 * performer assets (UPCapPerformerDataAsset, canonical), read by reflection via
 * UPCAPMocapData. Epic owns the core fields (name, Live Link subject, mesh); the
 * paired UPCAPPerformerExtension holds PCAPTool's extras (face/HMC, audio,
 * digital-double) and is editable in the detail panel. "+ new" creates an Epic
 * performer asset.
 */
class PCAPTOOL_API SPCAPActorDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPActorDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    using FPerformerPtr = TSharedPtr<FPCAPPerformerInfo>;

    TArray<FPerformerPtr> AllPerformers;
    TArray<FPerformerPtr> FilteredPerformers;
    FPerformerPtr SelectedPerformer;            // valid → detail panel open
    FString FilterText;

    TSharedPtr<STileView<FPerformerPtr>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<FGuid, TSharedPtr<FAssetThumbnail>> TileThumbnails;   // keyed by performer AssetUID
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadPerformers();
    void ApplyFilter();

    TSharedRef<ITableRow> OnGenerateTile(FPerformerPtr Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(FPerformerPtr Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewPerformerCommitted(const FText& Text, ETextCommit::Type CommitType);

    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(FPerformerPtr Info);
    static UObject* ResolvePreview(const FPCAPPerformerInfo& Info, UPCAPPerformerExtension* Ext);

    // Default content path for new Epic performer assets created from this panel.
    static const TCHAR* PerformerPackageDir() { return TEXT("/Game/PCAPTool/PCap/Performers"); }

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
