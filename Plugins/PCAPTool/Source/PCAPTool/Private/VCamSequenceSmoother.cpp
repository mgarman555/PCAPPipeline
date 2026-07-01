#include "VCamSequenceSmoother.h"
#include "VCamCurveSmoothing.h"

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "GameFramework/Actor.h"

namespace
{
    // Find the camera's transform section. Prefer the binding whose name matches the camera's
    // label; otherwise take the first transform section in the sequence (a vcam-only take).
    UMovieScene3DTransformSection* FindCameraTransformSection(UMovieScene* MS, const AActor* Cam)
    {
        if (!MS) { return nullptr; }
        const FString CamLabel = Cam ? Cam->GetActorLabel() : FString();
        UMovieScene3DTransformSection* Fallback = nullptr;

        for (const FMovieSceneBinding& Binding : MS->GetBindings())
        {
            UMovieScene3DTransformTrack* Track = MS->FindTrack<UMovieScene3DTransformTrack>(Binding.GetObjectGuid());
            if (!Track) { continue; }

            UMovieScene3DTransformSection* Sec = nullptr;
            for (UMovieSceneSection* S : Track->GetAllSections())
            {
                Sec = Cast<UMovieScene3DTransformSection>(S);
                if (Sec) { break; }
            }
            if (!Sec) { continue; }
            if (!Fallback) { Fallback = Sec; }

            if (!CamLabel.IsEmpty())
            {
                if (const FMovieScenePossessable* P = MS->FindPossessable(Binding.GetObjectGuid()))
                {
                    if (P->GetName() == CamLabel) { return Sec; }
                }
            }
        }
        return Fallback;
    }
}

bool FVCamSequenceSmoother::SmoothRecordedVCam(ULevelSequence* Sequence, const AActor* CameraActor,
                                               const FVCamSmoothingSettings& Settings)
{
    if (Settings.TranslationMethod == EVCamSmoothMethod::None &&
        Settings.RotationMethod    == EVCamSmoothMethod::None)
    {
        return false;
    }
    if (!Sequence) { return false; }
    UMovieScene* MS = Sequence->GetMovieScene();
    if (!MS) { return false; }

    UMovieScene3DTransformSection* Section = FindCameraTransformSection(MS, CameraActor);
    if (!Section) { return false; }

    // Transform channels: [0..2] Loc XYZ, [3..5] Rot Roll/Pitch/Yaw, [6..8] Scale (UE5: doubles).
    TArrayView<FMovieSceneDoubleChannel*> Ch = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
    if (Ch.Num() < 6) { return false; }

    const FFrameRate TickRes = MS->GetTickResolution();
    const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    if (!Range.GetLowerBound().IsClosed() || !Range.GetUpperBound().IsClosed()) { return false; }
    const FFrameNumber Start = Range.GetLowerBoundValue();
    const FFrameNumber End   = Range.GetUpperBoundValue();

    const float Fps = FMath::Max(1.f, Settings.ResamplingFps);
    const int32 StepTicks = FMath::Max(1, (int32)TickRes.AsFrameNumber(1.0 / Fps).Value);
    const int32 N = (End.Value - Start.Value) / StepTicks + 1;
    if (N < 8) { return false; }

    auto SampleTick = [&](int32 i) { return FFrameNumber(Start.Value + StepTicks * i); };

    auto SampleChannel = [&](int32 k, TArray<float>& Out)
    {
        Out.SetNum(N);
        for (int32 i = 0; i < N; ++i)
        {
            double V = 0.0;
            Ch[k]->Evaluate(FFrameTime(SampleTick(i)), V);
            Out[i] = (float)V;
        }
    };

    TArray<float> LX, LY, LZ, RRoll, RPitch, RYaw;
    SampleChannel(0, LX); SampleChannel(1, LY); SampleChannel(2, LZ);
    SampleChannel(3, RRoll); SampleChannel(4, RPitch); SampleChannel(5, RYaw);

    if (Settings.TranslationMethod == EVCamSmoothMethod::LowPassFilter)
    {
        TArray<FVector> P; P.SetNum(N);
        for (int32 i = 0; i < N; ++i) { P[i] = FVector(LX[i], LY[i], LZ[i]); }
        FVCamCurveSmoothing::SmoothPositions(P, Settings);
        for (int32 i = 0; i < N; ++i) { LX[i] = P[i].X; LY[i] = P[i].Y; LZ[i] = P[i].Z; }
    }

    if (Settings.RotationMethod != EVCamSmoothMethod::None)
    {
        TArray<FQuat> Q; Q.SetNum(N);
        for (int32 i = 0; i < N; ++i) { Q[i] = FRotator(RPitch[i], RYaw[i], RRoll[i]).Quaternion(); }
        FVCamCurveSmoothing::SmoothRotations(Q, Settings);
        for (int32 i = 0; i < N; ++i)
        {
            const FRotator R = Q[i].Rotator();
            RRoll[i] = R.Roll; RPitch[i] = R.Pitch; RYaw[i] = R.Yaw;
        }
    }

    // Re-key the transform channels with the smoothed uniform samples (cubic + auto tangents).
    auto Rekey = [&](int32 k, const TArray<float>& V)
    {
        FMovieSceneDoubleChannel* C = Ch[k];
        TArray<FFrameNumber> Times; Times.SetNum(N);
        TArray<FMovieSceneDoubleValue> Values; Values.SetNum(N);
        for (int32 i = 0; i < N; ++i)
        {
            Times[i] = SampleTick(i);
            FMovieSceneDoubleValue DV((double)V[i]);
            DV.InterpMode   = RCIM_Cubic;
            DV.TangentMode  = RCTM_Auto;
            Values[i] = DV;
        }
        C->Set(Times, Values);
        C->AutoSetTangents();
    };

    Section->Modify();
    Rekey(0, LX); Rekey(1, LY); Rekey(2, LZ);
    if (Settings.RotationMethod != EVCamSmoothMethod::None)
    {
        Rekey(3, RRoll); Rekey(4, RPitch); Rekey(5, RYaw);
    }
    MS->Modify();
    return true;
}
