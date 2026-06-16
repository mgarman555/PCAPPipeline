#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UPCAPVCamConfig;
class STableViewBase;
class ITableRow;
class SBox;
class IDetailsView;

/**
 * VCam Database — the 4th library: a card gallery of UPCAPVCamConfig assets.
 * Mirrors the Actor/Prop/Stage card + scrim-popup pattern, built on the shared
 * SPCAPRosterCard; the detail popup is an IDetailsView (the config has many fields).
 */
class PCAPTOOL_API SPCAPVCamDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPVCamDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TWeakObjectPtr<UPCAPVCamConfig>> AllVCams;
    TArray<TWeakObjectPtr<UPCAPVCamConfig>> FilteredVCams;
    TWeakObjectPtr<UPCAPVCamConfig> SelectedVCam;
    FString FilterText;

    TSharedPtr<STileView<TWeakObjectPtr<UPCAPVCamConfig>>> TileView;
    TSharedPtr<SBox> DetailBox;
    TSharedPtr<IDetailsView> DetailsView;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    void ReloadVCams();
    void ApplyFilter();
    static UPCAPVCamConfig* CreateVCamAsset(const FString& VCamID);
    static void SaveVCamAsset(UPCAPVCamConfig* Entry);
    static bool DeleteVCamAsset(UPCAPVCamConfig* Entry);

    TSharedRef<ITableRow> OnGenerateTile(TWeakObjectPtr<UPCAPVCamConfig> Item, const TSharedRef<STableViewBase>& Owner);
    void OnFilterChanged(const FText& Text);
    void OnNewVCamCommitted(const FText& Text, ETextCommit::Type CommitType);
    void OpenDetail(UPCAPVCamConfig* Entry);
    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(UPCAPVCamConfig* Entry);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
