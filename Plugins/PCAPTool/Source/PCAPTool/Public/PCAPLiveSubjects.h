#pragma once

#include "CoreMinimal.h"

// Floor ↔ database helpers: enumerate what Live Link (Shogun/Vicon) is streaming and
// what's already bound to a database entry, so each database can offer to capture the
// untracked subjects. Editor/runtime — reads the live Live Link client.
namespace PCAPLiveSubjects
{
    // Names of all live Live Link subjects currently streaming (e.g. from Shogun/Vicon).
    PCAPTOOL_API TArray<FString> GetLive();

    // Subject names already bound to an Actor (body/face) or Prop database entry.
    PCAPTOOL_API TSet<FString> GetAllBound();

    // Live subjects not yet tracked in any database, sorted alphabetically.
    PCAPTOOL_API TArray<FString> GetUntracked();
}
