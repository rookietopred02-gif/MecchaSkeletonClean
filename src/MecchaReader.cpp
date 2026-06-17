#include "MecchaReader.h"

#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace Offsets {
    constexpr uintptr_t GWorld = 0x9C70620;

    constexpr uintptr_t UWorld_PersistentLevel = 0x30;
    constexpr uintptr_t UWorld_GameState = 0x1B0;
    constexpr uintptr_t UWorld_Levels = 0x1C8;
    constexpr uintptr_t UWorld_OwningGameInstance = 0x228;

    constexpr uintptr_t ULevel_Actors = 0xA0;
    constexpr uintptr_t AGameStateBase_PlayerArray = 0x2C0;

    constexpr uintptr_t UGameInstance_LocalPlayers = 0x38;
    constexpr uintptr_t UPlayer_PlayerController = 0x30;
    constexpr uintptr_t APlayerController_AcknowledgedPawn = 0x350;
    constexpr uintptr_t APlayerController_PlayerCameraManager = 0x360;
    constexpr uintptr_t APlayerCameraManager_CameraCachePrivate = 0x1530;
    constexpr uintptr_t APlayerCameraManager_LastFrameCameraCachePrivate = 0x1E00;
    constexpr uintptr_t FCameraCacheEntry_POV = 0x10;
    constexpr uintptr_t APlayerState_PawnPrivate = 0x320;

    constexpr uintptr_t AActor_PlayerBodyMesh = 0x418;
    constexpr uintptr_t BP_Main_HandBone = 0x490;
    constexpr uintptr_t BP_Main_LeftItemPositon3 = 0x4A0;
    constexpr uintptr_t BP_Main_LeftItemPositon2 = 0x4A8;
    constexpr uintptr_t BP_Main_LeftItemPositon1 = 0x4B8;
    constexpr uintptr_t BP_Main_RightItemPositon = 0x4C0;
    constexpr uintptr_t BP_Main_FirstPersonMesh = 0x4C8;
    constexpr uintptr_t BP_FirstPersonPlayerState_TargetCharacter = 0x378;
    constexpr uintptr_t BP_GameState_cLeon_LiveSurvivorsPlayerState = 0x348;
    constexpr uintptr_t BP_GameState_cLeon_HuntersPlayerState = 0x3A0;
    constexpr uintptr_t BP_Character_IsPaintViewLock = 0xB78;
    constexpr uintptr_t BP_Character_IsPaintMode = 0xB79;
    constexpr uintptr_t BP_Character_CurrentPaintColor = 0xBA8;
    constexpr uintptr_t BP_Character_IsBrushing = 0xBF8;
    constexpr uintptr_t BP_Character_CurrentPlayEmote = 0xC20;
    constexpr uintptr_t BP_Character_BodyShadow = 0xC29;
    constexpr uintptr_t BP_Character_IsHunter = 0xC2A;
    constexpr uintptr_t BP_Character_BodyVisibility = 0xC40;
    constexpr uintptr_t BP_Character_HideBlock = 0xC50;

    constexpr uintptr_t USceneComponent_ComponentToWorld = 0x1E0;
    constexpr uintptr_t UPrimitiveComponent_RenderFlags = 0x271;
    constexpr uintptr_t UPrimitiveComponent_CustomDepthStencilWriteMask = 0x29A;
    constexpr uintptr_t UPrimitiveComponent_CustomDepthStencilValue = 0x2A4;
    constexpr uintptr_t USkeletalMeshComponent_CachedComponentSpaceTransforms_Dumper = 0x9B8;
    constexpr uintptr_t USkeletalMeshComponent_CachedComponentSpaceTransforms_LiveFallback = 0x928;
}

constexpr double Pi = 3.14159265358979323846;
constexpr int PlayerHighlightStencilValue = 255;

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

struct Transformd {
    Quatd Rotation;
    Vec3d Translation;
    double Pad0 = 0.0;
    Vec3d Scale3D;
    double Pad1 = 0.0;
};

static_assert(sizeof(Transformd) == 0x60);

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

struct LinearColor {
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float A = 1.0f;
};

enum class PlayerRole {
    Unknown,
    Survivor,
    Hunter
};

struct BonePoint {
    Vec3d World{};
    ImVec2 Screen{};
    bool Visible = false;
};

struct HighlightTarget {
    uintptr_t Actor = 0;
    std::vector<uintptr_t> Components;
};

struct PlayerSnapshot {
    uintptr_t Actor = 0;
    uintptr_t BodyComponent = 0;
    uintptr_t PlayerState = 0;
    std::vector<uintptr_t> HighlightComponents;
    PlayerRole Role = PlayerRole::Unknown;
    bool IsLocal = false;
    bool IsPaintViewLock = false;
    bool IsPaintMode = false;
    bool IsBrushing = false;
    bool HasEmote = false;
    bool BodyVisibility = true;
    bool HideBlock = false;
    LinearColor CurrentPaintColor{};
    RemoteArray TransformArray{};
    Transformd ComponentToWorld{};
    Vec3d FootWorld{};
    Vec3d HeadWorld{};
    ImVec2 FootScreen{};
    ImVec2 HeadScreen{};
    ImVec2 TopLeft{};
    ImVec2 BottomRight{};
    float BoxHeight = 0.0f;
    float BoxWidth = 0.0f;
    std::vector<BonePoint> BonePoints;
};

struct TransformArrayOffsetCandidate {
    uintptr_t Offset = 0;
    const char* Name = nullptr;
};

bool IsReadablePointer(uintptr_t value)
{
    return value >= 0x10000 && value < 0x0000800000000000ULL;
}

bool IsFinite(double value)
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3d& value)
{
    return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
}

bool IsSaneArray(const RemoteArray& value, int hardLimit)
{
    return IsReadablePointer(value.Data)
        && value.Count > 0
        && value.Count <= hardLimit
        && value.Max >= value.Count
        && value.Max <= hardLimit * 4;
}

bool IsSaneTransform(const Transformd& transform)
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

Vec3d operator+(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z };
}

Vec3d operator-(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z };
}

Vec3d operator*(const Vec3d& lhs, const Vec3d& rhs)
{
    return { lhs.X * rhs.X, lhs.Y * rhs.Y, lhs.Z * rhs.Z };
}

Vec3d operator*(const Vec3d& lhs, double rhs)
{
    return { lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs };
}

double Dot(const Vec3d& lhs, const Vec3d& rhs)
{
    return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;
}

