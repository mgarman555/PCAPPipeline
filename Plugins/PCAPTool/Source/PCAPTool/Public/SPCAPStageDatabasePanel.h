#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UStageConfigAsset;
class STableViewBase;
class ITableRow;
class SBox;
class UEnum;

/**
 * Stage Database — the stage library (Tool 3). A stage is a physical location (e.g.
 * "Osborne Stage") plus a per-element checklist of which capture system is used:
 * body (Giant/Xsens/Motive/Shogun), face, audio, vcam — "what we're looking to record".
 * Backed by UStageConfigAsset at _Roster/StageConfigs/[name].uasset.
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

    TSharedPtr<SListView<TWeakObjectPtr<UStageConfigAsset>>> ListView;
    TSharedPtr<SBox> FormContainer;

    void ReloadStages();
    void ApplyFilter();
    static UStageConfigAsset* CreateStageAsset(const FString& StageName);
    static void SaveStageAsset(UStageConfigAsset* Entry);
    static bool DeleteStageAsset(UStageConfigAsset* Entry);

    TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<UStageConfigAsset> Item, const TSharedRef<STableViewBase>& Owner);
    void OnSelectionChanged(TWeakObjectPtr<UStageConfigAsset> Item, ESelectInfo::Type);
    void OnFilterChanged(const FText& Text);
    void OnNewStageCommitted(const FText& Text, ETextCommit::Type CommitType);
    FReply OnRefreshClicked();

    void RebuildForm();
    TSharedRef<SWidget> BuildFormFor(UStageConfigAsset* Entry);
    TSharedRef<SWidget> MakeEnumRow(const FString& Label, UEnum* EnumPtr, int32 Current, TFunction<void(int32)> Set);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
