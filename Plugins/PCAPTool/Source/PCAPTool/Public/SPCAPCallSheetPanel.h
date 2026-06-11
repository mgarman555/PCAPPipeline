#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;

/**
 * Call Sheet — the shoot-day prep tool. Left-rail workspace: Context (Project, Stages,
 * Shoot day) + Called out (Actors, Props). Selecting a rail item swaps the main pane.
 * Stages/Actors/Props host the existing database panels; the section IS the database.
 */
class PCAPTOOL_API SPCAPCallSheetPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPCallSheetPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    enum class ESection : uint8 { Project, Stages, ShootDay, Actors, Props };
    ESection Current = ESection::Actors;

    TSharedPtr<SBox> RailBox;
    TSharedPtr<SBox> ContentBox;

    void SelectSection(ESection S);
    TSharedRef<SWidget> BuildRail();
    TSharedRef<SWidget> BuildRailItem(ESection S, const FText& Label);
    TSharedRef<SWidget> BuildSectionContent(ESection S);
    TSharedRef<SWidget> BuildProjectSection();
    TSharedRef<SWidget> BuildShootDaySection();
    class UMocapDatabase* GetDB() const;

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
