#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class FWorkspaceItem;

class FPCAPToolModule : public IModuleInterface
{
public:
    // PCAP Tools group.
    static const FName HMCTabName;
    static const FName ConsoleTabName;
    static const FName CallSheetTabName;
    static const FName VCamTabName;
    // Databases group.
    static const FName ActorDBTabName;
    static const FName PropDBTabName;
    static const FName StageDBTabName;
    static const FName VCamDBTabName;
    static const FName ProdDBTabName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnHMCTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnConsoleTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnCallSheetTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnVCamTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnActorDBTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnPropDBTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnStageDBTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnVCamDBTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnProdDBTab(const FSpawnTabArgs& Args);

    TSharedPtr<FWorkspaceItem> PCAPMenuGroup;        // "PCAP Tools" — operator tools
    TSharedPtr<FWorkspaceItem> DatabasesMenuGroup;   // "Databases" — separate data setup
};
