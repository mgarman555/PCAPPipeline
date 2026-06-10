#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class FPCAPToolModule : public IModuleInterface
{
public:
    static const FName HMCTabName;
    static const FName DatabaseTabName;
    static const FName ActorDBTabName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnHMCTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDatabaseTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnActorDBTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
};
