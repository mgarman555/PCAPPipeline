#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UMocapDatabase;
class IDetailsView;
struct FSlateCsvRow;
struct FShot;
struct FSession;
enum class EShotType : uint8;

/**
 * Call Sheet — the shoot-day prep tool, as a single scrollable page:
 *   header (Production / Day / Stage pickers + readiness + spawn-viz) →
 *   stage setup → called Actors / Props / VCam (chips + "+ call" picker) → Shots.
 * Calls out from the Actor/Prop/Stage/VCam libraries; management lives in their
 * Database tabs. The Production picker reads the Production Database.
 */
class PCAPTOOL_API SPCAPCallSheetPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPCallSheetPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SBox> SheetBox;                    // the single scrollable page
    TSharedPtr<IDetailsView> StageDetailsView;    // inline editor for the called stage's setup

    UMocapDatabase* GetDB() const;
    void RebuildSheet();                          // re-render the whole page (replaces section-switching)

    TSharedRef<SWidget> BuildSheet();
    TSharedRef<SWidget> BuildHeader();            // production/day/stage pickers + readiness + spawn-viz
    TSharedRef<SWidget> BuildStageArea();         // stage dropdown + editable setup
    // One shared called-section builder, type-erased via lambdas (actors/props/vcam).
    TSharedRef<SWidget> BuildCallSection(const FText& Title,
        const TArray<TPair<FString, FString>>& Items,
        TFunction<bool(const FString&)> IsCalled,
        TFunction<void(const FString&, bool)> SetCalled);
    TArray<TPair<FString, FString>> GatherActors() const;   // id, display
    TArray<TPair<FString, FString>> GatherProps() const;
    TArray<TPair<FString, FString>> GatherVCams() const;

    // Drop an APCAPVolumeVisualizer into the level for the active stage (header-only include).
    void SpawnVolumeVisualizer();

    // ── Shots (the day's slate list; built here in prep, run in the Operator Console) ──
    TSharedRef<SWidget> BuildShotsSection();
    FSession* EnsureActiveSession(bool bCreate);
    void   AddShotBySlot(const FString& Slot);
    FReply OnRemoveShot(FString ShotID);
    void   ImportShotsCsv();
    void   ExportShotsCsv();
    void   ApplyRowsToActiveDay(const TArray<FSlateCsvRow>& Rows);
    FSlateCsvRow RowFromShot(const FShot& Shot) const;
    static FString   NormalizeSlot(const FString& Slot);
    static EShotType ShotTypeFor(const FString& TypeText, const FString& Slot);
    static FString   ShotTypeName(EShotType Type);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
