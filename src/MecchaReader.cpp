#include "MecchaReader.h"
#include "EspTypes.hpp"
#include "Offsets.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

MecchaReader::MecchaReader()
{
    LoadConfig();
    lastConfigHash_ = GetConfigHash();
    lastConfigSaveTick_ = GetTickCount64();
}

MecchaReader::~MecchaReader()
{
    // Final flush on exit: persist any change the debounced autosave (which only
    // runs while attached and at most once / 1.5s) has not written yet. Done
    // before Detach() so member flags still reflect the user's last state.
    if (GetConfigHash() != lastConfigHash_) {
        SaveConfig();
    }
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
        // Do NOT LoadConfig() here. The constructor already loads the config at
        // startup and the in-memory state is the authoritative working copy
        // thereafter; reloading on every (auto-)reattach would clobber any
        // toggle the user changed before the debounced autosave flushed (and
        // would resurrect enableFreecam that Detach() just cleared).
        return true;
    }

    SetStatus("PenguinHotel-Win64-Shipping.exe not found");
    return false;
}

void MecchaReader::Detach()
{
    // Best-effort restore of everything we changed before the handle closes.
    RestoreMovementHacks(lastLocalPawn_);
    RestoreFreecamCamera();

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
    lastKingActor_ = 0;
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
    lastWarningCount_ = 0;
    lastWriteFailCount_ = 0;
    lastPlayerCount_ = 0;
    lastKillAllTargetCount_ = 0;
    lastKillAllSuccessCount_ = 0;
    lastKillAllFailCount_ = 0;
    lastAutoCamoApplied_ = 0;
    lastAutoCamoR_ = 0.0f;
    lastAutoCamoG_ = 0.0f;
    lastAutoCamoB_ = 0.0f;
    // Detaching always clears movement backups so a later re-attach captures the
    // new pawn's originals fresh (RestoreMovementHacks may intentionally keep the
    // Mover flags set when settings could not be resolved).
    speedBackupValid_ = false;
    moverSpeedBackupValid_ = false;
    jumpBackupValid_ = false;
    moverJumpBackupValid_ = false;
    gravityBackupValid_ = false;
    originalCollisionByteValid_ = false;
    autoCamoRequested_ = false;
    killAllRequested_ = false;
    localPawnValid_ = false;
    enableFreecam = false;
    freecamActive_ = false;
    freecamOriginalValid_ = false;
    freecamMouseTracking_ = false;
    freecamCameraManager_ = 0;
    lastFreecamTick_ = 0;
}
