#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class FWorkspaceItem;

class FPCAPToolModule : public IModuleInterface
{
public:
    static const FName HMCTabName;
    static const FName DatabaseTabName;
    static const FName ConsoleTabName;
    static const FName CallSheetTabName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnHMCTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDatabaseTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnConsoleTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnCallSheetTab(const FSpawnTabArgs& Args);
    void RegisterMenus();

    TSharedPtr<FWorkspaceItem> PCAPMenuGroup;   // "PCAP Tools" submenu under Window > Tools
};
