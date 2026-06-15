#pragma once

#include "CoreMinimal.h"
#include "PCAPMarkerSource.h"

// Phase 1 stand-in: solved skeleton joints (and prop transforms) from Live Link,
// as marker dots. Not the physical optical markers — those need the Vicon SDK (Phase 2).
class FLiveLinkMarkerSource : public IMarkerSource
{
public:
    virtual bool IsAvailable() const override;
    virtual void Poll(FVizFrame& OutFrame) override;
};
