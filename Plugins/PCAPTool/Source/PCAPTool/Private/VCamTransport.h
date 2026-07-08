#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────────────
// Controller / panel → Sequencer transport bridge.
//
// Reproduces the 4.26 VCamIO transport command map (reconciliation §6) against the
// currently open Level Sequence, using the editor-safe ULevelSequenceEditorBlueprintLibrary
// path (module LevelSequenceEditor). In 4.26 these actions arrived as VCamIO `cm` packets and
// were applied to `GetSequencer()`; here they are driven by the input layer's intents
// (bPlaybackToggle / bScrubFwd / bScrubBack) and the operator panel's transport row, but the
// actions themselves are the same spec.
//
// Every entry no-ops unless a Level Sequence is open AND no take is recording — the §6
// `if (isRecording) break;` guard, so controller transport is inert during a shot.
//
// Editor-only (Sequencer is not available at runtime). CONFIRM-AT-BUILD on UE 5.8: the exact
// ULevelSequenceEditorBlueprintLibrary::SetPlaybackSpeed / SetCurrentTime / GetCurrentTime
// signatures, and UMovieScene::SetPlaybackRange's TRange overload.
// ─────────────────────────────────────────────────────────────────────────────
struct FPCAPVCamTransport
{
    // True only when a Level Sequence is open in Sequencer AND no take is recording.
    static bool IsControllable();

    static void TogglePlayback();          // left_down (Default): play ⇄ pause
    static void Play(float Rate);          // |rate| ~ 0 → pause; else SetPlaybackSpeed(rate) + play
    static void Pause();
    static void ScrubFrames(int32 Frames); // pause + step ±Frames in the sequence's display rate
    static void JumpToFirst();             // → playback-range lower bound
    static void JumpToLast();              // → playback-range upper bound (exclusive → −1 frame)
    static void SetFrameIn();              // playback-range lower ← current frame
    static void SetFrameOut();             // playback-range upper ← current frame (exclusive → +1)
};
