#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "Brushes/SlateColorBrush.h"

class UMocapDatabase;
class STableViewBase;
class ITableRow;
class SBox;

/**
 * Production Database — a card library over the master DB's productions (FProduction
 * structs; not separate assets, so no data migration). Browse / create / rename /
 * set-active. Mirrors the other DB card + scrim-popup panels, built on SPCAPRosterCard.
 */
class PCAPTOOL_API SPCAPProductionDatabasePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPProductionDatabasePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TArray<TSharedPtr<FString>> Codes;          // tile source — project codes (sorted)
    TArray<TSharedPtr<FString>> FilteredCodes;
    TSharedPtr<FString> SelectedCode;
    FString FilterText;

    TSharedPtr<STileView<TSharedPtr<FString>>> TileView;
    TSharedPtr<SBox> DetailBox;
    FSlateColorBrush ScrimBrush = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.5f));

    UMocapDatabase* GetDB() const;
    void Reload();
    void ApplyFilter();

    TSharedRef<ITableRow> OnGenerateTile(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner);
    void OnFilterChanged(const FText& Text);
    void OnNewCommitted(const FText& Text, ETextCommit::Type CommitType);
    void OpenDetail(const FString& Code);
    void CloseDetail();
    TSharedRef<SWidget> BuildDetailFor(const FString& Code);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
