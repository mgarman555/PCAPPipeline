// Minimal Unreal Engine math/type stubs for HOST-SIDE compilation of PCAPTool's
// pure-logic classes (FVCamInputLayer, FPCAPVCamProcessor) with plain clang — no
// engine required. This is NOT a UE header; it only reproduces the subset of the
// UE API those translation units use, with UE's exact math conventions:
//   * FQuat A*B  == apply B then A  (standard Hamilton product)
//   * FTransform A*B == apply A then B
//   * RotateVector / FRotator<->FQuat use UE's formulas
// Verified against Unreal's FQuat/FTransform so the transform chain matches the editor.
#pragma once

#include <cmath>
#include <vector>
#include <string>
#include <initializer_list>
#include <algorithm>

// ── primitive typedefs ───────────────────────────────────────────────────────
using int32  = int;
using uint32 = unsigned int;
using uint8  = unsigned char;
using int64  = long long;

// ── UHT / reflection macros → no-ops (args are consumed by the variadic) ──────
#define PCAPTOOL_API
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UMETA(...)
#define GENERATED_BODY(...)
#define GENERATED_USTRUCT_BODY(...)

#ifndef PI
#define PI 3.1415926535897932f
#endif
#define KINDA_SMALL_NUMBER 1e-4f
#define SMALL_NUMBER       1e-8f
#define TEXT(x) x

