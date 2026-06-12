#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;
class UMocapDatabase;

/**
 * Call Sheet — the shoot-day prep tool. Left-rail workspace:
 *   Overview (the day's callout at a glance) / Context (Project, Stages, Shoot day) /
 *   Called out (Actors, Props). Selecting a rail item swaps the main pane.
 * Stages/Actors/Props host the database panels; the section IS the database.
 */
class PCAPTOOL_API SPCAPCallSheetPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPCallSheetPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    enum class ESection : uint8 { Overview, Project, Stages, ShootDay, Actors, Props };
    ESection Current = ESection::Overview;

    TSharedPtr<SBox> RailBox;
    TSharedPtr<SBox> ContentBox;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> OverviewThumbnails;   // kept alive while the overview is shown

    void SelectSection(ESection S);
    TSharedRef<SWidget> BuildRail();
    TSharedRef<SWidget> BuildRailItem(ESection S, const FText& Label);
    TSharedRef<SWidget> BuildSectionContent(ESection S);
    TSharedRef<SWidget> BuildOverviewSection();
    TSharedRef<SWidget> BuildProjectSection();
    TSharedRef<SWidget> BuildShootDaySection();
    UMocapDatabase* GetDB() const;

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
