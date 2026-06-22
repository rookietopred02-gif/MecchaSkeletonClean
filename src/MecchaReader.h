#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "imgui.h"

// MecchaReader — external read-only ESP overlay + memory-write trainer for the
// Unreal Engine 5.6 game PenguinHotel-Win64-Shipping.exe ("Chameleon", LINK
// mode). This is the MERGED build: it folds every feature from the original
// `src/` line (Kill All) together with the `fork/` line (movement/aimbot/noclip/
// stealth/cooldown/penetration/config) and re-points all offsets at the
// SDK-derived `Offsets.hpp` layer. Nothing was removed.
class MecchaReader {
public:
    // ---- Visuals / ESP ----
    bool enableEsp = true;
    bool autoAttach = true;
    bool skipLocalPawn = false;
    bool drawFootDot = true;
    bool drawBoxes = true;
    bool drawCornerBoxes = true;
    bool useBoneBoundsForBoxes = true;
    bool drawCrosshairEnemyLines = true;
    bool drawBoneIndices = false;
    bool drawRoleEsp = true;
    bool drawStateEsp = true;
    bool drawHealthBar = true;     // read-based: Health/MaxHealthValue
    bool drawFreezeEsp = true;     // read-based: LINK IsFreeze (find frozen teammates)
    bool onlyPlayerBodyMesh = true;

    // ---- Memory-write features ----
    bool enableMemoryWrites = true;     // master switch for ALL memory writes
    bool enablePlayerHighlight = true;  // overlay-drawn blinking highlight (no game writes)
    bool playerHighlightHitFlash = true; // sharp hit-flash pulse vs smooth sine blink
    float highlightBlinkSpeed = 3.0f;   // pulses (flashes) per second
    bool enableSeekerWarning = true;
    bool enableFreecam = false;

    // ---- Fork hacks ----
    bool enableSpeedHack = false;
    float speedHackValue = 800.0f;
    bool enableSuperJump = false;
    float superJumpValue = 1000.0f;
    bool enableGravityScale = false;
    float gravityScaleValue = 0.1f;

    bool enableNoCooldown = false;      // guarded: no scalar field in LINK build (see .cpp)
    bool enablePenetration = false;     // guarded: no field exists in LINK build (see .cpp)
    bool enableStealthMode = false;     // guarded: no plain visibility bool in LINK build (see .cpp)

    bool enableNoclip = false;
    float noclipSpeed = 1000.0f;

    bool enableAimbot = false;
    int aimbotKey = VK_RBUTTON;
    int aimbotTargetType = 0;   // 0 = Stable Head, 1 = Stable Chest, 2 = Bone Index
    int aimbotBoneIndex = 25;
    float aimbotFov = 30.0f;
    bool aimbotSmooth = true;
    float aimbotSmoothSpeed = 15.0f;

    // ---- Tuning ----
    int maxActorsPerFrame = 8192;
    int bodyTransformCount = 28;
    int maxBonesPerMesh = 128;
    int playerHighlightRgb[3] = { 255, 255, 255 };  // hit-flash white by default
    int kingHighlightRgb[3] = { 255, 215, 0 };
    float lineThickness = 1.6f;
    float dotRadius = 2.0f;
    float minCameraDistance = 0.0f;
    float boxWorldHeight = 165.0f;
    float boxWidthRatio = 0.42f;
    float boxPaddingPixels = 5.0f;
    float seekerWarningAngleDegrees = 18.0f;
    float seekerWarningMaxDistance = 6500.0f;
    float freecamSpeed = 700.0f;
    float freecamFastMultiplier = 3.0f;
    float freecamMouseSensitivity = 0.12f;

    MecchaReader();
    ~MecchaReader();

    bool Attach();
    void Detach();
    void DrawControls();
    void DrawEsp(ImDrawList* drawList, ImVec2 viewportSize, ImVec2 screenOrigin = ImVec2(0.0f, 0.0f));

