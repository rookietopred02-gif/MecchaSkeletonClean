#include "MecchaReader.h"
#include "EspTypes.hpp"
#include "Offsets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_set>
#include <vector>

// =============================================================================
//  Read + render pipeline (DrawEsp) and its local render helpers. Walks the
//  UE5 world -> levels -> actors, builds a per-player snapshot (bones, box,
//  health, freeze, role), projects it with WorldToScreen, and draws the ESP.
//  Also dispatches the per-frame write features (movement/noclip/aimbot/kill-
//  all) via the MecchaReader helpers in MecchaReader_Hacks.cpp.
// =============================================================================

namespace {
// Roles in the current LINK (co-op) build: King is the one readable role-ish
// signal (GameState.KingCharacter); everyone else is a generic player. The old
// asymmetric Survivor/Hunter roles no longer exist in this build.
enum class PlayerRole {
    Unknown,
    King
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
    PlayerRole Role = PlayerRole::Unknown;
    bool IsLocal = false;
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
    double Health = 0.0;
    double MaxHealth = 0.0;
    bool IsFrozen = false;
    std::vector<BonePoint> BonePoints;
};

struct TransformArrayOffsetCandidate {
    uintptr_t Offset = 0;
    const char* Name = nullptr;
};

float Distance(const ImVec2& lhs, const ImVec2& rhs)
{
    const float dx = lhs.x - rhs.x;
    const float dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

const char* RoleText(PlayerRole role)
{
    return role == PlayerRole::King ? "KING" : "PLAYER";
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

ImU32 RoleColor(PlayerRole role, const int kingRgb[3], int alpha = 245)
{
    if (role == PlayerRole::King) {
        return MakeRgbColor(kingRgb, alpha);
    }
    return IM_COL32(160, 220, 255, ClampColorChannel(alpha));
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

void MecchaReader::DrawEsp(ImDrawList* drawList, ImVec2 viewportSize, ImVec2 screenOrigin)
{
    // Per-frame autosave: runs every frame (Main.cpp calls DrawEsp unconditionally),
    // so config persists even when the menu/DrawControls is hidden. Internally
    // gated on IsAttached() + the 1.5s throttle, so this is cheap.
    CheckAndAutoSave();

    lastActorCount_ = 0;
    lastComponentCandidates_ = 0;
    lastBoxCount_ = 0;
    lastTargetCount_ = 0;
    lastCrosshairLineCount_ = 0;
    lastIndexCount_ = 0;
    lastRoleCount_ = 0;
    lastStateCount_ = 0;
    lastHighlightedCount_ = 0;
    lastWarningCount_ = 0;
    lastWriteFailCount_ = 0;
    lastTransformArrayOffset_ = 0;
    lastPlayerCount_ = 0;
    lastKingActor_ = 0;
    localPawnValid_ = false;

    if ((GetAsyncKeyState(VK_F6) & 1) != 0) {
        enableFreecam = !enableFreecam;
        if (!enableFreecam) {
            RestoreFreecamCamera();
        }
    }
    if ((GetAsyncKeyState(VK_F7) & 1) != 0) {
        autoCamoRequested_ = true;
    }
    if ((GetAsyncKeyState(VK_F8) & 1) != 0) {
        killAllRequested_ = true;
    }

    if (!enableEsp) {
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

    auto readArray = [this](uintptr_t address, RemoteArray& out) -> bool {
        out = {};
        return ReadMemory(address, &out, sizeof(out));
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
    localPawnValid_ = IsReadablePointer(lastLocalPawn_);

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

    // ---- Freecam (camera write) ----
    if (!enableFreecam || !enableMemoryWrites) {
        if (freecamActive_) {
            RestoreFreecamCamera();
        }
    } else {
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
        } else {
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

    // ---- King detection (only role-ish signal in the LINK build) ----
    uintptr_t gameState = 0;
    if (readPtr(world + Offsets::UWorld_GameState, gameState)) {
        readPtr(gameState + Offsets::GameState_KingCharacter, lastKingActor_);
    }

    // ---- Enumerate levels -> actors ----
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

        // Role: only "King" is derivable in the LINK build; everyone else is a generic player.
        if (lastKingActor_ != 0 && actor == lastKingActor_) {
            snapshot.Role = PlayerRole::King;
            ++lastRoleCount_;
        } else {
            snapshot.Role = PlayerRole::Unknown;
        }

        // Read-based extras (Health bar, frozen-teammate ESP). All read-only.
        ReadMemory(actor + Offsets::BP_Main_Health, &snapshot.Health, sizeof(snapshot.Health));
        ReadMemory(actor + Offsets::BP_Main_MaxHealthValue, &snapshot.MaxHealth, sizeof(snapshot.MaxHealth));
        if (!std::isfinite(snapshot.MaxHealth) || snapshot.MaxHealth <= 0.0 || snapshot.MaxHealth > 100000.0) {
            snapshot.MaxHealth = 0.0;
        }
        if (!std::isfinite(snapshot.Health) || snapshot.Health < 0.0 || snapshot.Health > 100000.0) {
            snapshot.Health = 0.0;
        }
        {
            uint8_t frozen = 0;
            if (ReadMemory(actor + Offsets::BP_LINK_IsFreeze, &frozen, sizeof(frozen))) {
                snapshot.IsFrozen = frozen != 0;
            }
        }

        snapshots.push_back(std::move(snapshot));
    }

    lastPlayerCount_ = static_cast<int>(snapshots.size());
    lastTargetCount_ = static_cast<int>(highlightTargets.size());

    // ---- Kill All (write Health=0 to every other player character) ----
    if (killAllRequested_) {
        killAllRequested_ = false;
        if (!enableMemoryWrites) {
            SetStatus("Kill all blocked: enable memory writes first");
        } else if (snapshots.empty()) {
            SetStatus("Kill all skipped: no player characters in view");
        } else {
            std::vector<uintptr_t> killTargets;
            killTargets.reserve(snapshots.size());
            for (const PlayerSnapshot& snapshot : snapshots) {
                if (!snapshot.IsLocal) {
                    killTargets.push_back(snapshot.Actor);
                }
            }
            KillAllPlayersExternal(killTargets, lastLocalPawn_);
        }
    }

    // ---- Auto-camo (sample screen color) ----
    // The old paint-write target (CurrentPaintColor) is gone in the LINK build,
    // so we only SAMPLE and report the colour now; no memory is written.
    if (autoCamoRequested_) {
        autoCamoRequested_ = false;
        const PlayerSnapshot* localSnapshot = nullptr;
        for (const PlayerSnapshot& snapshot : snapshots) {
            if (snapshot.IsLocal) {
                localSnapshot = &snapshot;
                break;
            }
        }

        if (!localSnapshot) {
            lastAutoCamoApplied_ = 0;
            SetStatus("Auto camo: local pawn snapshot missing");
        } else {
            HDC screenDc = GetDC(nullptr);
            int count = 0;
            double r = 0.0, g = 0.0, b = 0.0;
            if (screenDc) {
                const float cx = (localSnapshot->TopLeft.x + localSnapshot->BottomRight.x) * 0.5f;
                const float cy = (localSnapshot->TopLeft.y + localSnapshot->BottomRight.y) * 0.5f;
                const std::array<ImVec2, 5> samplePoints{ {
                    { cx, localSnapshot->TopLeft.y }, { cx, cy }, { cx, localSnapshot->BottomRight.y },
                    { localSnapshot->TopLeft.x, cy }, { localSnapshot->BottomRight.x, cy },
                } };
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
            }
            if (count > 0) {
                lastAutoCamoApplied_ = 1;
                lastAutoCamoR_ = static_cast<float>(r / count);
                lastAutoCamoG_ = static_cast<float>(g / count);
                lastAutoCamoB_ = static_cast<float>(b / count);
                SetStatus("Auto camo sampled RGB %.2f %.2f %.2f (write disabled: paint offset obsolete)",
                    lastAutoCamoR_, lastAutoCamoG_, lastAutoCamoB_);
            } else {
                lastAutoCamoApplied_ = 0;
                SetStatus("Auto camo: screen sample failed");
            }
        }
    }

    auto roleColor = [this](PlayerRole role, int alpha = 245) -> ImU32 {
        return RoleColor(role, kingHighlightRgb, alpha);
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

    // ---- Seeker / watcher warning: warn when another player faces the local pawn ----
    if (enableSeekerWarning && localSnapshot) {
        const double threshold = std::cos(static_cast<double>(seekerWarningAngleDegrees) * Pi / 180.0);
        double nearestDanger = seekerWarningMaxDistance;
        for (const PlayerSnapshot& other : snapshots) {
            if (other.Actor == localSnapshot->Actor) {
                continue;
            }

            const Vec3d toLocal = localSnapshot->FootWorld - other.FootWorld;
            const double distance = Length(toLocal);
            if (distance > static_cast<double>(seekerWarningMaxDistance)) {
                continue;
            }

            const Vec3d watcherForward = Normalize(RotateVector(other.ComponentToWorld.Rotation, { 1.0, 0.0, 0.0 }));
            const double viewDot = Dot(watcherForward, Normalize(toLocal));
            if (viewDot >= threshold) {
                ++lastWarningCount_;
                nearestDanger = (std::min)(nearestDanger, distance);
                if (IsUsableScreenPoint(other.HeadScreen, viewportSize, 0.10f)) {
                    drawList->AddLine(other.HeadScreen, localSnapshot->HeadScreen,
                        IM_COL32(255, 80, 80, 190), 2.0f);
                }
            }
        }

        if (lastWarningCount_ > 0) {
            const char* warningText = "PLAYER LOOKING AT YOU";
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

    // ---- Crosshair lines to every other player ----
    if (drawCrosshairEnemyLines) {
        const ImVec2 screenCenter(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
        const float centerDeadzone = (std::max)(18.0f, (std::min)(36.0f, viewportSize.y * 0.025f));
        const ImU32 lineColor = IM_COL32(120, 220, 255, 215);
        for (const PlayerSnapshot& other : snapshots) {
            if (other.Actor == lastLocalPawn_) {
                continue;
            }

            const ImVec2 targetCenter(
                (other.TopLeft.x + other.BottomRight.x) * 0.5f,
                (other.TopLeft.y + other.BottomRight.y) * 0.5f);
            if (Distance(screenCenter, targetCenter) <= centerDeadzone ||
                IsPointInExpandedRect(screenCenter, other.TopLeft, other.BottomRight, 6.0f, 6.0f)) {
                continue;
            }

            drawList->AddLine(screenCenter, targetCenter, IM_COL32(0, 0, 0, 190), lineThickness + 1.25f);
            drawList->AddLine(screenCenter, targetCenter, lineColor, lineThickness);
            ++lastCrosshairLineCount_;
        }
    }

    // Time-based pulse for the player highlight (0..1). Two looks, both drawn
    // entirely in our overlay (no game writes / function calls):
    //   - Hit-flash (default): sharp attack + quick decay, repeated at the flash
    //     rate, mimicking a repeatedly-retriggered on-hit white flash.
    //   - Smooth blink: sinusoidal fade in/out.
    const double pulseTime = static_cast<double>(GetTickCount64()) / 1000.0;
    const double pulseSpeed = static_cast<double>((std::max)(0.2f, highlightBlinkSpeed));
    float highlightPulse;
    if (playerHighlightHitFlash) {
        const double phase = pulseTime * pulseSpeed;
        const double saw = phase - std::floor(phase);   // 0..1 ramp per flash
        highlightPulse = static_cast<float>(std::pow(1.0 - saw, 2.5)); // bright snap, fast fade
    } else {
        highlightPulse = 0.5f + 0.5f * static_cast<float>(std::sin(pulseTime * pulseSpeed * 2.0 * Pi));
    }

    // ---- Boxes / dots / labels / bone indices ----
    for (const PlayerSnapshot& snapshot : snapshots) {
        if (skipLocalPawn && snapshot.IsLocal) {
            continue;
        }

        const ImU32 snapshotRoleColor = roleColor(snapshot.Role);

        // Player highlight: an overlay-drawn flash that pulses over the target.
        // Uses playerHighlightRgb (default white) so it never reads as the solid
        // black the old custom-depth stencil produced. To make the *body* appear
        // to flash (not just a box), bright glow blobs are stamped over the
        // visible bone points at the flash peak.
        if (enablePlayerHighlight) {
            const int fillAlpha = 24 + static_cast<int>(highlightPulse * 170.0f);
            const int edgeAlpha = 80 + static_cast<int>(highlightPulse * 175.0f);
            drawList->AddRectFilled(snapshot.TopLeft, snapshot.BottomRight, MakeRgbColor(playerHighlightRgb, fillAlpha), 3.0f);
            drawList->AddRect(snapshot.TopLeft, snapshot.BottomRight, MakeRgbColor(playerHighlightRgb, edgeAlpha), 3.0f, 0, lineThickness + 0.6f);

            if (highlightPulse > 0.05f && !snapshot.BonePoints.empty()) {
                const float glowRadius = (std::max)(2.5f, snapshot.BoxWidth * 0.11f);
                const ImU32 glowColor = MakeRgbColor(playerHighlightRgb, static_cast<int>(highlightPulse * 205.0f));
                for (const BonePoint& bonePoint : snapshot.BonePoints) {
                    if (bonePoint.Visible) {
                        drawList->AddCircleFilled(bonePoint.Screen, glowRadius, glowColor);
                    }
                }
            }
            ++lastHighlightedCount_;
        }

        if (drawBoxes) {
            if (drawCornerBoxes) {
                DrawCornerBox(drawList, snapshot.TopLeft, snapshot.BottomRight, snapshotRoleColor, lineThickness);
            } else {
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
            const double distance = Length(snapshot.FootWorld - camera.Location) / 100.0; // cm -> m
            char stateText[96]{};
            std::snprintf(stateText, sizeof(stateText), "%.0fm", distance);
            drawList->AddText(ImVec2(snapshot.TopLeft.x + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 220), stateText);
            drawList->AddText(ImVec2(snapshot.TopLeft.x, textY), IM_COL32(210, 240, 255, 240), stateText);
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

        // Health bar (read-only): Health / MaxHealthValue.
        if (drawHealthBar && snapshot.MaxHealth > 0.0) {
            const float ratio = static_cast<float>(Clamp(snapshot.Health / snapshot.MaxHealth, 0.0, 1.0));
            const ImVec2 barTop(snapshot.TopLeft.x - 9.0f, snapshot.TopLeft.y);
            const ImVec2 barBot(snapshot.TopLeft.x - 6.0f, snapshot.BottomRight.y);
            drawList->AddRectFilled(ImVec2(barTop.x - 1.0f, barTop.y - 1.0f), ImVec2(barBot.x + 1.0f, barBot.y + 1.0f), IM_COL32(0, 0, 0, 200));
            const float fillTop = barBot.y - (barBot.y - barTop.y) * ratio;
            const ImU32 hpColor = IM_COL32(
                static_cast<int>((1.0f - ratio) * 255.0f),
                static_cast<int>(ratio * 230.0f), 60, 235);
            drawList->AddRectFilled(ImVec2(barTop.x, fillTop), barBot, hpColor);
        }

        // Frozen-teammate marker (read-only): LINK IsFreeze — find teammates to rescue.
        if (drawFreezeEsp && snapshot.IsFrozen) {
            const char* frozenText = "FROZEN";
            const ImVec2 pos(snapshot.TopLeft.x, snapshot.BottomRight.y + 2.0f);
            drawList->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 220), frozenText);
            drawList->AddText(pos, IM_COL32(120, 220, 255, 255), frozenText);
        }
    }

    // =========================================================================
    //  Memory-write hacks (movement / noclip / aimbot). Gated by the master
    //  "Enable Memory Writes" switch. Guarded features (penetration / stealth /
    //  no-cooldown) intentionally do nothing here: the LINK build has no field
    //  for them and the old offsets corrupted unrelated memory.
    // =========================================================================
    if (enableMemoryWrites && IsReadablePointer(lastLocalPawn_)) {
        // 1. Speed / Jump / Gravity (incl. noclip zero-g) via the Mover plugin.
        ApplyMovementHacks(lastLocalPawn_);

        // 2. Noclip: disable collision (bit-precise) + drive RootComponent position.
        if (enableNoclip) {
            noclipActive_ = true;
            uint8_t collisionByte = 0;
            if (ReadMemory(lastLocalPawn_ + Offsets::AActor_CollisionFlagsByte, &collisionByte, sizeof(collisionByte))) {
                if (!originalCollisionByteValid_) {
                    originalCollisionByte_ = collisionByte;
                    originalCollisionByteValid_ = true;
                }
                const uint8_t cleared = static_cast<uint8_t>(collisionByte & ~Offsets::AActor_EnableCollisionBitMask);
                if (cleared != collisionByte) {
                    WriteMemory(lastLocalPawn_ + Offsets::AActor_CollisionFlagsByte, &cleared, sizeof(cleared));
                }
            }

            uintptr_t rootComponent = 0;
            if (readPtr(lastLocalPawn_ + Offsets::AActor_RootComponent, rootComponent)) {
                Vec3d worldPos{};
                if (ReadMemory(rootComponent + Offsets::USceneComponent_ComponentToWorld + offsetof(Transformd, Translation),
                        &worldPos, sizeof(worldPos)) && IsFinite(worldPos)) {
                    const ULONGLONG nowTick = GetTickCount64();
                    if (lastNoclipTick_ == 0) {
                        lastNoclipTick_ = nowTick;
                    }
                    double deltaSeconds = Clamp(static_cast<double>(nowTick - lastNoclipTick_) / 1000.0, 0.001, 0.100);
                    lastNoclipTick_ = nowTick;

                    Vec3d movement{};
                    const Vec3d forward = ForwardFromRotation(camera.Rotation);
                    const Vec3d right = RightFromYaw(camera.Rotation.Yaw);
                    if ((GetAsyncKeyState('W') & 0x8000) != 0) movement = movement + forward;
                    if ((GetAsyncKeyState('S') & 0x8000) != 0) movement = movement - forward;
                    if ((GetAsyncKeyState('D') & 0x8000) != 0) movement = movement + right;
                    if ((GetAsyncKeyState('A') & 0x8000) != 0) movement = movement - right;
                    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) movement.Z += 1.0;
                    if ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 || (GetAsyncKeyState('C') & 0x8000) != 0) movement.Z -= 1.0;

                    const double moveLength = Length(movement);
                    if (moveLength > 0.0001) {
                        movement = movement * (1.0 / moveLength);
                        const double speed = static_cast<double>(noclipSpeed) *
                            (((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) ? 2.5 : 1.0);
                        worldPos.X += movement.X * speed * deltaSeconds;
                        worldPos.Y += movement.Y * speed * deltaSeconds;
                        worldPos.Z += movement.Z * speed * deltaSeconds;
                    }

                    // Root component has no parent, so RelativeLocation == world location.
                    WriteMemory(rootComponent + Offsets::USceneComponent_RelativeLocation, &worldPos, sizeof(worldPos));
                    WriteMemory(rootComponent + Offsets::USceneComponent_ComponentToWorld + offsetof(Transformd, Translation),
                        &worldPos, sizeof(worldPos));
                }
            }
        } else if (noclipActive_) {
            if (originalCollisionByteValid_) {
                WriteMemory(lastLocalPawn_ + Offsets::AActor_CollisionFlagsByte, &originalCollisionByte_, sizeof(originalCollisionByte_));
                originalCollisionByteValid_ = false;
            }
            noclipActive_ = false;
            lastNoclipTick_ = 0;
        }

        // 3. Penetration / Stealth / NoCooldown — GUARDED (no valid field in LINK build).
        //    Toggles persist (feature preserved) but we deliberately write nothing.

        // 4. Aimbot: snap AController::ControlRotation toward the nearest player.
        if (enableAimbot) {
            const ImVec2 screenCenter(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
            drawList->AddCircle(screenCenter, aimbotFov * 10.0f, IM_COL32(255, 255, 255, 120), 32, 1.0f);

            double bestDist = (std::numeric_limits<double>::max)();
            Vec3d targetWorld{};
            bool foundTarget = false;
            for (const PlayerSnapshot& snapshot : snapshots) {
                if (snapshot.IsLocal) {
                    continue;
                }

                Vec3d testTargetPos{};
                bool posValid = false;
                if (aimbotTargetType == 0) {
                    testTargetPos = snapshot.HeadWorld;
                    posValid = true;
                } else if (aimbotTargetType == 1) {
                    testTargetPos = snapshot.HeadWorld - Vec3d{ 0.0, 0.0, 30.0 };
                    posValid = true;
                } else if (aimbotTargetType == 2 && !snapshot.BonePoints.empty()) {
                    const int boneIdx = (std::min)(static_cast<int>(snapshot.BonePoints.size()) - 1, aimbotBoneIndex);
                    if (boneIdx >= 0 && snapshot.BonePoints[static_cast<size_t>(boneIdx)].Visible) {
                        testTargetPos = snapshot.BonePoints[static_cast<size_t>(boneIdx)].World;
                        posValid = true;
                    }
                }

                if (posValid) {
                    ImVec2 targetScreen{};
                    if (WorldToScreen(camera, testTargetPos, viewportSize, targetScreen)) {
                        const double screenDist = std::sqrt(
                            (targetScreen.x - screenCenter.x) * (targetScreen.x - screenCenter.x) +
                            (targetScreen.y - screenCenter.y) * (targetScreen.y - screenCenter.y));
                        if (screenDist <= static_cast<double>(aimbotFov * 10.0f) && screenDist < bestDist) {
                            bestDist = screenDist;
                            targetWorld = testTargetPos;
                            foundTarget = true;
                        }
                    }
                }
            }

            if (foundTarget && IsReadablePointer(lastPlayerController_) && (GetAsyncKeyState(aimbotKey) & 0x8000) != 0) {
                const Vec3d dir = targetWorld - camera.Location;
                const double dist = Length(dir);
                if (dist > 1.0) {
                    double targetPitch = std::asin(Clamp(dir.Z / dist, -1.0, 1.0)) * (180.0 / Pi);
                    double targetYaw = std::atan2(dir.Y, dir.X) * (180.0 / Pi);
                    targetPitch = Clamp(targetPitch, -89.0, 89.0);

                    Rotatord currentRot{};
                    if (ReadMemory(lastPlayerController_ + Offsets::AController_ControlRotation, &currentRot, sizeof(currentRot))) {
                        Rotatord nextRot{ targetPitch, targetYaw, 0.0 };
                        if (aimbotSmooth) {
                            double diffYaw = nextRot.Yaw - currentRot.Yaw;
                            while (diffYaw < -180.0) diffYaw += 360.0;
                            while (diffYaw > 180.0) diffYaw -= 360.0;
                            const double diffPitch = nextRot.Pitch - currentRot.Pitch;
                            const double smoothFactor = (std::min)(1.0, static_cast<double>(aimbotSmoothSpeed) * 0.01);
                            nextRot.Yaw = currentRot.Yaw + diffYaw * smoothFactor;
                            nextRot.Pitch = currentRot.Pitch + diffPitch * smoothFactor;
                        }
                        while (nextRot.Yaw < -180.0) nextRot.Yaw += 360.0;
                        while (nextRot.Yaw > 180.0) nextRot.Yaw -= 360.0;
                        nextRot.Pitch = Clamp(nextRot.Pitch, -89.0, 89.0);
                        nextRot.Roll = 0.0;
                        WriteMemory(lastPlayerController_ + Offsets::AController_ControlRotation, &nextRot, sizeof(nextRot));
                    }
                }
            }
        }
    } else if (!enableMemoryWrites) {
        // Master write switch off: make sure nothing stays modified.
        if (speedBackupValid_ || jumpBackupValid_ || gravityBackupValid_ ||
            moverSpeedBackupValid_ || moverJumpBackupValid_ || originalCollisionByteValid_) {
            RestoreMovementHacks(lastLocalPawn_);
        }
    }

    SetStatus("Frame OK. actors %d, players %d, boxes %d, lines %d, bones %d, highlights %d, warnings %d, king %s, kill %d/%d, FOV %.1f",
        lastActorCount_, lastPlayerCount_, lastBoxCount_, lastCrosshairLineCount_, lastIndexCount_,
        lastHighlightedCount_, lastWarningCount_, (lastKingActor_ != 0 ? "yes" : "no"),
        lastKillAllSuccessCount_, lastKillAllTargetCount_, camera.FOV);
}
