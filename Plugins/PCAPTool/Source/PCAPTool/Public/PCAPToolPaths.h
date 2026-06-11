#pragma once

#include "CoreMinimal.h"

// ── PCAP tool content area — single source of truth ─────────────────────────
// Everything the toolset builds lives under one root, organised into branches so
// it is always findable in the proper place. Current AND future tools/databases
// must derive their paths from here — never hardcode a "/Game/..." path.
//
//   Disk root:   Content/PCAP Tool/
//   Asset root:  /Game/PCAP Tool/
//
//   /Game/PCAP Tool/
//   ├── Databases/        ← the database setup (rosters + master DB)
//   ├── Pipeline Tools/   ← per-tool asset areas (Shoot Setup, Realtime Operator, HMC, VCam, …)
//   └── Productions/      ← recorded takes (shoot output)
//
// To relocate the entire area, change Root() — everything follows.
namespace PCAPPaths
{
    inline FString Root()           { return TEXT("/Game/PCAP Tool"); }

    // Top-level branches.
    inline FString Databases()      { return Root() + TEXT("/Databases"); }
    inline FString PipelineTools()  { return Root() + TEXT("/Pipeline Tools"); }
    inline FString Productions()    { return Root() + TEXT("/Productions"); }

    // Database sub-areas.
    inline FString ActorsDir()      { return Databases() + TEXT("/Actors"); }
    inline FString PropsDir()       { return Databases() + TEXT("/Props"); }
    inline FString StagesDir()      { return Databases() + TEXT("/Stages"); }
    inline FString MasterDatabase() { return Databases() + TEXT("/MasterPCAPDatabase"); }

    // Per-tool asset area, created on demand as a tool needs to write assets.
    // e.g. ToolDir(TEXT("Realtime Operator")) → /Game/PCAP Tool/Pipeline Tools/Realtime Operator
    inline FString ToolDir(const TCHAR* ToolName) { return PipelineTools() + TEXT("/") + ToolName; }
}