float Distance(const ImVec2& lhs, const ImVec2& rhs)
{
    const float dx = lhs.x - rhs.x;
    const float dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

double Length(const Vec3d& value)
{
    return std::sqrt(Dot(value, value));
}

Vec3d Normalize(const Vec3d& value)
{
    const double length = Length(value);
    if (length <= 0.0001) {
        return {};
    }
    return value * (1.0 / length);
}

double Clamp(double value, double minValue, double maxValue)
{
    return (std::max)(minValue, (std::min)(maxValue, value));
}

Vec3d ForwardFromRotation(const Rotatord& rotation)
{
    const double pitch = rotation.Pitch * Pi / 180.0;
    const double yaw = rotation.Yaw * Pi / 180.0;
    const double sp = std::sin(pitch);
    const double cp = std::cos(pitch);
    const double sy = std::sin(yaw);
    const double cy = std::cos(yaw);
    return Normalize({ cp * cy, cp * sy, sp });
}

Vec3d RightFromYaw(double yawDegrees)
{
    const double yaw = (yawDegrees + 90.0) * Pi / 180.0;
    return Normalize({ std::cos(yaw), std::sin(yaw), 0.0 });
}

const char* RoleText(PlayerRole role)
{
    switch (role) {
    case PlayerRole::Survivor:
        return "SURVIVOR";
    case PlayerRole::Hunter:
        return "HUNTER";
    default:
        return "UNKNOWN";
    }
}

int ClampColorChannel(int value)
{
    return (std::max)(0, (std::min)(255, value));
}

ImU32 MakeRgbColor(const int rgb[3], int alpha)
{
    return IM_COL32(
        ClampColorChannel(rgb[0]),
        ClampColorChannel(rgb[1]),
        ClampColorChannel(rgb[2]),
        ClampColorChannel(alpha));
}

LinearColor MakeLinearColor(const int rgb[3])
{
    return {
        static_cast<float>(ClampColorChannel(rgb[0])) / 255.0f,
        static_cast<float>(ClampColorChannel(rgb[1])) / 255.0f,
        static_cast<float>(ClampColorChannel(rgb[2])) / 255.0f,
        1.0f
    };
}

ImU32 RoleColor(PlayerRole role, const int survivorRgb[3], const int hunterRgb[3], int alpha = 245)
{
    switch (role) {
    case PlayerRole::Survivor:
        return MakeRgbColor(survivorRgb, alpha);
    case PlayerRole::Hunter:
        return MakeRgbColor(hunterRgb, alpha);
    default:
        return IM_COL32(210, 210, 210, ClampColorChannel(alpha));
    }
}

bool IsUsableScreenPoint(const ImVec2& point, ImVec2 viewportSize, float marginScale)
{
    const float marginX = viewportSize.x * marginScale;
    const float marginY = viewportSize.y * marginScale;
    return point.x >= -marginX && point.x <= viewportSize.x + marginX &&
        point.y >= -marginY && point.y <= viewportSize.y + marginY;
}

bool IsPointInExpandedRect(const ImVec2& point, const ImVec2& topLeft, const ImVec2& bottomRight, float padX, float padY)
{
    return point.x >= topLeft.x - padX && point.x <= bottomRight.x + padX &&
        point.y >= topLeft.y - padY && point.y <= bottomRight.y + padY;
}

bool ComputeBoneScreenBounds(const std::vector<BonePoint>& bonePoints,
    float padding,
    ImVec2& outTopLeft,
    ImVec2& outBottomRight,
    int& outVisibleCount)
{
    float minX = (std::numeric_limits<float>::max)();
    float minY = (std::numeric_limits<float>::max)();
    float maxX = (std::numeric_limits<float>::lowest)();
    float maxY = (std::numeric_limits<float>::lowest)();
    int visibleCount = 0;

    for (const BonePoint& point : bonePoints) {
        if (!point.Visible) {
            continue;
        }

        minX = (std::min)(minX, point.Screen.x);
        minY = (std::min)(minY, point.Screen.y);
        maxX = (std::max)(maxX, point.Screen.x);
        maxY = (std::max)(maxY, point.Screen.y);
        ++visibleCount;
    }

    outVisibleCount = visibleCount;
    if (visibleCount < 4 || maxX <= minX || maxY <= minY) {
        return false;
    }

    outTopLeft = ImVec2(minX - padding, minY - padding);
    outBottomRight = ImVec2(maxX + padding, maxY + padding);
    return true;
}

void DrawCornerBox(ImDrawList* drawList, const ImVec2& topLeft, const ImVec2& bottomRight, ImU32 color, float thickness)
{
    const float width = (std::max)(1.0f, bottomRight.x - topLeft.x);
    const float height = (std::max)(1.0f, bottomRight.y - topLeft.y);
    const float lineX = (std::max)(7.0f, (std::min)(width * 0.28f, 24.0f));
    const float lineY = (std::max)(7.0f, (std::min)(height * 0.22f, 26.0f));

    auto drawCorners = [&](ImU32 drawColor, float drawThickness, float offset) {
        const ImVec2 tl(topLeft.x - offset, topLeft.y - offset);
        const ImVec2 br(bottomRight.x + offset, bottomRight.y + offset);
        drawList->AddLine(tl, ImVec2(tl.x + lineX, tl.y), drawColor, drawThickness);
        drawList->AddLine(tl, ImVec2(tl.x, tl.y + lineY), drawColor, drawThickness);
        drawList->AddLine(ImVec2(br.x - lineX, tl.y), ImVec2(br.x, tl.y), drawColor, drawThickness);
        drawList->AddLine(ImVec2(br.x, tl.y), ImVec2(br.x, tl.y + lineY), drawColor, drawThickness);
        drawList->AddLine(ImVec2(tl.x, br.y - lineY), ImVec2(tl.x, br.y), drawColor, drawThickness);
        drawList->AddLine(ImVec2(tl.x, br.y), ImVec2(tl.x + lineX, br.y), drawColor, drawThickness);
        drawList->AddLine(ImVec2(br.x - lineX, br.y), br, drawColor, drawThickness);
        drawList->AddLine(ImVec2(br.x, br.y - lineY), br, drawColor, drawThickness);
    };

    drawCorners(IM_COL32(0, 0, 0, 190), thickness + 1.2f, 1.0f);
    drawCorners(color, thickness, 0.0f);
}

Vec3d Cross(const Vec3d& lhs, const Vec3d& rhs)
{
    return {
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X
    };
}

Vec3d RotateVector(const Quatd& q, const Vec3d& v)
{
    const Vec3d qv{ q.X, q.Y, q.Z };
    const Vec3d t = Cross(qv, v) * 2.0;
    return v + (t * q.W) + Cross(qv, t);
}

Vec3d TransformPosition(const Transformd& transform, const Vec3d& position)
{
    return RotateVector(transform.Rotation, position * transform.Scale3D) + transform.Translation;
}

bool WorldToScreen(const CameraPov& camera, const Vec3d& world, ImVec2 viewportSize, ImVec2& out)
{
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f || camera.FOV <= 1.0f) {
        return false;
    }

    const double pitch = camera.Rotation.Pitch * Pi / 180.0;
    const double yaw = camera.Rotation.Yaw * Pi / 180.0;
    const double roll = camera.Rotation.Roll * Pi / 180.0;

    const double sp = std::sin(pitch);
    const double cp = std::cos(pitch);
    const double sy = std::sin(yaw);
    const double cy = std::cos(yaw);
    const double sr = std::sin(roll);
    const double cr = std::cos(roll);

    const Vec3d axisX{ cp * cy, cp * sy, sp };
    const Vec3d axisY{ sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
    const Vec3d axisZ{ -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp };

    const Vec3d delta = world - camera.Location;
    const Vec3d transformed{
        Dot(delta, axisY),
        Dot(delta, axisZ),
        Dot(delta, axisX)
    };

    if (transformed.Z < 1.0) {
        return false;
    }

    const double centerX = static_cast<double>(viewportSize.x) * 0.5;
    const double centerY = static_cast<double>(viewportSize.y) * 0.5;
    const double focal = centerX / std::tan((static_cast<double>(camera.FOV) * Pi / 180.0) * 0.5);

    out.x = static_cast<float>(centerX + transformed.X * focal / transformed.Z);
    out.y = static_cast<float>(centerY - transformed.Y * focal / transformed.Z);

    return out.x > -viewportSize.x && out.x < viewportSize.x * 2.0f &&
        out.y > -viewportSize.y && out.y < viewportSize.y * 2.0f;
}

} // namespace

MecchaReader::MecchaReader()
{
    enableEsp = true;
    autoAttach = true;
    skipLocalPawn = false;
    drawFootDot = true;
    drawBoxes = true;
    drawCornerBoxes = true;
    useBoneBoundsForBoxes = true;
    drawCrosshairEnemyLines = true;
    drawBoneIndices = false;
    drawRoleEsp = true;
    drawStateEsp = true;
    onlyPlayerBodyMesh = true;
    enableMemoryWrites = true;
    enablePlayerHighlight = true;
    enableSeekerWarning = true;
    enableFreecam = false;

    maxActorsPerFrame = 8192;
    bodyTransformCount = 28;
    maxBonesPerMesh = 128;
    survivorStencilValue = 1;
    hunterStencilValue = 2;
    customDepthWriteMask = 1;
    playerHighlightRgb[0] = 255;
    playerHighlightRgb[1] = 0;
    playerHighlightRgb[2] = 255;
    lineThickness = 1.6f;
    dotRadius = 2.0f;
    minCameraDistance = 0.0f;
    boxWorldHeight = 165.0f;
    boxWidthRatio = 0.42f;
    boxPaddingPixels = 5.0f;
    seekerWarningAngleDegrees = 18.0f;
    seekerWarningMaxDistance = 6500.0f;
    freecamSpeed = 700.0f;
    freecamFastMultiplier = 3.0f;
    freecamMouseSensitivity = 0.12f;
}

MecchaReader::~MecchaReader()
{
    Detach();
}

void MecchaReader::SetStatus(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsprintf_s(status_, fmt, args);
    va_end(args);
}

bool MecchaReader::ReadMemory(uintptr_t address, void* out, size_t size) const
{
    if (!process_ || !IsReadablePointer(address)) {
        return false;
    }

    SIZE_T bytesRead = 0;
    return ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), out, size, &bytesRead) != 0
        && bytesRead == size;
}

bool MecchaReader::WriteMemory(uintptr_t address, const void* buffer, size_t size) const
{
    if (!process_ || !IsReadablePointer(address)) {
        return false;
    }

    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(process_, reinterpret_cast<LPVOID>(address), buffer, size, &bytesWritten) != 0
        && bytesWritten == size;
}

