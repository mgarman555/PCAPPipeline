#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "PCAPVCamActor.generated.h"

// The in-editor virtual camera the VCAM subsystem drives every frame and Take Recorder
// records. One instance, found-or-spawned by UPCAPVCamSubsystem (not per take).
UCLASS()
class PCAPTOOL_API APCAPVCamActor : public ACineCameraActor
{
    GENERATED_BODY()
public:
    APCAPVCamActor(const FObjectInitializer& ObjectInitializer);
};
