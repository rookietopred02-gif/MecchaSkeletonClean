#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>

#include "imgui.h"

class MecchaReader {
public:
    bool enableEsp = true;
    bool autoAttach = true;
    bool skipLocalPawn = false;
    bool drawFootDot = true;
    bool drawBoxes = true;
    bool onlyPlayerBodyMesh = true;

    int maxActorsPerFrame = 8192;
    int bodyTransformCount = 28;
    float lineThickness = 1.6f;
    float dotRadius = 2.0f;
    float minCameraDistance = 0.0f;
    float boxWorldHeight = 165.0f;
    float boxWidthRatio = 0.42f;

    MecchaReader();
    ~MecchaReader();

    bool Attach();
    void Detach();
    void DrawControls();
    void DrawEsp(ImDrawList* drawList, ImVec2 viewportSize);

    bool IsAttached() const { return process_ != nullptr; }
    DWORD TargetPid() const { return pid_; }
    const char* Status() const { return status_; }

private:
    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    uintptr_t moduleBase_ = 0;
    DWORD moduleSize_ = 0;

    uintptr_t lastWorld_ = 0;
    uintptr_t lastPlayerController_ = 0;
    uintptr_t lastLocalPawn_ = 0;
    int lastActorCount_ = 0;
    int lastComponentCandidates_ = 0;
    int lastBoxCount_ = 0;
    int lastTargetCount_ = 0;
    char status_[256] = "Not attached";

    bool TryAttachProcess(const wchar_t* processName);
    bool ReadMemory(uintptr_t address, void* out, size_t size) const;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const;
    void SetStatus(const char* fmt, ...);
};
