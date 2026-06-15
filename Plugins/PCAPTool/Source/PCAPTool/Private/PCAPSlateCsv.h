#pragma once

#include "CoreMinimal.h"

// One row of the shot-list ("slate") CSV — a plain, UI-free, engine-type-free record.
// Columns: slot,type,description,actors,props,notes
// `actors` / `props` are ';'-separated id lists inside a single CSV field
// (e.g. "sarahKov;kevinDorman"), so commas stay free for real CSV columns.
struct FSlateCsvRow
{
    FString         Slot;          // shot slot as written, e.g. "3" / "003" / "901"
    FString         Type;          // "Production" | "Calibration" | "Test" | "Retargeting" (free text)
    FString         Description;
    TArray<FString> Actors;        // actor IDs called to this shot
    TArray<FString> Props;         // prop IDs on this shot
    FString         Notes;
};

// Pure CSV parse/format for the shot-list / slate adoption. No UObject, no UI, no asset
// I/O — so it is unit-testable in isolation (see Tests/PCAPSlateCsvTests.cpp). The Call
// Sheet turns FSlateCsvRow into FShot (resolving the roster) and back.
struct FPCAPSlateCsv
{
    // Canonical header line (no trailing newline).
    static FString Header();

    // Rows -> CSV text (header + one line per row). RFC-4180-style quoting is applied to
    // any field containing a comma, quote, or newline.
    static FString Format(const TArray<FSlateCsvRow>& Rows);

    // CSV text -> rows. Tolerant: an optional header row (first cell == "slot") is skipped,
    // blank records are ignored, fields are trimmed, quoted fields are unescaped. Returns
    // false with OutError if a data row has an empty slot (the one hard requirement).
    static bool Parse(const FString& Text, TArray<FSlateCsvRow>& OutRows, FString& OutError);
};
