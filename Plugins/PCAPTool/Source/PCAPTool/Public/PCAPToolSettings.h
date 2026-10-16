#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PCAP Tool"))
class PCAPTOOL_API UPCAPToolSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPCAPToolSettings();

    UPROPERTY(Config, EditAnywhere, Category="PCAP Tool")
    TSoftObjectPtr<UMocapDatabase> DatabaseAsset;

    // ── Capture systems (the Stage Database / Capture Setup dropdowns) ──
    // Edit these lists to add, rename or retire systems; no rebuild needed. Saved to
    // Config/DefaultGame.ini. Each version becomes its own entry ("Shogun Live 1.19").

    UPROPERTY(Config, EditAnywhere, Category="Capture Systems")
    TArray<FPCAPSystemOption> MocapSystems;

    UPROPERTY(Config, EditAnywhere, Category="Capture Systems")
    TArray<FPCAPSystemOption> FacialSystems;

    // "Only With Facial" limits an entry to one facial system (iPhone audio -> Rokoko iPhone).
    UPROPERTY(Config, EditAnywhere, Category="Capture Systems")
    TArray<FPCAPSystemOption> AudioSystems;

    // Where offloaded takes are written: <CaptureRoot>/<Production>/<Day>/... (see PCAPTakeOffload.h).
    UPROPERTY(Config, EditAnywhere, Category="Capture")
    FDirectoryPath CaptureRoot;

    // Body camera (markerless). Defaults are the C920S's verified mode: docs/devices/body-camera.md §8.
    UPROPERTY(Config, EditAnywhere, Category="Body Camera", meta=(FilePathFilter="exe"))
    FFilePath FFmpegPath;

    UPROPERTY(Config, EditAnywhere, Category="Body Camera")
    FString BodyCameraDevice = TEXT("HD Pro Webcam C920");

    UPROPERTY(Config, EditAnywhere, Category="Body Camera", meta=(ClampMin=160))
    int32 BodyCameraWidth = 1280;

    UPROPERTY(Config, EditAnywhere, Category="Body Camera", meta=(ClampMin=90))
    int32 BodyCameraHeight = 720;

    UPROPERTY(Config, EditAnywhere, Category="Body Camera", meta=(ClampMin=1, ClampMax=120))
    int32 BodyCameraFps = 30;

    static UPCAPToolSettings* Get();
    UMocapDatabase* GetDatabase() const;

    // Returns the assigned database, or creates one in the PCAP tool area
    // (PCAPPaths::MasterDatabase) and self-assigns it if none is set. Editor only.
    UMocapDatabase* GetOrCreateDatabase();

    // Dropdown entries: names, expanded by version ("Shogun Live 1.13", "Shogun Live 1.19").
    static TArray<FString> FlattenOptions(const TArray<FPCAPSystemOption>& Options);
    // Audio entries valid for a facial selection (drops OnlyWithFacial entries that don't match).
    static TArray<FString> AudioOptionsFor(const TArray<FPCAPSystemOption>& Audio, const FString& FacialSelection);

    // The option a dropdown entry came from ("Shogun Live 1.19" -> Shogun Live), or null.
    static const FPCAPSystemOption* FindOption(const TArray<FPCAPSystemOption>& Options, const FString& Selection);
    const FPCAPSystemOption* FindMocap(const FString& Selection) const { return FindOption(MocapSystems, Selection); }

    TArray<FString> GetMocapOptions() const  { return FlattenOptions(MocapSystems); }
    TArray<FString> GetFacialOptions() const { return FlattenOptions(FacialSystems); }
    TArray<FString> GetAudioOptions(const FString& FacialSelection) const { return AudioOptionsFor(AudioSystems, FacialSelection); }

    virtual FName GetCategoryName() const override { return FName("PCAP"); }
    virtual FName GetSectionName()  const override { return FName("PCAP Tool"); }
};
