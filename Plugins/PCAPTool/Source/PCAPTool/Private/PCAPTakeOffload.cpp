#include "PCAPTakeOffload.h"

#include "PCAPToolTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

// Named namespace, not anonymous: unity builds merge this file with others.
namespace PCAPOffloadPrivate
{
    // Capture Manager 5.8 (StereoVideoIngestDevice.cpp / MonoVideoIngestDevice.cpp).
    static const TCHAR* VideoExts[] = { TEXT("mp4"), TEXT("mov") };
    static const TCHAR* AudioExts[] = { TEXT("wav") };
    static const TCHAR* ImageExts[] = { TEXT("jpg"), TEXT("jpeg"), TEXT("png") };

    template <int32 N>
    static bool HasExt(const FString& Path, const TCHAR* (&Exts)[N])
    {
        const FString Ext = FPaths::GetExtension(Path);
        for (const TCHAR* E : Exts) if (Ext.Equals(E, ESearchCase::IgnoreCase)) return true;
        return false;
    }

    static FString HashOf(const FString& Path)
    {
        return LexToString(FMD5Hash::HashFile(*Path));
    }

    static const TCHAR* KindName(EPCAPOffloadKind K)
    {
        switch (K)
        {
        case EPCAPOffloadKind::Face:  return TEXT("face");
        case EPCAPOffloadKind::Body:  return TEXT("body");
        case EPCAPOffloadKind::Audio: return TEXT("audio");
        }
        return TEXT("unknown");
    }

    template <typename TEnum>
    static FString EnumName(TEnum V)
    {
        return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(V));
    }

    static bool WriteManifest(const FString& Path, const FString& ProjectCode, const FString& DayID,
        const FString& ShotID, const FTake& Take, const FString& InDayDir, const TArray<FPCAPOffloadedFile>& Files)
    {
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("schema"), 1);
        J->SetStringField(TEXT("project"), ProjectCode);
        J->SetStringField(TEXT("day"), DayID);
        J->SetStringField(TEXT("shot"), ShotID);
        J->SetStringField(TEXT("take"), Take.TakeID);
        J->SetStringField(TEXT("takeNumber"), Take.TakeNumber);
        J->SetStringField(TEXT("session"), Take.SessionID);
        J->SetStringField(TEXT("recordedAt"), Take.RecordedAt.ToIso8601());
        J->SetNumberField(TEXT("durationSec"), Take.DurationSeconds);
        J->SetStringField(TEXT("label"), EnumName(Take.Label));
        J->SetBoolField(TEXT("autoFlagged"), Take.bAutoFlagged);
        J->SetStringField(TEXT("sync"), EnumName(Take.Sync));

        TArray<TSharedPtr<FJsonValue>> Alarms;
        for (const FPCAPAlarmEvent& A : Take.Alarms)
        {
            TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
            O->SetNumberField(TEXT("offsetSec"), A.TakeOffsetSec);
            O->SetStringField(TEXT("source"), A.Source.ToString());
            O->SetStringField(TEXT("severity"), EnumName(A.Severity));
            O->SetNumberField(TEXT("flags"), A.Flags);
            O->SetStringField(TEXT("reason"), A.Reason);
            Alarms.Add(MakeShared<FJsonValueObject>(O));
        }
        J->SetArrayField(TEXT("alarms"), Alarms);

        TSharedRef<FJsonObject> Offsets = MakeShared<FJsonObject>();
        for (const TPair<FName, double>& P : Take.DeviceStartOffsets)
            Offsets->SetNumberField(P.Key.ToString(), P.Value);
        J->SetObjectField(TEXT("deviceStartOffsets"), Offsets);

        TArray<TSharedPtr<FJsonValue>> FileArr;
        for (const FPCAPOffloadedFile& F : Files)
        {
            FString Rel = F.DestPath;
            FPaths::MakePathRelativeTo(Rel, *(InDayDir + TEXT("/")));
            TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
            O->SetStringField(TEXT("path"), Rel);
            O->SetStringField(TEXT("kind"), KindName(F.Kind));
            O->SetNumberField(TEXT("size"), static_cast<double>(F.Size));
            O->SetStringField(TEXT("md5"), F.MD5);
            O->SetBoolField(TEXT("verified"), F.bVerified);
            FileArr.Add(MakeShared<FJsonValueObject>(O));
        }
        J->SetArrayField(TEXT("files"), FileArr);

        FString Out;
        TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
        if (!FJsonSerializer::Serialize(J, W)) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
}

