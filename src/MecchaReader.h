#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "imgui.h"

class MecchaReader {
public:
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
    bool onlyPlayerBodyMesh = true;
    bool enableMemoryWrites = true;
    bool enablePlayerHighlight = true;
    bool enableSeekerWarning = true;
    bool enableFreecam = false;

    int maxActorsPerFrame = 8192;
    int bodyTransformCount = 28;
    int maxBonesPerMesh = 128;
    int survivorStencilValue = 1;
    int hunterStencilValue = 2;
    int customDepthWriteMask = 1;
    int playerHighlightRgb[3] = { 255, 0, 255 };
    int survivorHighlightRgb[3] = { 80, 235, 130 };
    int hunterHighlightRgb[3] = { 255, 80, 80 };
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
    struct CustomDepthBackup {
        uint8_t renderFlags = 0;
        uint8_t writeMask = 0;
        int32_t stencilValue = 0;
    };

    struct PaintColorBackup {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        uint8_t bodyShadow = 0;
        uint8_t bodyVisibility = 1;
        uint8_t hideBlock = 0;
    };

    struct RemoteCameraPov {
        double LocationX = 0.0;
        double LocationY = 0.0;
        double LocationZ = 0.0;
        double Pitch = 0.0;
        double Yaw = 0.0;
        double Roll = 0.0;
        float FOV = 90.0f;
    };

    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    uintptr_t moduleBase_ = 0;
    DWORD moduleSize_ = 0;

    uintptr_t lastWorld_ = 0;
    uintptr_t lastPlayerController_ = 0;
    uintptr_t lastLocalPawn_ = 0;
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
    int lastPaintHighlightCount_ = 0;
    int lastWarningCount_ = 0;
    int lastWriteFailCount_ = 0;
    int lastAutoCamoApplied_ = 0;
    float lastAutoCamoR_ = 0.0f;
    float lastAutoCamoG_ = 0.0f;
    float lastAutoCamoB_ = 0.0f;
    std::unordered_map<uintptr_t, CustomDepthBackup> customDepthBackups_;
    std::unordered_map<uintptr_t, PaintColorBackup> paintColorBackups_;
    bool autoCamoRequested_ = false;
    bool freecamActive_ = false;
    bool freecamOriginalValid_ = false;
    bool freecamMouseTracking_ = false;
    uintptr_t freecamCameraManager_ = 0;
    RemoteCameraPov freecamOriginalPov_{};
    RemoteCameraPov freecamCurrentPov_{};
    POINT freecamLastCursor_{};
    ULONGLONG lastFreecamTick_ = 0;
    char status_[256] = "Not attached";

    bool TryAttachProcess(const wchar_t* processName);
    bool ReadMemory(uintptr_t address, void* out, size_t size) const;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;
    bool ApplyCustomDepthHighlight(uintptr_t primitiveComponent, int stencilValue);
    void RestoreCustomDepthHighlights();
    void RestoreCustomDepthHighlightsExcept(const std::unordered_set<uintptr_t>& keep);
    bool ApplyPaintHighlightColor(uintptr_t actor, const int rgb[3]);
    void RestorePaintHighlightColors();
    void RestorePaintHighlightColorsExcept(const std::unordered_set<uintptr_t>& keep);
    void RestoreFreecamCamera();
    void SetStatus(const char* fmt, ...);
};
