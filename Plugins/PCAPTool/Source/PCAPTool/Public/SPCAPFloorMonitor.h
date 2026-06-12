#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;

/**
 * Floor Monitor — a live view of every Live Link subject currently streaming into the
 * editor (what Shogun/Vicon is putting out on the floor): body skeletons, the TPVCam
 * rigid body, tracked props. Each row shows name, role, source, and a live/stale dot.
 * Read-only; refreshes a few times a second.
 */
class PCAPTOOL_API SPCAPFloorMonitor : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCAPFloorMonitor) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<SBox> ListBox;
    void RebuildList();
    EActiveTimerReturnType Poll(double InCurrentTime, float InDeltaTime);

    const FLinearColor ColGreen = FLinearColor(0.290f, 0.878f, 0.502f);
    const FLinearColor ColAmber = FLinearColor(0.878f, 0.627f, 0.188f);
    const FLinearColor ColText2 = FLinearColor(0.478f, 0.541f, 0.502f);
};