FString PCAPOffload::DayDir(const FString& Root, const FString& ProjectCode, const FString& DayID)
{
    return Root / ProjectCode / (TEXT("Day_") + DayID);
}

FString PCAPOffload::FaceTakeDir(const FString& InDayDir, const FString& TakeID, const FString& ActorID)
{
    return InDayDir / TEXT("face") / (TakeID + TEXT("_") + ActorID);
}

FString PCAPOffload::BodyTakeDir(const FString& InDayDir, const FString& TakeID)
{
    return InDayDir / TEXT("body") / TakeID;
}

FString PCAPOffload::ManifestPath(const FString& InDayDir, const FString& TakeID)
{
    return InDayDir / TEXT("takes") / (TakeID + TEXT(".json"));
}

bool PCAPOffload::IsIngestSafeName(const FString& Name)
{
    // Same character sets as UE::CaptureManager::SanitizePackagePath.
    return !Name.IsEmpty() && FName::IsValidXName(Name, INVALID_OBJECTPATH_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS);
}

FPCAPOffloadResult PCAPOffload::OffloadTake(const FString& Root, const FString& ProjectCode,
    const FString& DayID, const FString& ShotID, const FTake& Take, const TArray<FPCAPOffloadFile>& Files)
{
    using namespace PCAPOffloadPrivate;
    FPCAPOffloadResult R;
    IFileManager& FM = IFileManager::Get();

    if (!IsIngestSafeName(Take.TakeID))
    {
        R.Errors.Add(FString::Printf(TEXT("Take ID \"%s\" is empty or has characters Capture Manager would rename."), *Take.TakeID));
        return R;
    }
    if (Files.Num() == 0)
    {
        R.Errors.Add(TEXT("Nothing to offload."));
        return R;
    }

    const FString Day = DayDir(Root, ProjectCode, DayID);
    TSet<FString> FaceActors;
    for (const FPCAPOffloadFile& F : Files)
        if (F.Kind == EPCAPOffloadKind::Face) FaceActors.Add(F.ActorID);

    for (const FPCAPOffloadFile& F : Files)
    {
        FString Dir;
        if (F.Kind == EPCAPOffloadKind::Body)
        {
            Dir = BodyTakeDir(Day, Take.TakeID);
        }
        else if (F.Kind == EPCAPOffloadKind::Audio && !FaceActors.Contains(F.ActorID))
        {
            Dir = BodyTakeDir(Day, Take.TakeID);
        }
        else
        {
            if (!IsIngestSafeName(F.ActorID))
            {
                R.Errors.Add(FString::Printf(TEXT("%s: actor ID \"%s\" is empty or would be renamed on ingest."),
                    *FPaths::GetCleanFilename(F.SourcePath), *F.ActorID));
                continue;
            }
            Dir = FaceTakeDir(Day, Take.TakeID, F.ActorID);
        }

        FPCAPOffloadedFile Out;
        Out.SourcePath = F.SourcePath;
        Out.Kind = F.Kind;
        Out.DestPath = Dir / FPaths::GetCleanFilename(F.SourcePath);

        if (!FM.FileExists(*F.SourcePath))
        {
            R.Errors.Add(FString::Printf(TEXT("Missing source file: %s"), *F.SourcePath));
            continue;
        }
        if (!IsIngestSafeName(FPaths::GetBaseFilename(F.SourcePath)))
        {
            R.Errors.Add(FString::Printf(TEXT("%s: file name has characters Capture Manager would rename."),
                *FPaths::GetCleanFilename(F.SourcePath)));
            continue;
        }

        Out.MD5  = HashOf(F.SourcePath);
        Out.Size = FM.FileSize(*F.SourcePath);

        if (FM.FileExists(*Out.DestPath))
        {
            if (HashOf(Out.DestPath) != Out.MD5)
            {
                R.Errors.Add(FString::Printf(TEXT("A different file is already at %s. Left it alone."), *Out.DestPath));
                R.Files.Add(Out);
                continue;
            }
        }
        else
        {
            FM.MakeDirectory(*Dir, true);
            if (FM.Copy(*Out.DestPath, *F.SourcePath, false) != COPY_OK)
            {
                R.Errors.Add(FString::Printf(TEXT("Copy failed: %s -> %s"), *F.SourcePath, *Out.DestPath));
                R.Files.Add(Out);
                continue;
            }
            if (HashOf(Out.DestPath) != Out.MD5)
            {
                R.Errors.Add(FString::Printf(TEXT("Copy of %s does not match the source."), *FPaths::GetCleanFilename(F.SourcePath)));
                R.Files.Add(Out);
                continue;
            }
        }
        Out.bVerified = true;
        R.Files.Add(Out);
    }

    R.ManifestPath = ManifestPath(Day, Take.TakeID);
    if (!WriteManifest(R.ManifestPath, ProjectCode, DayID, ShotID, Take, Day, R.Files))
        R.Errors.Add(FString::Printf(TEXT("Could not write %s"), *R.ManifestPath));

    R.bOk = R.Errors.Num() == 0;
    return R;
}

