#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// =============================================================================
//  EspTypes.hpp — pure engine value types + vector math + validation helpers
// -----------------------------------------------------------------------------
//  Shared between the memory core, the write-hacks, and the ESP pipeline. This
//  header has NO ImGui / Windows dependency on purpose: it only models the
//  raw UE5 (LWC/double) memory layouts and the math used to project them.
//  Everything is inline so it can be included by multiple translation units.
//
//  The byte layouts below are VERIFIED against the SDK and are load-bearing:
//  a wrong size/offset silently corrupts game memory on write. Do not change
//  the struct layouts or the static_asserts.
// =============================================================================

inline constexpr double Pi = 3.14159265358979323846;

struct RemoteArray {
    uintptr_t Data = 0;
    int32_t Count = 0;
    int32_t Max = 0;
};

struct Vec3d {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

struct Quatd {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 1.0;
};

// UE5 (LWC/double) FTransform layout — VERIFIED against the SDK:
//   Rotation (FQuat)  0x00, Translation (FVector) 0x20, Pad 0x38,
//   Scale3D (FVector) 0x40, Pad 0x58 ; total 0x60.
// The Pad0 between Translation and Scale3D is REQUIRED — do not remove it.
struct Transformd {
    Quatd Rotation;
    Vec3d Translation;
    double Pad0 = 0.0;
    Vec3d Scale3D;
    double Pad1 = 0.0;
};

static_assert(sizeof(Transformd) == 0x60, "FTransform must be 0x60 for this UE5.6 build");
static_assert(offsetof(Transformd, Translation) == 0x20, "Translation must be at 0x20");
static_assert(offsetof(Transformd, Scale3D) == 0x40, "Scale3D must be at 0x40 (UE5 double FTransform)");

struct Rotatord {
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
};

struct CameraPov {
    Vec3d Location;
    Rotatord Rotation;
    float FOV = 90.0f;
};

// ---- Validation -----------------------------------------------------------
inline bool IsReadablePointer(uintptr_t value)
{
    return value >= 0x10000 && value < 0x0000800000000000ULL;
}

inline bool IsFinite(double value)
{
    return std::isfinite(value);
}

inline bool IsFinite(const Vec3d& value)
{
    return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
}

inline bool IsSaneArray(const RemoteArray& value, int hardLimit)
{
    return IsReadablePointer(value.Data)
        && value.Count > 0
        && value.Count <= hardLimit
        && value.Max >= value.Count
        && value.Max <= hardLimit * 4;
}

inline bool IsSaneTransform(const Transformd& transform)
{
    if (!IsFinite(transform.Translation) || !IsFinite(transform.Scale3D)) {
        return false;
    }

    const double quatNorm =
        transform.Rotation.X * transform.Rotation.X +
        transform.Rotation.Y * transform.Rotation.Y +
        transform.Rotation.Z * transform.Rotation.Z +
        transform.Rotation.W * transform.Rotation.W;

    if (!IsFinite(quatNorm) || quatNorm < 0.25 || quatNorm > 4.0) {
        return false;
    }

    if (std::abs(transform.Scale3D.X) < 0.001 ||
        std::abs(transform.Scale3D.Y) < 0.001 ||
        std::abs(transform.Scale3D.Z) < 0.001 ||
        std::abs(transform.Scale3D.X) > 1000.0 ||
        std::abs(transform.Scale3D.Y) > 1000.0 ||
        std::abs(transform.Scale3D.Z) > 1000.0) {
        return false;
    }

    return std::abs(transform.Translation.X) < 100000000.0 &&
        std::abs(transform.Translation.Y) < 100000000.0 &&
        std::abs(transform.Translation.Z) < 100000000.0;
}

// ---- Vector math ----------------------------------------------------------
inline Vec3d operator+(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z };
}

inline Vec3d operator-(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z };
}

inline Vec3d operator*(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X * rhs.X, lhs.Y * rhs.Y, lhs.Z * rhs.Z };
}

inline Vec3d operator*(const Vec3d& lhs, double rhs)
{
    return { lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs };
}

inline double Dot(const Vec3d& lhs, const Vec3d& rhs)
{
    return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;
}

inline Vec3d Cross(const Vec3d& lhs, const Vec3d& rhs)
{
    return {
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X
    };
}

inline double Length(const Vec3d& value)
{
    return std::sqrt(Dot(value, value));
}

inline Vec3d Normalize(const Vec3d& value)
{
    const double length = Length(value);
    if (length <= 0.0001) {
        return {};
    }
    return value * (1.0 / length);
}

inline double Clamp(double value, double minValue, double maxValue)
{
    return (std::max)(minValue, (std::min)(maxValue, value));
}

inline Vec3d ForwardFromRotation(const Rotatord& rotation)
{
    const double pitch = rotation.Pitch * Pi / 180.0;
    const double yaw = rotation.Yaw * Pi / 180.0;
    const double sp = std::sin(pitch);
    const double cp = std::cos(pitch);
    const double sy = std::sin(yaw);
    const double cy = std::cos(yaw);
    return Normalize({ cp * cy, cp * sy, sp });
}

inline Vec3d RightFromYaw(double yawDegrees)
{
    const double yaw = (yawDegrees + 90.0) * Pi / 180.0;
    return Normalize({ std::cos(yaw), std::sin(yaw), 0.0 });
}

inline Vec3d RotateVector(const Quatd& q, const Vec3d& v)
{
    const Vec3d qv{ q.X, q.Y, q.Z };
    const Vec3d t = Cross(qv, v) * 2.0;
    return v + (t * q.W) + Cross(qv, t);
}

inline Vec3d TransformPosition(const Transformd& transform, const Vec3d& position)
{
    return RotateVector(transform.Rotation, position * transform.Scale3D) + transform.Translation;
}
