#include "VCamTransport.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"   // module LevelSequenceEditor
#include "MovieScene.h"
#include "Recorder/TakeRecorderBlueprintLibrary.h"  // UTakeRecorderBlueprintLibrary::IsRecording

namespace
{
    using ULSEBL = ULevelSequenceEditorBlueprintLibrary;

    UMovieScene* CurrentMovieScene()
    {
        ULevelSequence* Seq = ULSEBL::GetCurrentLevelSequence();
        return Seq ? Seq->GetMovieScene() : nullptr;
    }

    // Convert a frame value between the sequence's display rate and tick resolution.
    // GetCurrentTime / SetCurrentTime operate in DISPLAY rate; playback range lives in TICK
    // resolution, so jump/set-range have to hop between the two.
    int32 TickToDisplay(const UMovieScene& MS, FFrameNumber Tick)
    {
        const FFrameTime T = FFrameRate::TransformTime(FFrameTime(Tick), MS.GetTickResolution(), MS.GetDisplayRate());
        return T.FrameNumber.Value;
    }
    FFrameNumber DisplayToTick(const UMovieScene& MS, int32 DisplayFrame)
    {
        const FFrameTime T = FFrameRate::TransformTime(FFrameTime(FFrameNumber(DisplayFrame)), MS.GetDisplayRate(), MS.GetTickResolution());
        return T.FrameNumber;
    }
}

bool FPCAPVCamTransport::IsControllable()
{
    // §6 guard: transport is inert while a take is recording.
    if (UTakeRecorderBlueprintLibrary::IsRecording()) { return false; }
    return ULSEBL::GetCurrentLevelSequence() != nullptr;
}

void FPCAPVCamTransport::Pause()
{
    if (!IsControllable()) { return; }
    ULSEBL::Pause();
}

void FPCAPVCamTransport::TogglePlayback()
{
    if (!IsControllable()) { return; }
    if (ULSEBL::IsPlaying()) { ULSEBL::Pause(); }
    else                     { ULSEBL::Play(); }
}

void FPCAPVCamTransport::Play(float Rate)
{
    if (!IsControllable()) { return; }
    if (FMath::IsNearlyZero(Rate, 1.e-3f)) { ULSEBL::Pause(); return; }
    ULSEBL::SetPlaybackSpeed(Rate);
    ULSEBL::Play();
}

void FPCAPVCamTransport::ScrubFrames(int32 Frames)
{
    if (!IsControllable()) { return; }
    ULSEBL::Pause();
    const int32 Now = ULSEBL::GetCurrentTime();          // display rate
    ULSEBL::SetCurrentTime(Now + Frames);
}

void FPCAPVCamTransport::JumpToFirst()
{
    if (!IsControllable()) { return; }
    UMovieScene* MS = CurrentMovieScene();
    if (!MS) { return; }
    const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    if (!Range.GetLowerBound().IsClosed()) { return; }
    ULSEBL::Pause();
    ULSEBL::SetCurrentTime(TickToDisplay(*MS, Range.GetLowerBoundValue()));
}

void FPCAPVCamTransport::JumpToLast()
{
    if (!IsControllable()) { return; }
    UMovieScene* MS = CurrentMovieScene();
    if (!MS) { return; }
    const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    if (!Range.GetUpperBound().IsClosed()) { return; }
    ULSEBL::Pause();
    // Upper bound is exclusive → the last playable frame is one before it.
    ULSEBL::SetCurrentTime(TickToDisplay(*MS, Range.GetUpperBoundValue()) - 1);
}

void FPCAPVCamTransport::SetFrameIn()
{
    if (!IsControllable()) { return; }
    UMovieScene* MS = CurrentMovieScene();
    if (!MS) { return; }
    const FFrameNumber Cur = DisplayToTick(*MS, ULSEBL::GetCurrentTime());
    const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    MS->Modify();
    MS->SetPlaybackRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(Cur), Range.GetUpperBound()));
}

void FPCAPVCamTransport::SetFrameOut()
{
    if (!IsControllable()) { return; }
    UMovieScene* MS = CurrentMovieScene();
    if (!MS) { return; }
    // Out-point is the current frame; the range's upper bound is exclusive → +1.
    const FFrameNumber Cur = DisplayToTick(*MS, ULSEBL::GetCurrentTime());
    const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    MS->Modify();
    MS->SetPlaybackRange(TRange<FFrameNumber>(Range.GetLowerBound(), TRangeBound<FFrameNumber>::Exclusive(Cur + 1)));
}