TArray<FString> PCAPOffload::CheckIngestFolder(const FString& ContainerDir, int32 VideosPerTake)
{
    using namespace PCAPOffloadPrivate;
    TArray<FString> P;
    IFileManager& FM = IFileManager::Get();
    if (!FM.DirectoryExists(*ContainerDir))
    {
        P.Add(FString::Printf(TEXT("Folder not found: %s"), *ContainerDir));
        return P;
    }

    TArray<FString> Loose;
    FM.FindFiles(Loose, *(ContainerDir / TEXT("*")), true, false);
    for (const FString& L : Loose)
        if (HasExt(L, VideoExts) || HasExt(L, AudioExts) || HasExt(L, ImageExts))
            P.Add(FString::Printf(TEXT("%s sits loose in the folder; it belongs inside a take folder."), *L));

    TArray<FString> Takes;
    FM.FindFiles(Takes, *(ContainerDir / TEXT("*")), false, true);
    if (Takes.Num() == 0)
        P.Add(TEXT("No take folders found."));

    for (const FString& T : Takes)
    {
        if (!IsIngestSafeName(T))
            P.Add(FString::Printf(TEXT("%s: folder name would be renamed on ingest."), *T));

        int32 Videos = 0, Wavs = 0;
        TSet<FString> SeqDirs;
        FM.IterateDirectoryRecursively(*(ContainerDir / T), [&](const TCHAR* Path, bool bIsDir)
        {
            if (bIsDir) return true;
            const FString S(Path);
            if (HasExt(S, VideoExts)) ++Videos;
            else if (HasExt(S, AudioExts)) ++Wavs;
            else if (HasExt(S, ImageExts)) SeqDirs.Add(FPaths::GetPath(S));
            return true;
        });

        const bool bVideoOk = (Videos == VideosPerTake && SeqDirs.Num() == 0)
                           || (Videos == 0 && SeqDirs.Num() == VideosPerTake);
        if (!bVideoOk)
            P.Add(FString::Printf(TEXT("%s: %d videos and %d image sequences; a take needs exactly %d of one kind."),
                *T, Videos, SeqDirs.Num(), VideosPerTake));
        if (Wavs > 1)
            P.Add(FString::Printf(TEXT("%s: %d .wav files; a take can hold at most 1."), *T, Wavs));
    }
    return P;
}
