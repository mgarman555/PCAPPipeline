#pragma once

#include "CoreMinimal.h"

// ── PCAP tool content area — single source of truth ─────────────────────────
// Every database, folder, and generated asset the tool builds lives under this
// root, so it is always findable in the proper place. Current AND future tools /
// databases must derive their paths from here — never hardcode a "/Game/..." path.
//
//   Disk:        Content/PCAPTool/...
//   Asset path:  /Game/PCAPTool/...
//
// To relocate the entire PCAP tool area, change Root() — everything follows.
namespace PCAPPaths
{
    inline FString Root()           { return TEXT("/Game/PCAPTool"); }
    inline FString Databases()      { return Root() + TEXT("/Databases"); }
    inline FString Productions()    { return Root() + TEXT("/Productions"); }

    inline FString ActorsDir()      { return Databases() + TEXT("/Actors"); }
    inline FString PropsDir()       { return Databases() + TEXT("/Props"); }
    inline FString StagesDir()      { return Databases() + TEXT("/Stages"); }
    inline FString MasterDatabase() { return Databases() + TEXT("/MasterPCAPDatabase"); }
}
