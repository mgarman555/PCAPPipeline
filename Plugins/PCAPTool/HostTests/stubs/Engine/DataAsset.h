#pragma once
#include "CoreMinimal.h"

// Minimal UObject/UDataAsset bases so VCamConfig.h compiles host-side.
class UObject {};
class UDataAsset : public UObject {};