bool MecchaReader::ApplyCustomDepthHighlight(uintptr_t primitiveComponent, int stencilValue)
{
    if (!process_ || !IsReadablePointer(primitiveComponent)) {
        return false;
    }

    auto backup = customDepthBackups_.find(primitiveComponent);
    if (backup == customDepthBackups_.end()) {
        CustomDepthBackup original{};
        if (!ReadMemory(primitiveComponent + Offsets::UPrimitiveComponent_RenderFlags,
            &original.renderFlags, sizeof(original.renderFlags)) ||
            !ReadMemory(primitiveComponent + Offsets::UPrimitiveComponent_CustomDepthStencilWriteMask,
                &original.writeMask, sizeof(original.writeMask)) ||
            !ReadMemory(primitiveComponent + Offsets::UPrimitiveComponent_CustomDepthStencilValue,
                &original.stencilValue, sizeof(original.stencilValue))) {
            ++lastWriteFailCount_;
            return false;
        }
        backup = customDepthBackups_.emplace(primitiveComponent, original).first;
    }

    const uint8_t enabledFlags = static_cast<uint8_t>(backup->second.renderFlags | 0x40);
    const uint8_t writeMask = static_cast<uint8_t>((std::max)(0, (std::min)(255, customDepthWriteMask)));
    const int32_t safeStencilValue = static_cast<int32_t>((std::max)(0, (std::min)(255, stencilValue)));

    if (!WriteMemory(primitiveComponent + Offsets::UPrimitiveComponent_RenderFlags,
        &enabledFlags, sizeof(enabledFlags)) ||
        !WriteMemory(primitiveComponent + Offsets::UPrimitiveComponent_CustomDepthStencilWriteMask,
            &writeMask, sizeof(writeMask)) ||
        !WriteMemory(primitiveComponent + Offsets::UPrimitiveComponent_CustomDepthStencilValue,
            &safeStencilValue, sizeof(safeStencilValue))) {
        ++lastWriteFailCount_;
        return false;
    }

    return true;
}

void MecchaReader::RestoreCustomDepthHighlights()
{
    if (!process_) {
        customDepthBackups_.clear();
        return;
    }

    for (const auto& [component, backup] : customDepthBackups_) {
        WriteMemory(component + Offsets::UPrimitiveComponent_RenderFlags,
            &backup.renderFlags, sizeof(backup.renderFlags));
        WriteMemory(component + Offsets::UPrimitiveComponent_CustomDepthStencilWriteMask,
            &backup.writeMask, sizeof(backup.writeMask));
        WriteMemory(component + Offsets::UPrimitiveComponent_CustomDepthStencilValue,
            &backup.stencilValue, sizeof(backup.stencilValue));
    }
    customDepthBackups_.clear();
}

void MecchaReader::RestoreCustomDepthHighlightsExcept(const std::unordered_set<uintptr_t>& keep)
{
    if (!process_) {
        customDepthBackups_.clear();
        return;
    }

    std::vector<uintptr_t> removeList;
    for (const auto& [component, backup] : customDepthBackups_) {
        if (keep.find(component) != keep.end()) {
            continue;
        }

        WriteMemory(component + Offsets::UPrimitiveComponent_RenderFlags,
            &backup.renderFlags, sizeof(backup.renderFlags));
        WriteMemory(component + Offsets::UPrimitiveComponent_CustomDepthStencilWriteMask,
            &backup.writeMask, sizeof(backup.writeMask));
        WriteMemory(component + Offsets::UPrimitiveComponent_CustomDepthStencilValue,
            &backup.stencilValue, sizeof(backup.stencilValue));
        removeList.push_back(component);
    }

    for (uintptr_t component : removeList) {
        customDepthBackups_.erase(component);
    }
}

bool MecchaReader::ApplyPaintHighlightColor(uintptr_t actor, const int rgb[3])
{
    if (!process_ || !IsReadablePointer(actor)) {
        return false;
    }

    auto backup = paintColorBackups_.find(actor);
    if (backup == paintColorBackups_.end()) {
        LinearColor original{};
        if (!ReadMemory(actor + Offsets::BP_Character_CurrentPaintColor, &original, sizeof(original))) {
            ++lastWriteFailCount_;
            return false;
        }

        uint8_t bodyShadow = 0;
        uint8_t bodyVisibility = 1;
        uint8_t hideBlock = 0;
        ReadMemory(actor + Offsets::BP_Character_BodyShadow, &bodyShadow, sizeof(bodyShadow));
        ReadMemory(actor + Offsets::BP_Character_BodyVisibility, &bodyVisibility, sizeof(bodyVisibility));
        ReadMemory(actor + Offsets::BP_Character_HideBlock, &hideBlock, sizeof(hideBlock));

        PaintColorBackup saved{};
        saved.r = original.R;
        saved.g = original.G;
        saved.b = original.B;
        saved.a = original.A;
        saved.bodyShadow = bodyShadow;
        saved.bodyVisibility = bodyVisibility;
        saved.hideBlock = hideBlock;
        backup = paintColorBackups_.emplace(actor, saved).first;
    }

    const LinearColor highlightColor = MakeLinearColor(rgb);
    if (!WriteMemory(actor + Offsets::BP_Character_CurrentPaintColor,
        &highlightColor, sizeof(highlightColor))) {
        ++lastWriteFailCount_;
        return false;
    }

    const uint8_t bodyShadowOff = 0;
    const uint8_t bodyVisible = 1;
    const uint8_t hideBlockOff = 0;
    WriteMemory(actor + Offsets::BP_Character_BodyShadow, &bodyShadowOff, sizeof(bodyShadowOff));
    WriteMemory(actor + Offsets::BP_Character_BodyVisibility, &bodyVisible, sizeof(bodyVisible));
    WriteMemory(actor + Offsets::BP_Character_HideBlock, &hideBlockOff, sizeof(hideBlockOff));

    return true;
}

void MecchaReader::RestorePaintHighlightColors()
{
    if (!process_) {
        paintColorBackups_.clear();
        return;
    }

    for (const auto& [actor, backup] : paintColorBackups_) {
        const LinearColor original{ backup.r, backup.g, backup.b, backup.a };
        WriteMemory(actor + Offsets::BP_Character_CurrentPaintColor, &original, sizeof(original));
        WriteMemory(actor + Offsets::BP_Character_BodyShadow, &backup.bodyShadow, sizeof(backup.bodyShadow));
        WriteMemory(actor + Offsets::BP_Character_BodyVisibility, &backup.bodyVisibility, sizeof(backup.bodyVisibility));
        WriteMemory(actor + Offsets::BP_Character_HideBlock, &backup.hideBlock, sizeof(backup.hideBlock));
    }
    paintColorBackups_.clear();
}

void MecchaReader::RestorePaintHighlightColorsExcept(const std::unordered_set<uintptr_t>& keep)
{
    if (!process_) {
        paintColorBackups_.clear();
        return;
    }

    std::vector<uintptr_t> removeList;
    for (const auto& [actor, backup] : paintColorBackups_) {
        if (keep.find(actor) != keep.end()) {
            continue;
        }

        const LinearColor original{ backup.r, backup.g, backup.b, backup.a };
        WriteMemory(actor + Offsets::BP_Character_CurrentPaintColor, &original, sizeof(original));
        WriteMemory(actor + Offsets::BP_Character_BodyShadow, &backup.bodyShadow, sizeof(backup.bodyShadow));
        WriteMemory(actor + Offsets::BP_Character_BodyVisibility, &backup.bodyVisibility, sizeof(backup.bodyVisibility));
        WriteMemory(actor + Offsets::BP_Character_HideBlock, &backup.hideBlock, sizeof(backup.hideBlock));
        removeList.push_back(actor);
    }

    for (uintptr_t actor : removeList) {
        paintColorBackups_.erase(actor);
    }
}

void MecchaReader::RestoreFreecamCamera()
{
    if (!process_ || !freecamOriginalValid_ || !IsReadablePointer(freecamCameraManager_)) {
        freecamActive_ = false;
        freecamOriginalValid_ = false;
        freecamCameraManager_ = 0;
        freecamMouseTracking_ = false;
        return;
    }

    WriteMemory(freecamCameraManager_ + Offsets::APlayerCameraManager_CameraCachePrivate + Offsets::FCameraCacheEntry_POV,
        &freecamOriginalPov_, sizeof(freecamOriginalPov_));
    WriteMemory(freecamCameraManager_ + Offsets::APlayerCameraManager_LastFrameCameraCachePrivate + Offsets::FCameraCacheEntry_POV,
        &freecamOriginalPov_, sizeof(freecamOriginalPov_));

    freecamActive_ = false;
    freecamOriginalValid_ = false;
    freecamCameraManager_ = 0;
    freecamMouseTracking_ = false;
}

bool MecchaReader::TryAttachProcess(const wchar_t* processName)
{
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        SetStatus("Process snapshot failed (%lu)", GetLastError());
        return false;
    }

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (pid == 0) {
        return false;
    }

    constexpr DWORD desiredAccess =
        PROCESS_QUERY_LIMITED_INFORMATION |
        PROCESS_VM_READ |
        PROCESS_VM_WRITE |
        PROCESS_VM_OPERATION;
    HANDLE process = OpenProcess(desiredAccess, FALSE, pid);
    if (!process) {
        SetStatus("OpenProcess failed (%lu)", GetLastError());
        return false;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    HANDLE moduleSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (moduleSnapshot == INVALID_HANDLE_VALUE) {
        CloseHandle(process);
        SetStatus("Module snapshot failed (%lu)", GetLastError());
        return false;
    }

    uintptr_t moduleBase = 0;
    DWORD moduleSize = 0;
    if (Module32FirstW(moduleSnapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, processName) == 0) {
                moduleBase = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                moduleSize = module.modBaseSize;
                break;
            }
        } while (Module32NextW(moduleSnapshot, &module));
    }
    CloseHandle(moduleSnapshot);

    if (!moduleBase) {
        CloseHandle(process);
        SetStatus("Module base not found");
        return false;
    }

    process_ = process;
    pid_ = pid;
    moduleBase_ = moduleBase;
    moduleSize_ = moduleSize;
    SetStatus("Attached: PID %lu, base 0x%llX", pid_, static_cast<unsigned long long>(moduleBase_));
    return true;
}

