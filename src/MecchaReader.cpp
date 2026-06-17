#include "MecchaReader.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <unordered_set>
#include <vector>

namespace {

namespace Offsets {
    constexpr uintptr_t GWorld = 0x9C70620;

    constexpr uintptr_t UWorld_PersistentLevel = 0x30;
    constexpr uintptr_t UWorld_Levels = 0x1C8;
    constexpr uintptr_t UWorld_OwningGameInstance = 0x228;

    constexpr uintptr_t ULevel_Actors = 0xA0;

    constexpr uintptr_t UGameInstance_LocalPlayers = 0x38;
    constexpr uintptr_t UPlayer_PlayerController = 0x30;
    constexpr uintptr_t APlayerController_AcknowledgedPawn = 0x350;
    constexpr uintptr_t APlayerController_PlayerCameraManager = 0x360;
    constexpr uintptr_t APlayerCameraManager_CameraCachePrivate = 0x1530;
    constexpr uintptr_t FCameraCacheEntry_POV = 0x10;

    constexpr uintptr_t AActor_PlayerBodyMesh = 0x418;

    constexpr uintptr_t USceneComponent_ComponentToWorld = 0x1E0;
    constexpr uintptr_t USkeletalMeshComponent_CachedComponentSpaceTransforms = 0x928;
}

constexpr double Pi = 3.14159265358979323846;

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

bool IsUsableScreenPoint(const ImVec2& point, ImVec2 viewportSize, float marginScale)
{
    const float marginX = viewportSize.x * marginScale;
    const float marginY = viewportSize.y * marginScale;
    return point.x >= -marginX && point.x <= viewportSize.x + marginX &&
        point.y >= -marginY && point.y <= viewportSize.y + marginY;
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
    onlyPlayerBodyMesh = true;

    maxActorsPerFrame = 8192;
    bodyTransformCount = 28;
    lineThickness = 1.6f;
    dotRadius = 2.0f;
    minCameraDistance = 0.0f;
    boxWorldHeight = 165.0f;
    boxWidthRatio = 0.42f;
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

    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
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
    lastActorCount_ = 0;
    lastComponentCandidates_ = 0;
    lastBoxCount_ = 0;
    lastTargetCount_ = 0;
}

void MecchaReader::DrawControls()
{
    ImGui::TextUnformatted("MECCHA Box ESP");
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
    ImGui::Checkbox("Draw foot anchor", &drawFootDot);
    ImGui::Checkbox("Only player body mesh", &onlyPlayerBodyMesh);

    ImGui::SliderInt("Max actors/frame", &maxActorsPerFrame, 128, 20000);
    ImGui::SliderInt("Body transform count", &bodyTransformCount, 1, 128);
    ImGui::SliderFloat("Box world height", &boxWorldHeight, 80.0f, 260.0f, "%.0f");
    ImGui::SliderFloat("Box width ratio", &boxWidthRatio, 0.20f, 0.90f, "%.2f");
    ImGui::SliderFloat("Line thickness", &lineThickness, 0.5f, 5.0f, "%.1f");
    ImGui::SliderFloat("Dot radius", &dotRadius, 1.0f, 6.0f, "%.1f");
    ImGui::SliderFloat("Min camera distance", &minCameraDistance, 0.0f, 600.0f, "%.0f");

    ImGui::Separator();
    ImGui::Text("World: 0x%llX", static_cast<unsigned long long>(lastWorld_));
    ImGui::Text("PC: 0x%llX", static_cast<unsigned long long>(lastPlayerController_));
    ImGui::Text("LocalPawn: 0x%llX", static_cast<unsigned long long>(lastLocalPawn_));
    ImGui::Text("Actors: %d | Components: %d", lastActorCount_, lastComponentCandidates_);
    ImGui::Text("Targets: %d | Boxes: %d", lastTargetCount_, lastBoxCount_);
    ImGui::TextWrapped("Status: %s", status_);
}

void MecchaReader::DrawEsp(ImDrawList* drawList, ImVec2 viewportSize)
{
    lastActorCount_ = 0;
    lastComponentCandidates_ = 0;
    lastBoxCount_ = 0;
    lastTargetCount_ = 0;

    if (!enableEsp) {
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

    const ImU32 dotColor = IM_COL32(255, 235, 120, 240);

    std::unordered_set<uintptr_t> drawnComponents;
    auto drawBodyBox = [&](uintptr_t component) {
        if (!IsReadablePointer(component) || !drawnComponents.insert(component).second) {
            return;
        }
        ++lastComponentCandidates_;

        RemoteArray transformArray{};
        if (!readArray(component + Offsets::USkeletalMeshComponent_CachedComponentSpaceTransforms, transformArray) ||
            !IsSaneArray(transformArray, 2048)) {
            return;
        }

        if (onlyPlayerBodyMesh && transformArray.Count != bodyTransformCount) {
            return;
        }

        Transformd componentToWorld{};
        if (!ReadMemory(component + Offsets::USceneComponent_ComponentToWorld, &componentToWorld, sizeof(componentToWorld)) ||
            !IsSaneTransform(componentToWorld)) {
            return;
        }

        const Vec3d cameraToComponent = componentToWorld.Translation - camera.Location;
        const double componentDistance = std::sqrt(Dot(cameraToComponent, cameraToComponent));
        if (componentDistance < static_cast<double>(minCameraDistance)) {
            return;
        }

        Transformd footAnchorTransform{};
        if (!ReadMemory(transformArray.Data, &footAnchorTransform, sizeof(footAnchorTransform)) ||
            !IsSaneTransform(footAnchorTransform)) {
            return;
        }

        const Vec3d footWorld = TransformPosition(componentToWorld, footAnchorTransform.Translation);
        const Vec3d headWorld{ footWorld.X, footWorld.Y, footWorld.Z + static_cast<double>(boxWorldHeight) };

        ImVec2 footScreen{};
        ImVec2 headScreen{};
        if (!WorldToScreen(camera, footWorld, viewportSize, footScreen) ||
            !WorldToScreen(camera, headWorld, viewportSize, headScreen)) {
            return;
        }

        if (!IsUsableScreenPoint(footScreen, viewportSize, 0.20f) &&
            !IsUsableScreenPoint(headScreen, viewportSize, 0.20f)) {
            return;
        }

        float topY = (std::min)(headScreen.y, footScreen.y);
        float bottomY = (std::max)(headScreen.y, footScreen.y);
        float boxHeight = bottomY - topY;
        if (boxHeight < 8.0f) {
            return;
        }

        const float centerX = (headScreen.x + footScreen.x) * 0.5f;
        const float boxWidth = (std::max)(4.0f, boxHeight * boxWidthRatio);
        const float halfWidth = boxWidth * 0.5f;
        ImVec2 topLeft(centerX - halfWidth, topY);
        ImVec2 bottomRight(centerX + halfWidth, bottomY);

        ++lastTargetCount_;

        if (drawBoxes) {
            const ImU32 boxColor = IM_COL32(255, 100, 100, 230);
            drawList->AddRect(ImVec2(topLeft.x - 1.0f, topLeft.y - 1.0f),
                ImVec2(bottomRight.x + 1.0f, bottomRight.y + 1.0f),
                IM_COL32(0, 0, 0, 180), 0.0f, 0, lineThickness);
            drawList->AddRect(topLeft, bottomRight, boxColor, 0.0f, 0, lineThickness);
            ++lastBoxCount_;
        }

        if (drawFootDot) {
            drawList->AddCircleFilled(footScreen, dotRadius, dotColor);
        }
    };

    for (uintptr_t actor : actors) {
        if (skipLocalPawn && actor == lastLocalPawn_) {
            continue;
        }

        uintptr_t bodyComponent = 0;
        if (readPtr(actor + Offsets::AActor_PlayerBodyMesh, bodyComponent)) {
            drawBodyBox(bodyComponent);
        }
    }

    SetStatus("Frame OK. actors %d, targets %d, boxes %d, FOV %.1f",
        lastActorCount_, lastTargetCount_, lastBoxCount_, camera.FOV);
}
