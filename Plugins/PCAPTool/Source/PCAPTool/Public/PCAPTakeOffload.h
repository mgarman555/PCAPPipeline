#pragma once

#include "CoreMinimal.h"

struct FTake;

// ── Take offload: raw files off the devices into the capture folder ─────────────
//
// Layout is shaped by what Capture Manager's 5.8 importers accept (read from
// Engine/Plugins/VirtualProduction/CaptureManager/CaptureManagerDevices/Source):
//   * Stereo Video Ingest, <Auto> discovery, pointed at a folder of takes: each first-level
//     subfolder is one take, named by the folder; a take must hold exactly 2 videos
//     (.mp4/.mov) or 2 image-sequence folders (.png/.jpg), and at most 1 .wav.
//   * Mono Video Ingest recurses and treats every .mp4/.mov as its own take.
//   * Names are sanitized to valid package paths; we keep names valid so nothing is renamed.
// The handoff's single take folder (face + body + audio together) holds 3 videos and fails
// the stereo rule, so face and body are split by kind:
//   <Root>/<Project>/Day_<Day>/face/<TakeID>_<Actor>/   HMC videos + that performer's .wav
//   <Root>/<Project>/Day_<Day>/body/<TakeID>/           body camera video
//   <Root>/<Project>/Day_<Day>/takes/<TakeID>.json      manifest (ingest ignores .json)
// Point Stereo (or Mono) Video Ingest at .../face and Mono Video Ingest at .../body.

enum class EPCAPOffloadKind : uint8 { Face, Body, Audio };

struct PCAPTOOL_API FPCAPOffloadFile
{
    FString SourcePath;
    EPCAPOffloadKind Kind = EPCAPOffloadKind::Face;
    FString ActorID;     // Face/Audio: whose HMC or mic. Audio with no face for the actor goes to body/.
};

struct PCAPTOOL_API FPCAPOffloadedFile
{
    FString SourcePath;
    FString DestPath;
    EPCAPOffloadKind Kind = EPCAPOffloadKind::Face;
    int64   Size = 0;
    FString MD5;
    bool    bVerified = false;   // dest hash matches source hash
};

struct PCAPTOOL_API FPCAPOffloadResult
{
    bool bOk = false;
    TArray<FPCAPOffloadedFile> Files;
    TArray<FString> Errors;
    FString ManifestPath;
};

namespace PCAPOffload
{
    PCAPTOOL_API FString DayDir(const FString& Root, const FString& ProjectCode, const FString& DayID);
    PCAPTOOL_API FString FaceTakeDir(const FString& InDayDir, const FString& TakeID, const FString& ActorID);
    PCAPTOOL_API FString BodyTakeDir(const FString& InDayDir, const FString& TakeID);
    PCAPTOOL_API FString ManifestPath(const FString& InDayDir, const FString& TakeID);

    // Capture Manager keeps names as-is only when they are valid package-path names.
    PCAPTOOL_API bool IsIngestSafeName(const FString& Name);

    // Copies every file, hashes source and destination, and writes the take manifest.
    // Safe to re-run: an identical file already in place counts as done. A different file
    // already at the destination is an error and is never overwritten.
    PCAPTOOL_API FPCAPOffloadResult OffloadTake(const FString& Root, const FString& ProjectCode,
        const FString& DayID, const FString& ShotID, const FTake& Take, const TArray<FPCAPOffloadFile>& Files);

    // What Capture Manager would trip on when ingesting ContainerDir as a folder of takes.
    // VideosPerTake: 2 for stereo face, 1 for mono face or body. Empty = importable.
    PCAPTOOL_API TArray<FString> CheckIngestFolder(const FString& ContainerDir, int32 VideosPerTake);
}
