#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPToolTypes.h"   // FShotSubject / FPropEntry / FShot
#include "PCAPMocapBridge.generated.h"

class UPropRosterEntry;
class ACapturePerformer;   // PerformanceCaptureCore — CapturePerformer.h

// ---------------------------------------------------------------------------
// PCAP ▸ Mocap Manager bridge (UE 5.8)
//
// PCAPTool's databases stay the source of truth for who/what is called to a
// shot. This bridge projects that data onto Epic's official Performance Capture
// actors so the engine's Mocap Manager records them:
//
//   FShotSubject  -> ACapturePerformer (PerformanceCaptureCore)
//                      .SetLiveLinkSubject(body, else face)
//                      .SetMocapMesh(DrivenTarget when it is a USkeletalMesh)
//   FPropEntry    -> a mesh actor carrying a UPCapPropComponent
//                      (PerformanceCaptureWorkflowRuntime), bound to the prop's
//                      Live Link subject and controlling the actor's mesh.
//
// Spawn helpers place a fresh actor; Configure helpers re-bind an actor the
// Mocap Manager already placed. All functions are no-ops on a null World and
// return null/0 rather than asserting, so call sites stay simple.
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPMocapBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // The Live Link subject a called subject should stream from: the body
    // subject when it has one, otherwise the face subject. None if neither.
    UFUNCTION(BlueprintPure, Category="PCAP|Mocap Manager")
    static FName ResolvePerformerSubject(const FShotSubject& Subject);

    // Spawn an ACapturePerformer for a called subject and bind it to its Live
    // Link subject + driven skeletal mesh. Returns null if World is null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static ACapturePerformer* SpawnPerformerForSubject(UWorld* World, const FShotSubject& Subject);

    // Re-bind an existing performer (e.g. one the Mocap Manager placed) from a
    // called subject. Returns false if Performer is null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static bool ConfigurePerformer(ACapturePerformer* Performer, const FShotSubject& Subject);

    // Spawn a mesh actor carrying a UPCapPropComponent for a tracked prop. The
    // mesh comes from Roster->PropAsset; the Live Link subject from the called
    // prop (falling back to the roster default). Returns null if World is null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static AActor* SpawnPropActor(UWorld* World, const FPropEntry& Prop, const UPropRosterEntry* Roster);

    // Spawn performers + props for every called subject/prop in a shot. Spawned
    // actors are appended to OutSpawned. Returns the count spawned.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static int32 SpawnShotToStage(UWorld* World, const FShot& Shot,
                                  const TArray<UPropRosterEntry*>& PropRoster,
                                  TArray<AActor*>& OutSpawned);
};