    bool IsAttached() const { return process_ != nullptr; }
    DWORD TargetPid() const { return pid_; }
    const char* Status() const { return status_; }

private:
    struct RemoteCameraPov {
        double LocationX = 0.0;
        double LocationY = 0.0;
        double LocationZ = 0.0;
        double Pitch = 0.0;
        double Yaw = 0.0;
        double Roll = 0.0;
        float FOV = 90.0f;
    };

    struct Vec3dBackup {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    uintptr_t moduleBase_ = 0;
    DWORD moduleSize_ = 0;

    // ---- Per-frame diagnostics ----
    uintptr_t lastWorld_ = 0;
    uintptr_t lastPlayerController_ = 0;
    uintptr_t lastLocalPawn_ = 0;
    uintptr_t lastKingActor_ = 0;
    uintptr_t lastTransformArrayOffset_ = 0;
    int lastActorCount_ = 0;
    int lastComponentCandidates_ = 0;
    int lastBoxCount_ = 0;
    int lastTargetCount_ = 0;
    int lastCrosshairLineCount_ = 0;
    int lastIndexCount_ = 0;
    int lastRoleCount_ = 0;
    int lastStateCount_ = 0;
    int lastHighlightedCount_ = 0;
    int lastWarningCount_ = 0;
    int lastWriteFailCount_ = 0;
    int lastPlayerCount_ = 0;
    int lastKillAllTargetCount_ = 0;
    int lastKillAllSuccessCount_ = 0;
    int lastKillAllFailCount_ = 0;
    int lastAutoCamoApplied_ = 0;
    float lastAutoCamoR_ = 0.0f;
    float lastAutoCamoG_ = 0.0f;
    float lastAutoCamoB_ = 0.0f;

    bool autoCamoRequested_ = false;
    bool killAllRequested_ = false;
    bool localPawnValid_ = false;

    // ---- Freecam ----
    bool freecamActive_ = false;
    bool freecamOriginalValid_ = false;
    bool freecamMouseTracking_ = false;
    uintptr_t freecamCameraManager_ = 0;
    RemoteCameraPov freecamOriginalPov_{};
    RemoteCameraPov freecamCurrentPov_{};
    POINT freecamLastCursor_{};
    ULONGLONG lastFreecamTick_ = 0;

    // ---- Movement-hack backups ----
    bool speedBackupValid_ = false;
    double originalDefaultSpeed_ = 400.0;
    double originalDefaultMaxWalkSpeed_ = 400.0;
    bool moverSpeedBackupValid_ = false;
    float originalMoverMaxSpeed_ = 600.0f;
    bool jumpBackupValid_ = false;
    float originalDefaultJumpSpeed_ = 600.0f;
    bool moverJumpBackupValid_ = false;
    float originalMoverJumpSpeed_ = 600.0f;
    bool gravityBackupValid_ = false;
    Vec3dBackup originalGravity_{};
    uint8_t originalHasGravityOverride_ = 0;

    // ---- Noclip ----
    bool noclipActive_ = false;
    uint8_t originalCollisionByte_ = 0;
    bool originalCollisionByteValid_ = false;
    ULONGLONG lastNoclipTick_ = 0;

    char status_[256] = "Not attached";

    // ---- Config (fork) ----
    std::string lastConfigHash_;
    ULONGLONG lastConfigSaveTick_ = 0;

    bool TryAttachProcess(const wchar_t* processName);
    bool ReadMemory(uintptr_t address, void* out, size_t size) const;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;

    bool KillAllPlayersExternal(const std::vector<uintptr_t>& targetActors, uintptr_t localPawn);
    void RestoreFreecamCamera();

    // Mover helpers (resolve the UCommonLegacyMovementSettings object once).
    uintptr_t ResolveMoverSettings(uintptr_t pawn) const;
    void ApplyMovementHacks(uintptr_t pawn);
    void RestoreMovementHacks(uintptr_t pawn);

    void SaveConfig();
    void LoadConfig();
    void CheckAndAutoSave();
    std::string GetConfigHash() const;

    void SetStatus(const char* fmt, ...);
};
