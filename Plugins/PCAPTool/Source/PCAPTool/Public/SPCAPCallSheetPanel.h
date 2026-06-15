#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class FAssetThumbnail;
class FAssetThumbnailPool;
class UMocapDatabase;
struct FSlateCsvRow;
struct FShot;
struct FSession;
enum class EShotType : uint8;

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
    enum class ESection : uint8 { Overview, Project, Stages, ShootDay, Shots, Actors, Props };
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

    // Drop an APCAPVolumeVisualizer into the editor level, wired to the active stage config
    // (the stage FBX + live Vicon markers, to scale). Only includes the viz header — never
    // edits it. No-op (logs) when no active stage config.
    void SpawnVolumeVisualizer();

    // ── Shots (the day's slate list — built here in prep; run in the Operator Console) ──
    TSharedRef<SWidget> BuildShotsSection();
    // Returns the active day's working session, creating "S01" if the day has none
    // (when bCreate). Also points ActiveSessionID at it so the Operator Console follows.
    FSession* EnsureActiveSession(bool bCreate);
    void   AddShotBySlot(const FString& Slot);          // manual add; type inferred from slot
    FReply OnRemoveShot(FString ShotID);
    void   ImportShotsCsv();                            // file dialog → parse → merge into the day
    void   ExportShotsCsv();                            // gather the day's shots → CSV file
    void   ApplyRowsToActiveDay(const TArray<FSlateCsvRow>& Rows);  // merge rows into the session (by slot)
    FSlateCsvRow RowFromShot(const FShot& Shot) const;
    // Slot/type helpers (static members, not anon-namespace — unity-build safe).
    static FString   NormalizeSlot(const FString& Slot);            // "3" → "003", keeps "901"
    static EShotType ShotTypeFor(const FString& TypeText, const FString& Slot);
    static FString   ShotTypeName(EShotType Type);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
