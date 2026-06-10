#pragma once

#include "CoreMinimal.h"

// Shared PCAP panel palette. Declared `inline` so there is exactly one definition
// across every panel that includes it — which also sidesteps the anonymous-namespace
// "ColLabel redefinition" collision under UE's unity (combined-translation-unit) builds.
inline const FLinearColor ColLabel = FLinearColor(0.478f, 0.541f, 0.502f);
