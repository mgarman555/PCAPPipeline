#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UActorRosterEntry;
class STableViewBase;
class ITableRow;
class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Actor Database — a modern card gallery of UActorRosterEntry assets. Cards show the
 * headshot (→ MetaHuman → face-scan fallback) + actorID; clicking a card brings its
 * info up in a focused detail panel for editing. The same card + click-panel pattern
 * is mirrored by the Prop and Stage databases.
 */
class PCAPTOOL_API SPCAPActorDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPActorDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TWeakObjectPtr<UActorRosterEntry>> AllActors;
    TArray<TWeakObjectPtr<UActorRosterEntry>> FilteredActors;
    TWeakObjectPtr<UActorRosterEntry> SelectedActor;   // valid → detail panel open
    FString FilterText;

    TSharedPtr<STileView<TWeakObjectPtr<UActorRosterEntry>>> TileView;
    TSharedPtr<SBox> DetailBox;                          // holds the selected actor's detail card
    TSharedPtr<SBox> StreamingBox;                       // "streaming now, untracked" capture strip
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TMap<TWeakObjectPtr<UActorRosterEntry>, TSharedPtr<FAssetThumbnail>> TileThumbnails;   // keeps card thumbs alive
    TSharedPtr<FAssetThumbnail> DetailThumbnail;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadActors();
    void ApplyFilter();
    static UActorRosterEntry* CreateActorAsset(const FString& ActorID);
    static void SaveActorAsset(UActorRosterEntry* Entry);
    static bool DeleteActorAsset(UActorRosterEntry* Entry);

    TSharedRef<ITableRow> OnGenerateTile(TWeakObjectPtr<UActorRosterEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UActorRosterEntry> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewActorCommitted(const FText& Text, ETextCommit::Type CommitType);
    FReply OnRefreshClicked();

    void CloseDetail();
    void RefreshStreaming();                                      // rebuild the untracked-subjects strip
    EActiveTimerReturnType TickStreaming(double InCurrentTime, float InDeltaTime);
    TSharedRef<SWidget> BuildDetailFor(UActorRosterEntry* Entry);
    static UObject* ResolvePreview(UActorRosterEntry* Entry);   // headshot ?? MetaHuman ?? face scan

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
