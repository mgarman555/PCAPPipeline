#include "PCAPMarkerSource.h"

FLinearColor PCAPViz::SubjectColor(FName SubjectName)
{
    // djb2 over the string -> stable across runs (FName's type hash is not).
    const FString S = SubjectName.ToString();
    uint32 Hash = 5381;
    for (const TCHAR C : S) { Hash = ((Hash << 5) + Hash) + (uint32)C; }
    const uint8 Hue = (uint8)(Hash % 256);
    return FLinearColor::MakeFromHSV8(Hue, 200, 235);
}

FVector PCAPViz::ViconMMToUE(double X, double Y, double Z)
{
    // mm -> cm (x0.1); negate Y for right- to left-handed.
    return FVector((float)(X * 0.1), (float)(-Y * 0.1), (float)(Z * 0.1));
}
