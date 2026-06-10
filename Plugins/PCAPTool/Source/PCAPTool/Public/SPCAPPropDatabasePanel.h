#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UPropRosterEntry;
class STableViewBase;
class ITableRow;
class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;

/**
 * Prop Database — the prop library (Tool 2). Two-pane: searchable list of UPropRosterEntry
 * assets + the selected prop's setup form. The prop's mesh/asset renders as a real
 * FAssetThumbnail so the operator can confirm it's the right model. No "tracked" toggle —
 * tracking is a per-shot decision. "+ new propID + Enter" creates _Roster/Props/[propID].uasset.
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

    TSharedPtr<SListView<TWeakObjectPtr<UPropRosterEntry>>> ListView;
    TSharedPtr<SBox> FormContainer;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TSharedPtr<FAssetThumbnail> CurrentThumbnail;   // kept alive while shown

    void ReloadProps();
    void ApplyFilter();
    static UPropRosterEntry* CreatePropAsset(const FString& PropID);
    static void SavePropAsset(UPropRosterEntry* Entry);
    static bool DeletePropAsset(UPropRosterEntry* Entry);

    TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<UPropRosterEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UPropRosterEntry> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewPropCommitted(const FText& Text, ETextCommit::Type CommitType);
    FReply OnRefreshClicked();

    void RebuildForm();
    TSharedRef<SWidget> BuildFormFor(UPropRosterEntry* Entry);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