bool MecchaReader::Attach()
{
    Detach();

    if (TryAttachProcess(L"PenguinHotel-Win64-Shipping.exe")) {
        return true;
    }

    SetStatus("PenguinHotel-Win64-Shipping.exe not found");
    return false;
}

void MecchaReader::Detach()
{
    RestoreFreecamCamera();
    RestoreCustomDepthHighlights();
    RestorePaintHighlightColors();

    if (process_) {
        CloseHandle(process_);
    }

    pid_ = 0;
    process_ = nullptr;
    moduleBase_ = 0;
    moduleSize_ = 0;
    lastWorld_ = 0;
    lastPlayerController_ = 0;
    lastLocalPawn_ = 0;
    lastTransformArrayOffset_ = 0;
    lastActorCount_ = 0;
    lastComponentCandidates_ = 0;
    lastBoxCount_ = 0;
    lastTargetCount_ = 0;
    lastCrosshairLineCount_ = 0;
    lastIndexCount_ = 0;
    lastRoleCount_ = 0;
    lastStateCount_ = 0;
    lastHighlightedCount_ = 0;
    lastPaintHighlightCount_ = 0;
    lastWarningCount_ = 0;
    lastWriteFailCount_ = 0;
    lastAutoCamoApplied_ = 0;
    lastAutoCamoR_ = 0.0f;
    lastAutoCamoG_ = 0.0f;
    lastAutoCamoB_ = 0.0f;
    customDepthBackups_.clear();
    paintColorBackups_.clear();
    autoCamoRequested_ = false;
    enableFreecam = false;
    freecamActive_ = false;
    freecamOriginalValid_ = false;
    freecamMouseTracking_ = false;
    freecamCameraManager_ = 0;
    lastFreecamTick_ = 0;
}

