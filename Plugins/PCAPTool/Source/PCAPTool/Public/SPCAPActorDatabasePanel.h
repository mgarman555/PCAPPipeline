#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UActorRosterEntry;
class STableViewBase;
class ITableRow;
class SBox;
class SSearchBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Actor Database — the permanent talent library (Tool 1 of the operator suite).
 * Two-pane: a searchable list of UActorRosterEntry assets on the left, the selected
 * actor's setup form on the right. "+ New actor" creates Content/Mocap/_Roster/Actors/
 * [actorID].uasset. ActorID is locked after creation (the permanent key).
 * The operator console references talent by ActorID, resolved from here.
 */
class PCAPTOOL_API SPCAPActorDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPActorDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // Full set + the filtered view that feeds the list.
    TArray<TWeakObjectPtr<UActorRosterEntry>> AllActors;
    TArray<TWeakObjectPtr<UActorRosterEntry>> FilteredActors;
    TWeakObjectPtr<UActorRosterEntry> SelectedActor;
    FString FilterText;

    TSharedPtr<SListView<TWeakObjectPtr<UActorRosterEntry>>> ListView;
    TSharedPtr<SBox> FormContainer;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TSharedPtr<FAssetThumbnail> CurrentThumbnail;   // headshot ?? MetaHuman ?? face scan

    // ── Data ────────────────────────────────────────────────────────────────
    void ReloadActors();                 // pull all UActorRosterEntry via the asset registry
    void ApplyFilter();                  // rebuild FilteredActors from FilterText
    static UActorRosterEntry* CreateActorAsset(const FString& ActorID);   // new .uasset
    static void SaveActorAsset(UActorRosterEntry* Entry);
    static bool DeleteActorAsset(UActorRosterEntry* Entry);

    // ── List ────────────────────────────────────────────────────────────────
    TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<UActorRosterEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UActorRosterEntry> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);

    // ── Right pane (form) ─────────────────────────────────────────────────────
    void RebuildForm();
    TSharedRef<SWidget> BuildFormFor(UActorRosterEntry* Entry);

    // ── Actions ──────────────────────────────────────────────────────────────
    void OnNewActorCommitted(const FText& Text, ETextCommit::Type CommitType);   // type ActorID + Enter
    FReply OnRefreshClicked();

    // Palette (matches the other PCAP panels).
    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
