// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.

#include "ILiveLinkDataStreamModule.h"

#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "CameraCalibrationSubsystem.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "LiveLinkClient.h"

#include "ViconLensModel.h"

/**
 * Implements the Messaging module.
 */

#define LOCTEXT_NAMESPACE "LiveLinkDataStreamModule"

class FLiveLinkDataStreamModule
  : public ILiveLinkDataStreamModuleInterface
{
public:
  void* DataStreamHandle = nullptr;

  // IModuleInterface interface

  virtual void StartupModule() override
  {
    // DataStream DLL
    FString PluginsPath = IPluginManager::Get().FindPlugin("LiveLinkViconDataStream")->GetBaseDir();
    FString DllPath = FPaths::Combine(*PluginsPath, TEXT("Binaries/Win64/"));
    FPlatformProcess::AddDllDirectory(*DllPath);
    DataStreamHandle = FPlatformProcess::GetDllHandle(*(FPaths::Combine(*DllPath, TEXT("ViconDataStreamSDK_CPP.dll"))));
    if (DataStreamHandle == nullptr)
    {
      UE_LOG(LogTemp, Warning, TEXT("Unable to load Vicon DLL: %s"), *(FPaths::Combine(*DllPath, TEXT("ViconDataStreamSDK_CPP.dll"))));
    }
    // to move
    RegisterDistortionModels();
  }

  virtual void ShutdownModule() override
  {
    if (DataStreamHandle != nullptr)
    {
      FPlatformProcess::FreeDllHandle(DataStreamHandle);
    }
    UnregisterDistortionModels();
  }

  virtual bool SupportsDynamicReloading() override
  {
    return false;
  }

private:
  // to move to different plugin
  void RegisterDistortionModels()
  {
    auto RegisterModels = [this]()
    {
      // Register all lens models defined in this module
      UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
      SubSystem->RegisterDistortionModel(UViconLensModel::StaticClass());
    };

    if (FApp::CanEverRender())
    {
      if (GEngine && GEngine->IsInitialized())
      {
        RegisterModels();
      }
      else
      {
        PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda(RegisterModels);
      }
    }
  }

  void UnregisterDistortionModels()
  {
    if (GEngine)
    {
      // Unregister all lens models defined in this module
      if (UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>())
      {
        SubSystem->UnregisterDistortionModel(UViconLensModel::StaticClass());
      }
    }

    if (PostEngineInitHandle.IsValid())
    {
      FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
    }
  }
  FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FLiveLinkDataStreamModule, LiveLinkDataStream);

#undef LOCTEXT_NAMESPACE
