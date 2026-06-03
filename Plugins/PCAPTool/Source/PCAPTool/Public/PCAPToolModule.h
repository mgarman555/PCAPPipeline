#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class FPCAPToolModule : public IModuleInterface
{
public:
    static const FName HMCTabName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnHMCTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
};
