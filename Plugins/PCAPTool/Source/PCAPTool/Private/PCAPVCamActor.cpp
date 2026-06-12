#include "PCAPVCamActor.h"

APCAPVCamActor::APCAPVCamActor()
{
    // Tag so the subsystem can find an existing instance in the level deterministically.
    Tags.Add(FName(TEXT("PCAP_VCam")));
}