// ── FVector ───────────────────────────────────────────────────────────────────
struct FVector
{
    float X = 0.f, Y = 0.f, Z = 0.f;
    FVector() = default;
    explicit FVector(float F) : X(F), Y(F), Z(F) {}
    FVector(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

    FVector operator+(const FVector& V) const { return {X + V.X, Y + V.Y, Z + V.Z}; }
    FVector operator-(const FVector& V) const { return {X - V.X, Y - V.Y, Z - V.Z}; }
    FVector operator-()                  const { return {-X, -Y, -Z}; }
    FVector operator*(float S)           const { return {X * S, Y * S, Z * S}; }
    FVector operator*(const FVector& V)  const { return {X * V.X, Y * V.Y, Z * V.Z}; }
    FVector& operator+=(const FVector& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }
    FVector& operator-=(const FVector& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }
    FVector& operator*=(float S)          { X *= S; Y *= S; Z *= S; return *this; }
    FVector& operator*=(const FVector& V) { X *= V.X; Y *= V.Y; Z *= V.Z; return *this; }
    bool operator==(const FVector& V) const { return X == V.X && Y == V.Y && Z == V.Z; }

    float SizeSquared() const { return X * X + Y * Y + Z * Z; }
    float Size()        const { return std::sqrt(SizeSquared()); }
    bool IsNearlyZero(float Tol = KINDA_SMALL_NUMBER) const
    { return std::fabs(X) <= Tol && std::fabs(Y) <= Tol && std::fabs(Z) <= Tol; }

    static const FVector ZeroVector;
};
inline FVector operator*(float S, const FVector& V) { return V * S; }

// ── FRotator (degrees: Pitch=about Y, Yaw=about Z, Roll=about X) ───────────────
struct FQuat;
struct FRotator
{
    float Pitch = 0.f, Yaw = 0.f, Roll = 0.f;
    FRotator() = default;
    FRotator(float InPitch, float InYaw, float InRoll) : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
    FQuat Quaternion() const;            // defined after FQuat
    static const FRotator ZeroRotator;
};

// ── FQuat (X,Y,Z,W) ───────────────────────────────────────────────────────────
struct FQuat
{
    float X = 0.f, Y = 0.f, Z = 0.f, W = 1.f;
    FQuat() = default;
    FQuat(float InX, float InY, float InZ, float InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

    // Standard Hamilton product. (A * B) rotates a vector as A(B(v)) — i.e. apply B then A,
    // matching Unreal's FQuat::operator*.
    FQuat operator*(const FQuat& Q) const
    {
        return FQuat(
            W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
            W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
            W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
            W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z);
    }

    FVector RotateVector(const FVector& V) const
    {
        // UE: V + 2w(q x V) + 2 q x (q x V)
        const FVector Q(X, Y, Z);
        const FVector T = CrossXYZ(Q, V) * 2.f;
        return V + (T * W) + CrossXYZ(Q, T);
    }

    FQuat Inverse() const { return FQuat(-X, -Y, -Z, W); }   // unit-quat conjugate

    void Normalize()
    {
        const float Sq = X * X + Y * Y + Z * Z + W * W;
        if (Sq > SMALL_NUMBER) { const float S = 1.f / std::sqrt(Sq); X *= S; Y *= S; Z *= S; W *= S; }
        else { X = Y = Z = 0.f; W = 1.f; }
    }

    FRotator Rotator() const;            // defined below

    static const FQuat Identity;

private:
    static FVector CrossXYZ(const FVector& A, const FVector& B)
    { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
};

// FRotator -> FQuat (UE's exact formula)
inline FQuat FRotator::Quaternion() const
{
    const float D2 = PI / 360.f;   // (deg -> rad) / 2
    const float SP = std::sin(Pitch * D2), CP = std::cos(Pitch * D2);
    const float SY = std::sin(Yaw   * D2), CY = std::cos(Yaw   * D2);
    const float SR = std::sin(Roll  * D2), CR = std::cos(Roll  * D2);
    return FQuat(
        CR * SP * SY - SR * CP * CY,
        -CR * SP * CY - SR * CP * SY,
        CR * CP * SY - SR * SP * CY,
        CR * CP * CY + SR * SP * SY);
}

// FQuat -> FRotator (UE's exact formula, degrees)
inline FRotator FQuat::Rotator() const
{
    const float SingularityTest = Z * X - W * Y;
    const float YawY = 2.f * (W * Z + X * Y);
    const float YawX = (1.f - 2.f * (Y * Y + Z * Z));
    const float RAD2DEG = 180.f / PI;
    const float SINGULARITY = 0.4999995f;
    FRotator R;
    if (SingularityTest < -SINGULARITY)
    {
        R.Pitch = -90.f;
        R.Yaw   = std::atan2(YawY, YawX) * RAD2DEG;
        R.Roll  = -R.Yaw - (2.f * std::atan2(X, W) * RAD2DEG);
    }
    else if (SingularityTest > SINGULARITY)
    {
        R.Pitch = 90.f;
        R.Yaw   = std::atan2(YawY, YawX) * RAD2DEG;
        R.Roll  = R.Yaw - (2.f * std::atan2(X, W) * RAD2DEG);
    }
    else
    {
        R.Pitch = std::asin(2.f * SingularityTest) * RAD2DEG;
        R.Yaw   = std::atan2(YawY, YawX) * RAD2DEG;
        R.Roll  = std::atan2(-2.f * (W * X + Y * Z), (1.f - 2.f * (X * X + Y * Y))) * RAD2DEG;
    }
    return R;
}

// ── FTransform (rotation + translation; uniform scale 1) ──────────────────────
struct FTransform
{
    FQuat   Rotation;
    FVector Translation;

    FTransform() = default;
    FTransform(const FQuat& InRot, const FVector& InTrans) : Rotation(InRot), Translation(InTrans) {}

    FVector GetLocation() const { return Translation; }
    FQuat   GetRotation() const { return Rotation; }

    FVector TransformPosition(const FVector& V) const { return Rotation.RotateVector(V) + Translation; }

    // UE: (A * B) applies A first, then B.
    FTransform operator*(const FTransform& Other) const
    {
        FTransform R;
        R.Rotation    = Other.Rotation * Rotation;
        R.Translation = Other.Rotation.RotateVector(Translation) + Other.Translation;
        return R;
    }

    FTransform Inverse() const
    {
        const FQuat   InvRot = Rotation.Inverse();
        const FVector InvT   = InvRot.RotateVector(-Translation);
        return FTransform(InvRot, InvT);
    }
};

// ── FMath subset ───────────────────────────────────────────────────────────────
struct FMath
{
    static float Abs(float V)              { return std::fabs(V); }
    template <class T> static T Min(T A, T B) { return A < B ? A : B; }
    template <class T> static T Max(T A, T B) { return A > B ? A : B; }
    template <class T> static T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
    static bool IsNearlyEqual(float A, float B, float Tol = KINDA_SMALL_NUMBER) { return std::fabs(A - B) <= Tol; }
    static bool IsNearlyZero(float A, float Tol = KINDA_SMALL_NUMBER) { return std::fabs(A) <= Tol; }

    static FVector VInterpTo(const FVector& Cur, const FVector& Target, float Dt, float Speed)
    {
        if (Speed <= 0.f) return Target;
        const FVector Dist = Target - Cur;
        if (Dist.SizeSquared() < KINDA_SMALL_NUMBER) return Target;
        return Cur + Dist * Clamp(Dt * Speed, 0.f, 1.f);
    }

    static FQuat QInterpTo(const FQuat& Cur, const FQuat& Target, float Dt, float Speed)
    {
        if (Speed <= 0.f) return Target;
        return Slerp(Cur, Target, Clamp(Dt * Speed, 0.f, 1.f));
    }

    static FQuat Slerp(const FQuat& A, FQuat B, float Alpha)
    {
        float Cos = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
        if (Cos < 0.f) { B = FQuat(-B.X, -B.Y, -B.Z, -B.W); Cos = -Cos; }
        float s0, s1;
        if (Cos > 0.9999f) { s0 = 1.f - Alpha; s1 = Alpha; }
        else
        {
            const float Omega = std::acos(Cos), SinO = std::sin(Omega);
            s0 = std::sin((1.f - Alpha) * Omega) / SinO;
            s1 = std::sin(Alpha * Omega) / SinO;
        }
        FQuat R(s0 * A.X + s1 * B.X, s0 * A.Y + s1 * B.Y, s0 * A.Z + s1 * B.Z, s0 * A.W + s1 * B.W);
        R.Normalize();
        return R;
    }
};

// ── TArray / FName / FString (just enough for VCamConfig) ──────────────────────
template <class T>
struct TArray
{
    std::vector<T> Data;
    TArray() = default;
    TArray(std::initializer_list<T> L) : Data(L) {}
    int32 Num() const { return (int32)Data.size(); }
    void  Add(const T& V) { Data.push_back(V); }
    bool  IsValidIndex(int32 i) const { return i >= 0 && i < (int32)Data.size(); }
    T&       operator[](int32 i)       { return Data[i]; }
    const T& operator[](int32 i) const { return Data[i]; }
    T*       begin() { return Data.data(); }
    T*       end()   { return Data.data() + Data.size(); }
};

struct FString
{
    std::string S;
    FString() = default;
    FString(const char* In) : S(In ? In : "") {}
};

struct FName
{
    std::string S;
    FName() = default;
    FName(const char* In) : S(In ? In : "") {}
};