void MecchaReader::DrawControls()
{
    ImGui::TextUnformatted("MECCHA ESP");
    ImGui::Separator();

    if (!IsAttached()) {
        if (ImGui::Button("Attach to game", ImVec2(-1, 24))) {
            Attach();
        }
    }
    else {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Attached");
        ImGui::Text("PID: %lu", pid_);
        ImGui::Text("Base: 0x%llX", static_cast<unsigned long long>(moduleBase_));
        if (ImGui::Button("Detach", ImVec2(-1, 24))) {
            Detach();
            SetStatus("Detached");
        }
    }

    ImGui::Checkbox("Enable ESP", &enableEsp);
    ImGui::Checkbox("Auto attach", &autoAttach);
    ImGui::Checkbox("Skip local/spectated pawn", &skipLocalPawn);
    ImGui::Checkbox("Draw boxes", &drawBoxes);
    ImGui::Checkbox("Corner boxes", &drawCornerBoxes);
    ImGui::Checkbox("Use bone bounds for boxes", &useBoneBoundsForBoxes);
    ImGui::Checkbox("Draw crosshair enemy lines", &drawCrosshairEnemyLines);
    ImGui::Checkbox("Draw bone indices", &drawBoneIndices);
    ImGui::Checkbox("Draw role ESP", &drawRoleEsp);
    ImGui::Checkbox("Draw state ESP", &drawStateEsp);
    ImGui::Checkbox("Draw foot anchor", &drawFootDot);
    ImGui::Checkbox("Only player body mesh", &onlyPlayerBodyMesh);

    ImGui::Separator();
    enableMemoryWrites = true;
    ImGui::Checkbox("Highlight players", &enablePlayerHighlight);
    ImGui::SliderInt3("Highlight RGB", playerHighlightRgb, 0, 255);

    ImGui::Separator();
    ImGui::Checkbox("Seeker view warning", &enableSeekerWarning);
    ImGui::SliderFloat("Warning cone deg", &seekerWarningAngleDegrees, 3.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("Warning max distance", &seekerWarningMaxDistance, 500.0f, 20000.0f, "%.0f");
    if (ImGui::Checkbox("Freecam (F6)", &enableFreecam) && !enableFreecam) {
        RestoreFreecamCamera();
    }
    ImGui::SliderFloat("Freecam speed", &freecamSpeed, 50.0f, 4000.0f, "%.0f");
    ImGui::SliderFloat("Freecam fast x", &freecamFastMultiplier, 1.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Freecam mouse sens", &freecamMouseSensitivity, 0.02f, 1.0f, "%.2f");
    if (ImGui::Button("Sample & apply camo color (F7)", ImVec2(-1, 24))) {
        autoCamoRequested_ = true;
    }

    ImGui::Separator();
    ImGui::SliderInt("Max actors/frame", &maxActorsPerFrame, 128, 20000);
    ImGui::SliderInt("Body transform count", &bodyTransformCount, 1, 128);
    ImGui::SliderInt("Max bones/mesh", &maxBonesPerMesh, 8, 256);
    ImGui::SliderFloat("Box world height", &boxWorldHeight, 80.0f, 260.0f, "%.0f");
    ImGui::SliderFloat("Box width ratio", &boxWidthRatio, 0.20f, 0.90f, "%.2f");
    ImGui::SliderFloat("Box padding px", &boxPaddingPixels, 0.0f, 24.0f, "%.0f");
    ImGui::SliderFloat("Line thickness", &lineThickness, 0.5f, 5.0f, "%.1f");
    ImGui::SliderFloat("Dot radius", &dotRadius, 1.0f, 6.0f, "%.1f");
    ImGui::SliderFloat("Min camera distance", &minCameraDistance, 0.0f, 600.0f, "%.0f");

    ImGui::Separator();
    ImGui::Text("World: 0x%llX", static_cast<unsigned long long>(lastWorld_));
    ImGui::Text("PC: 0x%llX", static_cast<unsigned long long>(lastPlayerController_));
    ImGui::Text("LocalPawn: 0x%llX", static_cast<unsigned long long>(lastLocalPawn_));
    ImGui::Text("Transform offset: 0x%llX", static_cast<unsigned long long>(lastTransformArrayOffset_));
    ImGui::Text("Actors: %d | Components: %d", lastActorCount_, lastComponentCandidates_);
    ImGui::Text("Targets: %d | Boxes: %d | Lines: %d | Indices: %d",
        lastTargetCount_, lastBoxCount_, lastCrosshairLineCount_, lastIndexCount_);
    ImGui::Text("Roles: %d | States: %d | Depth: %d | Paint: %d | Warnings: %d",
        lastRoleCount_, lastStateCount_, lastHighlightedCount_, lastPaintHighlightCount_, lastWarningCount_);
    ImGui::Text("Write fails: %d | Camo applied: %d | RGB %.2f %.2f %.2f",
        lastWriteFailCount_, lastAutoCamoApplied_, lastAutoCamoR_, lastAutoCamoG_, lastAutoCamoB_);
    ImGui::Text("Freecam: %s | Depth backups: %d | Paint backups: %d",
        freecamActive_ ? "active" : "off",
        static_cast<int>(customDepthBackups_.size()),
        static_cast<int>(paintColorBackups_.size()));
    ImGui::TextWrapped("Status: %s", status_);
}

void MecchaReader::DrawEsp(ImDrawList* drawList, ImVec2 viewportSize, ImVec2 screenOrigin)
{
    lastActorCount_ = 0;
    lastComponentCandidates_ = 0;
    lastBoxCount_ = 0;
    lastTargetCount_ = 0;
    lastCrosshairLineCount_ = 0;
    lastIndexCount_ = 0;
    lastRoleCount_ = 0;
    lastStateCount_ = 0;
    lastHighlightedCount_ = 0;
    lastPaintHighlightCount_ = 0;
    lastWarningCount_ = 0;
    lastWriteFailCount_ = 0;
    lastTransformArrayOffset_ = 0;

    if ((GetAsyncKeyState(VK_F6) & 1) != 0) {
        enableFreecam = !enableFreecam;
        if (!enableFreecam) {
            RestoreFreecamCamera();
        }
    }
    if ((GetAsyncKeyState(VK_F7) & 1) != 0) {
        autoCamoRequested_ = true;
    }

    if (!enableEsp) {
        RestoreCustomDepthHighlights();
        RestorePaintHighlightColors();
        if (!enableFreecam) {
            RestoreFreecamCamera();
        }
        return;
    }

    if (!IsAttached()) {
        if (autoAttach) {
            Attach();
        }
        if (!IsAttached()) {
            return;
        }
    }

    auto readPtr = [this](uintptr_t address, uintptr_t& out) -> bool {
        out = 0;
        return ReadMemory(address, &out, sizeof(out)) && IsReadablePointer(out);
    };

    auto readRawPtr = [this](uintptr_t address, uintptr_t& out) -> bool {
        out = 0;
        return ReadMemory(address, &out, sizeof(out));
    };

    auto readArray = [this](uintptr_t address, RemoteArray& out) -> bool {
        out = {};
        return ReadMemory(address, &out, sizeof(out));
    };

    auto readBool = [this](uintptr_t address, bool& out) -> bool {
        uint8_t raw = 0;
        if (!ReadMemory(address, &raw, sizeof(raw))) {
            return false;
        }
        out = raw != 0;
        return true;
    };

    auto cameraToRemote = [](const CameraPov& camera) -> RemoteCameraPov {
        RemoteCameraPov remote{};
        remote.LocationX = camera.Location.X;
        remote.LocationY = camera.Location.Y;
        remote.LocationZ = camera.Location.Z;
        remote.Pitch = camera.Rotation.Pitch;
        remote.Yaw = camera.Rotation.Yaw;
        remote.Roll = camera.Rotation.Roll;
        remote.FOV = camera.FOV;
        return remote;
    };

    auto remoteToCamera = [](const RemoteCameraPov& remote) -> CameraPov {
        CameraPov camera{};
        camera.Location = { remote.LocationX, remote.LocationY, remote.LocationZ };
        camera.Rotation = { remote.Pitch, remote.Yaw, remote.Roll };
        camera.FOV = remote.FOV;
        return camera;
    };

    uintptr_t world = 0;
    if (!readPtr(moduleBase_ + Offsets::GWorld, world)) {
        lastWorld_ = 0;
        SetStatus("GWorld read failed at +0x%llX", static_cast<unsigned long long>(Offsets::GWorld));
        return;
    }
    lastWorld_ = world;

    uintptr_t gameInstance = 0;
    RemoteArray localPlayers{};
    uintptr_t localPlayer = 0;
    uintptr_t playerController = 0;
    uintptr_t cameraManager = 0;
    if (!readPtr(world + Offsets::UWorld_OwningGameInstance, gameInstance) ||
        !readArray(gameInstance + Offsets::UGameInstance_LocalPlayers, localPlayers) ||
        !IsSaneArray(localPlayers, 16) ||
        !readPtr(localPlayers.Data, localPlayer) ||
        !readPtr(localPlayer + Offsets::UPlayer_PlayerController, playerController) ||
        !readPtr(playerController + Offsets::APlayerController_PlayerCameraManager, cameraManager)) {
        lastPlayerController_ = 0;
        lastLocalPawn_ = 0;
        SetStatus("Local player/camera chain failed");
        return;
    }

    lastPlayerController_ = playerController;
    readPtr(playerController + Offsets::APlayerController_AcknowledgedPawn, lastLocalPawn_);

    CameraPov camera{};
    if (!ReadMemory(cameraManager + Offsets::APlayerCameraManager_CameraCachePrivate + Offsets::FCameraCacheEntry_POV,
        &camera, sizeof(camera)) ||
        !IsFinite(camera.Location) ||
        !IsFinite(camera.Rotation.Pitch) ||
        !IsFinite(camera.Rotation.Yaw) ||
        !IsFinite(camera.Rotation.Roll)) {
        SetStatus("CameraCachePrivate.POV read failed");
        return;
    }
    if (camera.FOV < 5.0f || camera.FOV > 170.0f) {
        camera.FOV = 90.0f;
    }

    if (!enableFreecam || !enableMemoryWrites) {
        if (freecamActive_) {
            RestoreFreecamCamera();
        }
    }
    else {
        if (!freecamActive_ || freecamCameraManager_ != cameraManager) {
            if (freecamActive_) {
                RestoreFreecamCamera();
            }
            freecamActive_ = true;
            freecamOriginalValid_ = true;
            freecamCameraManager_ = cameraManager;
            freecamOriginalPov_ = cameraToRemote(camera);
            freecamCurrentPov_ = freecamOriginalPov_;
            lastFreecamTick_ = GetTickCount64();
            freecamMouseTracking_ = false;
        }

        const ULONGLONG nowTick = GetTickCount64();
        double deltaSeconds = static_cast<double>(nowTick - lastFreecamTick_) / 1000.0;
        deltaSeconds = Clamp(deltaSeconds, 0.001, 0.100);
        lastFreecamTick_ = nowTick;

        if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                if (freecamMouseTracking_) {
                    const int dx = cursor.x - freecamLastCursor_.x;
                    const int dy = cursor.y - freecamLastCursor_.y;
                    freecamCurrentPov_.Yaw += static_cast<double>(dx) * freecamMouseSensitivity;
                    freecamCurrentPov_.Pitch = Clamp(
                        freecamCurrentPov_.Pitch - static_cast<double>(dy) * freecamMouseSensitivity,
                        -89.0, 89.0);
                }
                freecamLastCursor_ = cursor;
                freecamMouseTracking_ = true;
            }
        }
        else {
            freecamMouseTracking_ = false;
        }

        Vec3d movement{};
        const Rotatord freecamRot{ freecamCurrentPov_.Pitch, freecamCurrentPov_.Yaw, freecamCurrentPov_.Roll };
        const Vec3d forward = ForwardFromRotation(freecamRot);
        const Vec3d right = RightFromYaw(freecamCurrentPov_.Yaw);
        if ((GetAsyncKeyState('W') & 0x8000) != 0) movement = movement + forward;
        if ((GetAsyncKeyState('S') & 0x8000) != 0) movement = movement - forward;
        if ((GetAsyncKeyState('D') & 0x8000) != 0) movement = movement + right;
        if ((GetAsyncKeyState('A') & 0x8000) != 0) movement = movement - right;
        if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) movement.Z += 1.0;
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) movement.Z -= 1.0;

        const double moveLength = Length(movement);
        if (moveLength > 0.0001) {
            movement = movement * (1.0 / moveLength);
            const double speed = static_cast<double>(freecamSpeed) *
                (((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) ? static_cast<double>(freecamFastMultiplier) : 1.0);
            freecamCurrentPov_.LocationX += movement.X * speed * deltaSeconds;
            freecamCurrentPov_.LocationY += movement.Y * speed * deltaSeconds;
            freecamCurrentPov_.LocationZ += movement.Z * speed * deltaSeconds;
        }

        if (!WriteMemory(cameraManager + Offsets::APlayerCameraManager_CameraCachePrivate + Offsets::FCameraCacheEntry_POV,
            &freecamCurrentPov_, sizeof(freecamCurrentPov_)) ||
            !WriteMemory(cameraManager + Offsets::APlayerCameraManager_LastFrameCameraCachePrivate + Offsets::FCameraCacheEntry_POV,
                &freecamCurrentPov_, sizeof(freecamCurrentPov_))) {
            ++lastWriteFailCount_;
        }
        camera = remoteToCamera(freecamCurrentPov_);
    }

    std::unordered_map<uintptr_t, PlayerRole> roleByActor;
    std::unordered_map<uintptr_t, uintptr_t> playerStateByActor;
    uintptr_t gameState = 0;
    if (readPtr(world + Offsets::UWorld_GameState, gameState)) {
        auto mapPlayerState = [&](uintptr_t playerState, PlayerRole role) {
            if (!IsReadablePointer(playerState)) {
                return;
            }

            uintptr_t targetActor = 0;
            if (readPtr(playerState + Offsets::BP_FirstPersonPlayerState_TargetCharacter, targetActor) ||
                readPtr(playerState + Offsets::APlayerState_PawnPrivate, targetActor)) {
                roleByActor[targetActor] = role;
                playerStateByActor[targetActor] = playerState;
            }
        };

        auto readPlayerStateArray = [&](uintptr_t address, PlayerRole role) {
            RemoteArray playerStates{};
            if (!readArray(address, playerStates) || !IsSaneArray(playerStates, 128)) {
                return;
            }
            const int readCount = (std::min)(playerStates.Count, 128);
            std::vector<uintptr_t> values(static_cast<size_t>(readCount));
            if (!ReadMemory(playerStates.Data, values.data(), values.size() * sizeof(uintptr_t))) {
                return;
            }
            for (uintptr_t playerState : values) {
                mapPlayerState(playerState, role);
            }
        };

        readPlayerStateArray(gameState + Offsets::BP_GameState_cLeon_HuntersPlayerState, PlayerRole::Hunter);
        readPlayerStateArray(gameState + Offsets::BP_GameState_cLeon_LiveSurvivorsPlayerState, PlayerRole::Survivor);
        readPlayerStateArray(gameState + Offsets::AGameStateBase_PlayerArray, PlayerRole::Unknown);
    }

    std::vector<uintptr_t> levels;
    uintptr_t persistentLevel = 0;
    if (readPtr(world + Offsets::UWorld_PersistentLevel, persistentLevel)) {
        levels.push_back(persistentLevel);
    }

    RemoteArray levelArray{};
    if (readArray(world + Offsets::UWorld_Levels, levelArray) && IsSaneArray(levelArray, 256)) {
        std::vector<uintptr_t> levelPtrs(static_cast<size_t>((std::min)(levelArray.Count, 256)));
        if (ReadMemory(levelArray.Data, levelPtrs.data(), levelPtrs.size() * sizeof(uintptr_t))) {
            for (uintptr_t level : levelPtrs) {
                if (IsReadablePointer(level) && std::find(levels.begin(), levels.end(), level) == levels.end()) {
                    levels.push_back(level);
                }
            }
        }
    }

    std::vector<uintptr_t> actors;
    actors.reserve(static_cast<size_t>((std::max)(128, maxActorsPerFrame)));
    std::unordered_set<uintptr_t> seenActors;
    for (uintptr_t level : levels) {
        RemoteArray actorArray{};
        if (!readArray(level + Offsets::ULevel_Actors, actorArray) || !IsSaneArray(actorArray, 50000)) {
            continue;
        }

        const int remainingActorSlots = (std::max)(0, maxActorsPerFrame - static_cast<int>(actors.size()));
        if (remainingActorSlots == 0) {
            break;
        }

        const int readCount = (std::min)(actorArray.Count, remainingActorSlots);
        std::vector<uintptr_t> actorPtrs(static_cast<size_t>(readCount));
        if (!ReadMemory(actorArray.Data, actorPtrs.data(), actorPtrs.size() * sizeof(uintptr_t))) {
            continue;
        }

        for (uintptr_t actor : actorPtrs) {
            if (IsReadablePointer(actor) && seenActors.insert(actor).second) {
                actors.push_back(actor);
            }
        }
    }
    lastActorCount_ = static_cast<int>(actors.size());

    auto readTransformArray = [&](uintptr_t component,
        RemoteArray& outArray,
        Transformd& outFootAnchor,
        uintptr_t& outOffset) -> bool {
        static constexpr std::array<TransformArrayOffsetCandidate, 2> candidates{ {
            { Offsets::USkeletalMeshComponent_CachedComponentSpaceTransforms_Dumper, "Dumper CachedComponentSpaceTransforms" },
            { Offsets::USkeletalMeshComponent_CachedComponentSpaceTransforms_LiveFallback, "Live body transform fallback" },
        } };

        for (const TransformArrayOffsetCandidate& candidate : candidates) {
            RemoteArray transformArray{};
            if (!readArray(component + candidate.Offset, transformArray) ||
                !IsSaneArray(transformArray, 4096)) {
                continue;
            }

            if (onlyPlayerBodyMesh && transformArray.Count != bodyTransformCount) {
                continue;
            }

            Transformd footAnchorTransform{};
            if (!ReadMemory(transformArray.Data, &footAnchorTransform, sizeof(footAnchorTransform)) ||
                !IsSaneTransform(footAnchorTransform)) {
                continue;
            }

            outArray = transformArray;
            outFootAnchor = footAnchorTransform;
            outOffset = candidate.Offset;
            return true;
        }

        return false;
    };

    auto collectHighlightComponents = [&](uintptr_t actor, uintptr_t bodyComponent, std::vector<uintptr_t>& outComponents) {
        auto addHighlightComponent = [&](uintptr_t component) {
            if (!IsReadablePointer(component) ||
                std::find(outComponents.begin(), outComponents.end(), component) != outComponents.end()) {
                return;
            }

            uint8_t renderFlags = 0;
            if (!ReadMemory(component + Offsets::UPrimitiveComponent_RenderFlags,
                &renderFlags, sizeof(renderFlags))) {
                return;
            }

            outComponents.push_back(component);
        };

        addHighlightComponent(bodyComponent);
        static constexpr std::array<uintptr_t, 6> extraHighlightComponentOffsets{ {
            Offsets::BP_Main_HandBone,
            Offsets::BP_Main_LeftItemPositon3,
            Offsets::BP_Main_LeftItemPositon2,
            Offsets::BP_Main_LeftItemPositon1,
            Offsets::BP_Main_RightItemPositon,
            Offsets::BP_Main_FirstPersonMesh,
        } };
        for (uintptr_t componentOffset : extraHighlightComponentOffsets) {
            uintptr_t component = 0;
            if (readPtr(actor + componentOffset, component)) {
                addHighlightComponent(component);
            }
        }
    };

    std::vector<HighlightTarget> highlightTargets;
    highlightTargets.reserve(64);
    std::vector<PlayerSnapshot> snapshots;
    snapshots.reserve(64);
    std::unordered_set<uintptr_t> drawnComponents;
    for (uintptr_t actor : actors) {
        uintptr_t bodyComponent = 0;
        if (!readPtr(actor + Offsets::AActor_PlayerBodyMesh, bodyComponent) ||
            !drawnComponents.insert(bodyComponent).second) {
            continue;
        }
        ++lastComponentCandidates_;

        Transformd componentToWorld{};
        if (!ReadMemory(bodyComponent + Offsets::USceneComponent_ComponentToWorld, &componentToWorld, sizeof(componentToWorld)) ||
            !IsSaneTransform(componentToWorld)) {
            continue;
        }

        const Vec3d cameraToComponent = componentToWorld.Translation - camera.Location;
        const double componentDistance = std::sqrt(Dot(cameraToComponent, cameraToComponent));
        if (componentDistance < static_cast<double>(minCameraDistance)) {
            continue;
        }

        RemoteArray transformArray{};
        Transformd footAnchorTransform{};
        uintptr_t transformArrayOffset = 0;
        if (!readTransformArray(bodyComponent, transformArray, footAnchorTransform, transformArrayOffset)) {
            continue;
        }
        lastTransformArrayOffset_ = transformArrayOffset;

        std::vector<uintptr_t> highlightComponents;
        collectHighlightComponents(actor, bodyComponent, highlightComponents);
        if (!highlightComponents.empty()) {
            HighlightTarget target{};
            target.Actor = actor;
            target.Components = highlightComponents;
            highlightTargets.push_back(std::move(target));
        }

        const Vec3d footWorld = TransformPosition(componentToWorld, footAnchorTransform.Translation);
        const Vec3d headWorld{ footWorld.X, footWorld.Y, footWorld.Z + static_cast<double>(boxWorldHeight) };

        ImVec2 footScreen{};
        ImVec2 headScreen{};
        if (!WorldToScreen(camera, footWorld, viewportSize, footScreen) ||
            !WorldToScreen(camera, headWorld, viewportSize, headScreen)) {
            continue;
        }

        if (!IsUsableScreenPoint(footScreen, viewportSize, 0.20f) &&
            !IsUsableScreenPoint(headScreen, viewportSize, 0.20f)) {
            continue;
        }

        const float topY = (std::min)(headScreen.y, footScreen.y);
        const float bottomY = (std::max)(headScreen.y, footScreen.y);
        const float boxHeight = bottomY - topY;
        if (boxHeight < 8.0f) {
            continue;
        }

        const float centerX = (headScreen.x + footScreen.x) * 0.5f;
        const float boxWidth = (std::max)(4.0f, boxHeight * boxWidthRatio);
        const float halfWidth = boxWidth * 0.5f;
        const ImVec2 fallbackTopLeft(centerX - halfWidth, topY);
        const ImVec2 fallbackBottomRight(centerX + halfWidth, bottomY);

        PlayerSnapshot snapshot{};
        snapshot.Actor = actor;
        snapshot.BodyComponent = bodyComponent;
        snapshot.IsLocal = actor == lastLocalPawn_;
        snapshot.TransformArray = transformArray;
        snapshot.ComponentToWorld = componentToWorld;
        snapshot.FootWorld = footWorld;
        snapshot.HeadWorld = headWorld;
        snapshot.FootScreen = footScreen;
        snapshot.HeadScreen = headScreen;
        snapshot.TopLeft = fallbackTopLeft;
        snapshot.BottomRight = fallbackBottomRight;
        snapshot.BoxHeight = boxHeight;
        snapshot.BoxWidth = boxWidth;

        const int boneCount = (std::min)(transformArray.Count, maxBonesPerMesh);
        if (boneCount >= 2) {
            std::vector<Transformd> boneTransforms(static_cast<size_t>(boneCount));
            if (ReadMemory(transformArray.Data, boneTransforms.data(), boneTransforms.size() * sizeof(Transformd))) {
                snapshot.BonePoints.resize(static_cast<size_t>(boneCount));
                const float boneGateX = (std::max)(38.0f, boxWidth * 1.35f);
                const float boneGateY = (std::max)(32.0f, boxHeight * 0.45f);
                for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                    const Transformd& boneTransform = boneTransforms[static_cast<size_t>(boneIndex)];
                    if (!IsSaneTransform(boneTransform)) {
                        continue;
                    }

                    const Vec3d boneWorld = TransformPosition(componentToWorld, boneTransform.Translation);
                    ImVec2 boneScreen{};
                    if (WorldToScreen(camera, boneWorld, viewportSize, boneScreen) &&
                        IsUsableScreenPoint(boneScreen, viewportSize, 0.12f) &&
                        IsPointInExpandedRect(boneScreen, fallbackTopLeft, fallbackBottomRight, boneGateX, boneGateY)) {
                        BonePoint& point = snapshot.BonePoints[static_cast<size_t>(boneIndex)];
                        point.World = boneWorld;
                        point.Screen = boneScreen;
                        point.Visible = true;
                    }
                }
            }
        }

        if (useBoneBoundsForBoxes && !snapshot.BonePoints.empty()) {
            ImVec2 boneTopLeft{};
            ImVec2 boneBottomRight{};
            int visibleBoneCount = 0;
            if (ComputeBoneScreenBounds(snapshot.BonePoints, boxPaddingPixels, boneTopLeft, boneBottomRight, visibleBoneCount)) {
                const float boneHeight = boneBottomRight.y - boneTopLeft.y;
                const float boneWidthRaw = boneBottomRight.x - boneTopLeft.x;
                if (boneHeight >= 10.0f && boneHeight <= boxHeight * 1.35f && boneWidthRaw <= boxHeight * 0.95f) {
                    const float minBoneWidth = (std::max)(10.0f, boneHeight * 0.26f);
                    const float boneCenterX = (boneTopLeft.x + boneBottomRight.x) * 0.5f;
                    const float finalBoneWidth = (std::max)(boneWidthRaw, minBoneWidth);
                    snapshot.TopLeft = ImVec2(boneCenterX - finalBoneWidth * 0.5f, boneTopLeft.y);
                    snapshot.BottomRight = ImVec2(boneCenterX + finalBoneWidth * 0.5f, boneBottomRight.y);
                    snapshot.BoxHeight = snapshot.BottomRight.y - snapshot.TopLeft.y;
                    snapshot.BoxWidth = snapshot.BottomRight.x - snapshot.TopLeft.x;
                    snapshot.HeadScreen = ImVec2(boneCenterX, snapshot.TopLeft.y);
                }
            }
        }

        snapshot.HighlightComponents = highlightComponents;

        auto role = roleByActor.find(actor);
        if (role != roleByActor.end() && role->second != PlayerRole::Unknown) {
            snapshot.Role = role->second;
        }
        else {
            bool isHunter = false;
            if (readBool(actor + Offsets::BP_Character_IsHunter, isHunter)) {
                snapshot.Role = isHunter ? PlayerRole::Hunter : PlayerRole::Survivor;
            }
        }

        auto playerState = playerStateByActor.find(actor);
        if (playerState != playerStateByActor.end()) {
            snapshot.PlayerState = playerState->second;
        }

        readBool(actor + Offsets::BP_Character_IsPaintViewLock, snapshot.IsPaintViewLock);
        readBool(actor + Offsets::BP_Character_IsPaintMode, snapshot.IsPaintMode);
        readBool(actor + Offsets::BP_Character_IsBrushing, snapshot.IsBrushing);
        readBool(actor + Offsets::BP_Character_BodyVisibility, snapshot.BodyVisibility);
        readBool(actor + Offsets::BP_Character_HideBlock, snapshot.HideBlock);
        ReadMemory(actor + Offsets::BP_Character_CurrentPaintColor,
            &snapshot.CurrentPaintColor, sizeof(snapshot.CurrentPaintColor));
        uintptr_t currentEmote = 0;
        readRawPtr(actor + Offsets::BP_Character_CurrentPlayEmote, currentEmote);
        snapshot.HasEmote = IsReadablePointer(currentEmote);

        if (snapshot.Role != PlayerRole::Unknown) {
            ++lastRoleCount_;
        }
        snapshots.push_back(snapshot);
    }

    lastTargetCount_ = static_cast<int>(highlightTargets.size());

    std::unordered_set<uintptr_t> keepHighlightedComponents;
    std::unordered_set<uintptr_t> keepPaintActors;
    if (enablePlayerHighlight) {
        for (const HighlightTarget& target : highlightTargets) {
            if (ApplyPaintHighlightColor(target.Actor, playerHighlightRgb)) {
                keepPaintActors.insert(target.Actor);
                ++lastPaintHighlightCount_;
            }

            for (uintptr_t component : target.Components) {
                if (ApplyCustomDepthHighlight(component, PlayerHighlightStencilValue)) {
                    keepHighlightedComponents.insert(component);
                    ++lastHighlightedCount_;
                }
            }
        }
    }
    RestoreCustomDepthHighlightsExcept(keepHighlightedComponents);
    RestorePaintHighlightColorsExcept(keepPaintActors);

    auto sampleScreenColor = [&](const PlayerSnapshot& snapshot, LinearColor& outColor) -> bool {
        HDC screenDc = GetDC(nullptr);
        if (!screenDc) {
            return false;
        }

        const float cx = (snapshot.TopLeft.x + snapshot.BottomRight.x) * 0.5f;
        const float cy = (snapshot.TopLeft.y + snapshot.BottomRight.y) * 0.5f;
        const float left = snapshot.TopLeft.x - 14.0f;
        const float right = snapshot.BottomRight.x + 14.0f;
        const float top = snapshot.TopLeft.y - 16.0f;
        const float bottom = snapshot.BottomRight.y + 16.0f;
        const std::array<ImVec2, 10> samplePoints{ {
            { left, top }, { cx, top }, { right, top },
            { left, cy }, { right, cy },
            { left, bottom }, { cx, bottom }, { right, bottom },
            { snapshot.TopLeft.x - 28.0f, snapshot.FootScreen.y },
            { snapshot.BottomRight.x + 28.0f, snapshot.FootScreen.y },
        } };

        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        int count = 0;
        for (const ImVec2& point : samplePoints) {
            const int sx = static_cast<int>(screenOrigin.x + point.x);
            const int sy = static_cast<int>(screenOrigin.y + point.y);
            const COLORREF pixel = GetPixel(screenDc, sx, sy);
            if (pixel == CLR_INVALID) {
                continue;
            }

            r += static_cast<double>(GetRValue(pixel)) / 255.0;
            g += static_cast<double>(GetGValue(pixel)) / 255.0;
            b += static_cast<double>(GetBValue(pixel)) / 255.0;
            ++count;
        }
        ReleaseDC(nullptr, screenDc);

        if (count == 0) {
            return false;
        }

        outColor.R = static_cast<float>(r / static_cast<double>(count));
        outColor.G = static_cast<float>(g / static_cast<double>(count));
        outColor.B = static_cast<float>(b / static_cast<double>(count));
        outColor.A = 1.0f;
        return true;
    };

    if (autoCamoRequested_) {
        autoCamoRequested_ = false;
        const PlayerSnapshot* localSnapshot = nullptr;
        for (const PlayerSnapshot& snapshot : snapshots) {
            if (snapshot.IsLocal) {
                localSnapshot = &snapshot;
                break;
            }
        }

        if (!enableMemoryWrites) {
            lastAutoCamoApplied_ = 0;
            SetStatus("Auto camo needs Enable memory writes");
        }
        else if (!localSnapshot) {
            lastAutoCamoApplied_ = 0;
            SetStatus("Auto camo failed: local pawn snapshot missing");
        }
        else {
            LinearColor sampled{};
            if (sampleScreenColor(*localSnapshot, sampled) &&
                WriteMemory(localSnapshot->Actor + Offsets::BP_Character_CurrentPaintColor,
                    &sampled, sizeof(sampled))) {
                lastAutoCamoApplied_ = 1;
                lastAutoCamoR_ = sampled.R;
                lastAutoCamoG_ = sampled.G;
                lastAutoCamoB_ = sampled.B;
                SetStatus("Auto camo wrote CurrentPaintColor RGB %.2f %.2f %.2f",
                    sampled.R, sampled.G, sampled.B);
            }
            else {
                lastAutoCamoApplied_ = 0;
                ++lastWriteFailCount_;
                SetStatus("Auto camo failed: sample/write error");
            }
        }
    }

    auto roleColor = [this](PlayerRole role, int alpha = 245) -> ImU32 {
        return RoleColor(role, survivorHighlightRgb, hunterHighlightRgb, alpha);
    };

    const ImU32 dotColor = IM_COL32(255, 235, 120, 240);
    const ImU32 indexColor = IM_COL32(255, 220, 90, 245);

    const PlayerSnapshot* localSnapshot = nullptr;
    for (const PlayerSnapshot& snapshot : snapshots) {
        if (snapshot.IsLocal) {
            localSnapshot = &snapshot;
            break;
        }
    }

    PlayerRole localRole = PlayerRole::Unknown;
    if (localSnapshot) {
        localRole = localSnapshot->Role;
    }
    else if (IsReadablePointer(lastLocalPawn_)) {
        auto localRoleIt = roleByActor.find(lastLocalPawn_);
        if (localRoleIt != roleByActor.end() && localRoleIt->second != PlayerRole::Unknown) {
            localRole = localRoleIt->second;
        }
        else {
            bool localIsHunter = false;
            if (readBool(lastLocalPawn_ + Offsets::BP_Character_IsHunter, localIsHunter)) {
                localRole = localIsHunter ? PlayerRole::Hunter : PlayerRole::Survivor;
            }
        }
    }

    if (enableSeekerWarning &&
        localSnapshot &&
        localSnapshot->Role == PlayerRole::Survivor) {
        const double threshold = std::cos(static_cast<double>(seekerWarningAngleDegrees) * Pi / 180.0);
        double nearestDanger = seekerWarningMaxDistance;
        for (const PlayerSnapshot& hunter : snapshots) {
            if (hunter.Role != PlayerRole::Hunter || hunter.Actor == localSnapshot->Actor) {
                continue;
            }

            const Vec3d toLocal = localSnapshot->FootWorld - hunter.FootWorld;
            const double distance = Length(toLocal);
            if (distance > static_cast<double>(seekerWarningMaxDistance)) {
                continue;
            }

            const Vec3d hunterForward = Normalize(RotateVector(hunter.ComponentToWorld.Rotation, { 1.0, 0.0, 0.0 }));
            const double viewDot = Dot(hunterForward, Normalize(toLocal));
            if (viewDot >= threshold) {
                ++lastWarningCount_;
                nearestDanger = (std::min)(nearestDanger, distance);
                if (IsUsableScreenPoint(hunter.HeadScreen, viewportSize, 0.10f)) {
                    drawList->AddLine(hunter.HeadScreen, localSnapshot->HeadScreen,
                        roleColor(PlayerRole::Hunter, 190), 2.0f);
                }
            }
        }

        if (lastWarningCount_ > 0) {
            const char* warningText = "SEEKER VIEW WARNING";
            const ImVec2 textSize = ImGui::CalcTextSize(warningText);
            const ImVec2 textPos((viewportSize.x - textSize.x) * 0.5f, 80.0f);
            drawList->AddRectFilled(ImVec2(textPos.x - 12.0f, textPos.y - 8.0f),
                ImVec2(textPos.x + textSize.x + 12.0f, textPos.y + textSize.y + 8.0f),
                IM_COL32(120, 0, 0, 180), 4.0f);
            drawList->AddText(textPos, IM_COL32(255, 70, 70, 255), warningText);

            char distanceText[64]{};
            std::snprintf(distanceText, sizeof(distanceText), "nearest %.0f", nearestDanger);
            drawList->AddText(ImVec2(textPos.x, textPos.y + textSize.y + 8.0f),
                IM_COL32(255, 180, 180, 240), distanceText);
        }
    }

    if (drawCrosshairEnemyLines && localRole == PlayerRole::Hunter) {
        const ImVec2 screenCenter(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
        const float centerDeadzone = (std::max)(18.0f, (std::min)(36.0f, viewportSize.y * 0.025f));
        const ImU32 lineColor = RoleColor(PlayerRole::Survivor, survivorHighlightRgb, hunterHighlightRgb, 215);
        for (const PlayerSnapshot& survivor : snapshots) {
            if (survivor.Actor == lastLocalPawn_ || survivor.Role != PlayerRole::Survivor) {
                continue;
            }

            const ImVec2 targetCenter(
                (survivor.TopLeft.x + survivor.BottomRight.x) * 0.5f,
                (survivor.TopLeft.y + survivor.BottomRight.y) * 0.5f);
            if (Distance(screenCenter, targetCenter) <= centerDeadzone ||
                IsPointInExpandedRect(screenCenter, survivor.TopLeft, survivor.BottomRight, 6.0f, 6.0f)) {
                continue;
            }

            drawList->AddLine(screenCenter, targetCenter, IM_COL32(0, 0, 0, 190), lineThickness + 1.25f);
            drawList->AddLine(screenCenter, targetCenter, lineColor, lineThickness);
            ++lastCrosshairLineCount_;
        }
    }

    for (const PlayerSnapshot& snapshot : snapshots) {
        if (skipLocalPawn && snapshot.IsLocal) {
            continue;
        }

        const ImU32 snapshotRoleColor = roleColor(snapshot.Role);

        if (drawBoxes) {
            if (drawCornerBoxes) {
                DrawCornerBox(drawList, snapshot.TopLeft, snapshot.BottomRight, snapshotRoleColor, lineThickness);
            }
            else {
                drawList->AddRect(ImVec2(snapshot.TopLeft.x - 1.0f, snapshot.TopLeft.y - 1.0f),
                    ImVec2(snapshot.BottomRight.x + 1.0f, snapshot.BottomRight.y + 1.0f),
                    IM_COL32(0, 0, 0, 180), 0.0f, 0, lineThickness);
                drawList->AddRect(snapshot.TopLeft, snapshot.BottomRight, snapshotRoleColor, 0.0f, 0, lineThickness);
            }
            ++lastBoxCount_;
        }

        if (drawFootDot) {
            drawList->AddCircleFilled(snapshot.FootScreen, dotRadius, dotColor);
        }

        float textY = snapshot.TopLeft.y - 34.0f;
        if (drawRoleEsp) {
            const char* roleText = RoleText(snapshot.Role);
            drawList->AddText(ImVec2(snapshot.TopLeft.x + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 220), roleText);
            drawList->AddText(ImVec2(snapshot.TopLeft.x, textY), snapshotRoleColor, roleText);
            textY += 14.0f;
        }

        if (drawStateEsp) {
            char stateText[160]{};
            std::snprintf(stateText, sizeof(stateText), "%s%s%s%s%s RGB %.2f %.2f %.2f",
                snapshot.IsPaintMode ? "PAINT " : "",
                snapshot.IsPaintViewLock ? "LOCK " : "",
                snapshot.IsBrushing ? "BRUSH " : "",
                snapshot.HasEmote ? "EMOTE " : "",
                (!snapshot.BodyVisibility || snapshot.HideBlock) ? "HIDDEN " : "",
                snapshot.CurrentPaintColor.R,
                snapshot.CurrentPaintColor.G,
                snapshot.CurrentPaintColor.B);
            drawList->AddText(ImVec2(snapshot.TopLeft.x + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 220), stateText);
            drawList->AddText(ImVec2(snapshot.TopLeft.x, textY), IM_COL32(210, 240, 255, 240), stateText);
            drawList->AddRectFilled(ImVec2(snapshot.BottomRight.x + 4.0f, snapshot.TopLeft.y),
                ImVec2(snapshot.BottomRight.x + 16.0f, snapshot.TopLeft.y + 12.0f),
                IM_COL32(
                    static_cast<int>((std::max)(0.0f, (std::min)(1.0f, snapshot.CurrentPaintColor.R)) * 255.0f),
                    static_cast<int>((std::max)(0.0f, (std::min)(1.0f, snapshot.CurrentPaintColor.G)) * 255.0f),
                    static_cast<int>((std::max)(0.0f, (std::min)(1.0f, snapshot.CurrentPaintColor.B)) * 255.0f),
                    230));
            ++lastStateCount_;
        }

        if (drawBoneIndices && snapshot.BonePoints.size() >= 2) {
            const int boneCount = (std::min)(static_cast<int>(snapshot.BonePoints.size()), maxBonesPerMesh);
            const float boxPadX = (std::max)(14.0f, snapshot.BoxWidth * 0.35f);
            const float boxPadY = (std::max)(16.0f, snapshot.BoxHeight * 0.22f);

            for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                const BonePoint& point = snapshot.BonePoints[static_cast<size_t>(boneIndex)];
                if (!point.Visible ||
                    !IsPointInExpandedRect(point.Screen, snapshot.TopLeft, snapshot.BottomRight, boxPadX, boxPadY)) {
                    continue;
                }

                char label[12]{};
                std::snprintf(label, sizeof(label), "%d", boneIndex);
                const ImVec2 shadow(point.Screen.x + 3.0f, point.Screen.y + 3.0f);
                const ImVec2 textPos(point.Screen.x + 2.0f, point.Screen.y + 2.0f);
                drawList->AddText(shadow, IM_COL32(0, 0, 0, 210), label);
                drawList->AddText(textPos, indexColor, label);
                ++lastIndexCount_;
            }
        }
    }

    SetStatus("Frame OK. actors %d, targets %d, roles %d, states %d, boxes %d, lines %d, stencil %d, paint %d, warnings %d, FOV %.1f",
        lastActorCount_, lastTargetCount_, lastRoleCount_, lastStateCount_, lastBoxCount_,
        lastCrosshairLineCount_, lastHighlightedCount_, lastPaintHighlightCount_, lastWarningCount_, camera.FOV);
}
