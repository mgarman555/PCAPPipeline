#include "PCAPVCamActor.h"

APCAPVCamActor::APCAPVCamActor(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Tag so the subsystem can find an existing instance in the level deterministically.
    Tags.Add(FName(TEXT("PCAP_VCam")));
}
