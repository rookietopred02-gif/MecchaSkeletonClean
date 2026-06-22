#include "MecchaReader.h"
#include "EspTypes.hpp"
#include "Offsets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
//  Write-side features: custom-depth highlight (legacy), freecam, the UE5 Mover
//  movement hacks (speed / jump / gravity / noclip-gravity), and external
//  Kill-All. All of these go through MecchaReader::WriteMemory and are gated by
//  the caller (DrawEsp) on the "Enable Memory Writes" master switch.
// =============================================================================

namespace {
void AppendOverlayLog(const std::string& message)
{
    std::ofstream out("overlay_log.txt", std::ios::app);
    if (out.is_open()) {
        out << message << "\n";
    }
}

} // namespace

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

uintptr_t MecchaReader::ResolveMoverSettings(uintptr_t pawn) const
{
    // pawn -> CharacterMotionComponent (UCharacterMoverComponent) -> SharedSettings[0]
    // (the UCommonLegacyMovementSettings that holds the live MaxSpeed/JumpUpwardsSpeed).
    if (!IsReadablePointer(pawn)) {
        return 0;
    }

    uintptr_t mover = 0;
    if (!ReadMemory(pawn + Offsets::BP_Main_CharacterMotionComponent, &mover, sizeof(mover)) || !IsReadablePointer(mover)) {
        return 0;
    }

    RemoteArray sharedSettings{};
    if (!ReadMemory(mover + Offsets::Mover_SharedSettingsArray, &sharedSettings, sizeof(sharedSettings)) ||
        !IsReadablePointer(sharedSettings.Data) || sharedSettings.Count <= 0 || sharedSettings.Count > 32) {
        return 0;
    }

    uintptr_t settings = 0;
    if (!ReadMemory(sharedSettings.Data, &settings, sizeof(settings)) || !IsReadablePointer(settings)) {
        return 0;
    }
    return settings;
}

void MecchaReader::ApplyMovementHacks(uintptr_t pawn)
{
    if (!IsReadablePointer(pawn)) {
        return;
    }

    const uintptr_t settings = ResolveMoverSettings(pawn);

    // ---- Speed hack (BP default scalars + live Mover MaxSpeed) ----
    if (enableSpeedHack) {
        double currentDefault = 0.0;
        if (ReadMemory(pawn + Offsets::BP_Main_DefaultSpeed, &currentDefault, sizeof(currentDefault))) {
            if (!speedBackupValid_) {
                originalDefaultSpeed_ = currentDefault;
                ReadMemory(pawn + Offsets::BP_Main_DefaultMaxWalkSpeed, &originalDefaultMaxWalkSpeed_, sizeof(originalDefaultMaxWalkSpeed_));
                speedBackupValid_ = true;
            }
            const double target = static_cast<double>(speedHackValue);
            WriteMemory(pawn + Offsets::BP_Main_DefaultSpeed, &target, sizeof(target));
            WriteMemory(pawn + Offsets::BP_Main_DefaultMaxWalkSpeed, &target, sizeof(target));
        }
        if (settings) {
            float current = 0.0f;
            if (ReadMemory(settings + Offsets::MoverSettings_MaxSpeed, &current, sizeof(current))) {
                if (!moverSpeedBackupValid_) {
                    originalMoverMaxSpeed_ = current;
                    moverSpeedBackupValid_ = true;
                }
                WriteMemory(settings + Offsets::MoverSettings_MaxSpeed, &speedHackValue, sizeof(speedHackValue));
            }
        }
    } else {
        if (speedBackupValid_) {
            WriteMemory(pawn + Offsets::BP_Main_DefaultSpeed, &originalDefaultSpeed_, sizeof(originalDefaultSpeed_));
            WriteMemory(pawn + Offsets::BP_Main_DefaultMaxWalkSpeed, &originalDefaultMaxWalkSpeed_, sizeof(originalDefaultMaxWalkSpeed_));
            speedBackupValid_ = false;
        }
        if (moverSpeedBackupValid_ && settings) {
            WriteMemory(settings + Offsets::MoverSettings_MaxSpeed, &originalMoverMaxSpeed_, sizeof(originalMoverMaxSpeed_));
            moverSpeedBackupValid_ = false;
        }
    }

    // ---- Super jump (BP default scalar + live Mover JumpUpwardsSpeed) ----
    if (enableSuperJump) {
        float currentDefault = 0.0f;
        if (ReadMemory(pawn + Offsets::BP_Main_DefaultJumpSpeed, &currentDefault, sizeof(currentDefault))) {
            if (!jumpBackupValid_) {
                originalDefaultJumpSpeed_ = currentDefault;
                jumpBackupValid_ = true;
            }
            WriteMemory(pawn + Offsets::BP_Main_DefaultJumpSpeed, &superJumpValue, sizeof(superJumpValue));
        }
        if (settings) {
            float current = 0.0f;
            if (ReadMemory(settings + Offsets::MoverSettings_JumpUpwardsSpeed, &current, sizeof(current))) {
                if (!moverJumpBackupValid_) {
                    originalMoverJumpSpeed_ = current;
                    moverJumpBackupValid_ = true;
                }
                WriteMemory(settings + Offsets::MoverSettings_JumpUpwardsSpeed, &superJumpValue, sizeof(superJumpValue));
            }
        }
    } else {
        if (jumpBackupValid_) {
            WriteMemory(pawn + Offsets::BP_Main_DefaultJumpSpeed, &originalDefaultJumpSpeed_, sizeof(originalDefaultJumpSpeed_));
            jumpBackupValid_ = false;
        }
        if (moverJumpBackupValid_ && settings) {
            WriteMemory(settings + Offsets::MoverSettings_JumpUpwardsSpeed, &originalMoverJumpSpeed_, sizeof(originalMoverJumpSpeed_));
            moverJumpBackupValid_ = false;
        }
    }

    // ---- Gravity scale / Noclip zero-gravity (UMoverComponent inline override) ----
    uintptr_t mover = 0;
    if (ReadMemory(pawn + Offsets::BP_Main_CharacterMotionComponent, &mover, sizeof(mover)) && IsReadablePointer(mover)) {
        if (enableNoclip || enableGravityScale) {
            Vec3d currentGravity{};
            if (ReadMemory(mover + Offsets::Mover_GravityAccelOverride, &currentGravity, sizeof(currentGravity))) {
                if (!gravityBackupValid_) {
                    originalGravity_.X = currentGravity.X;
                    originalGravity_.Y = currentGravity.Y;
                    originalGravity_.Z = currentGravity.Z;
                    ReadMemory(mover + Offsets::Mover_bHasGravityOverride, &originalHasGravityOverride_, sizeof(originalHasGravityOverride_));
                    gravityBackupValid_ = true;
                }
                const uint8_t hasOverride = 1;
                Vec3d desired{};
                if (enableNoclip) {
                    desired = { 0.0, 0.0, 0.0 };
                } else {
                    desired = { 0.0, 0.0, -980.0 * static_cast<double>(gravityScaleValue) };
                }
                WriteMemory(mover + Offsets::Mover_bHasGravityOverride, &hasOverride, sizeof(hasOverride));
                WriteMemory(mover + Offsets::Mover_GravityAccelOverride, &desired, sizeof(desired));
            }
        } else if (gravityBackupValid_) {
            const Vec3d restore{ originalGravity_.X, originalGravity_.Y, originalGravity_.Z };
            WriteMemory(mover + Offsets::Mover_bHasGravityOverride, &originalHasGravityOverride_, sizeof(originalHasGravityOverride_));
            WriteMemory(mover + Offsets::Mover_GravityAccelOverride, &restore, sizeof(restore));
            gravityBackupValid_ = false;
        }
    }
}

void MecchaReader::RestoreMovementHacks(uintptr_t pawn)
{
    if (process_ && IsReadablePointer(pawn)) {
        const uintptr_t settings = ResolveMoverSettings(pawn);
        if (speedBackupValid_) {
            WriteMemory(pawn + Offsets::BP_Main_DefaultSpeed, &originalDefaultSpeed_, sizeof(originalDefaultSpeed_));
            WriteMemory(pawn + Offsets::BP_Main_DefaultMaxWalkSpeed, &originalDefaultMaxWalkSpeed_, sizeof(originalDefaultMaxWalkSpeed_));
        }
        if (jumpBackupValid_) {
            WriteMemory(pawn + Offsets::BP_Main_DefaultJumpSpeed, &originalDefaultJumpSpeed_, sizeof(originalDefaultJumpSpeed_));
        }
        if (settings) {
            if (moverSpeedBackupValid_) {
                WriteMemory(settings + Offsets::MoverSettings_MaxSpeed, &originalMoverMaxSpeed_, sizeof(originalMoverMaxSpeed_));
            }
            if (moverJumpBackupValid_) {
                WriteMemory(settings + Offsets::MoverSettings_JumpUpwardsSpeed, &originalMoverJumpSpeed_, sizeof(originalMoverJumpSpeed_));
            }
            // Clear the Mover backup flags only once the values are actually
            // written back. If ResolveMoverSettings transiently failed
            // (settings == 0) the flags stay set so the per-frame master-off
            // restore retries next frame instead of permanently leaving the
            // hacked MaxSpeed / JumpUpwardsSpeed in the live game. Detach()
            // force-clears them regardless so a re-attach re-captures fresh.
            moverSpeedBackupValid_ = false;
            moverJumpBackupValid_ = false;
        }
        uintptr_t mover = 0;
        if (gravityBackupValid_ && ReadMemory(pawn + Offsets::BP_Main_CharacterMotionComponent, &mover, sizeof(mover)) && IsReadablePointer(mover)) {
            const Vec3d restore{ originalGravity_.X, originalGravity_.Y, originalGravity_.Z };
            WriteMemory(mover + Offsets::Mover_bHasGravityOverride, &originalHasGravityOverride_, sizeof(originalHasGravityOverride_));
            WriteMemory(mover + Offsets::Mover_GravityAccelOverride, &restore, sizeof(restore));
        }
        if (originalCollisionByteValid_) {
            WriteMemory(pawn + Offsets::AActor_CollisionFlagsByte, &originalCollisionByte_, sizeof(originalCollisionByte_));
        }
    }

    speedBackupValid_ = false;
    jumpBackupValid_ = false;
    gravityBackupValid_ = false;
    originalCollisionByteValid_ = false;
    noclipActive_ = false;
    lastNoclipTick_ = 0;
    // moverSpeedBackupValid_ / moverJumpBackupValid_ are cleared above only when
    // the restore actually ran (settings resolved); left set otherwise to retry.
}

bool MecchaReader::KillAllPlayersExternal(const std::vector<uintptr_t>& targetActors, uintptr_t localPawn)
{
    lastKillAllTargetCount_ = 0;
    lastKillAllSuccessCount_ = 0;
    lastKillAllFailCount_ = 0;

    if (!process_) {
        SetStatus("Kill all failed: not attached");
        return false;
    }

    std::unordered_set<uintptr_t> processed;
    for (uintptr_t actor : targetActors) {
        if (!IsReadablePointer(actor) || actor == localPawn || !processed.insert(actor).second) {
            continue;
        }

        // Skip already-dead targets to keep the count meaningful.
        uint8_t deadBefore = 0;
        ReadMemory(actor + Offsets::BP_Main_Dead, &deadBefore, sizeof(deadBefore));
        if (deadBefore != 0) {
            continue;
        }

        double healthBefore = 0.0;
        double maxHealth = 0.0;
        ReadMemory(actor + Offsets::BP_Main_Health, &healthBefore, sizeof(healthBefore));
        ReadMemory(actor + Offsets::BP_Main_MaxHealthValue, &maxHealth, sizeof(maxHealth));
        if (!std::isfinite(maxHealth) || maxHealth <= 0.0 || maxHealth > 100000.0) {
            maxHealth = 100.0;
        }
        if (!std::isfinite(healthBefore) || healthBefore <= 0.0 || healthBefore > 100000.0) {
            healthBefore = maxHealth;
        }

        ++lastKillAllTargetCount_;

        const uint8_t falseByte = 0;
        const double zeroHealth = 0.0;
        const double previousHealth = healthBefore;

        bool wrote = true;
        wrote = WriteMemory(actor + Offsets::BP_Main_Invincible, &falseByte, sizeof(falseByte)) && wrote;
        wrote = WriteMemory(actor + Offsets::BP_Main_ChangeBeforeHealth, &previousHealth, sizeof(previousHealth)) && wrote;
        wrote = WriteMemory(actor + Offsets::BP_Main_Health, &zeroHealth, sizeof(zeroHealth)) && wrote;

        double healthAfter = healthBefore;
        ReadMemory(actor + Offsets::BP_Main_Health, &healthAfter, sizeof(healthAfter));
        const bool verified = std::isfinite(healthAfter) && healthAfter <= 0.001;

        char targetLog[320]{};
        std::snprintf(targetLog, sizeof(targetLog),
            "KillAll target[%d] actor=0x%llX hp %.3f->%.3f wrote=%d",
            lastKillAllTargetCount_ - 1,
            static_cast<unsigned long long>(actor),
            healthBefore, healthAfter, wrote ? 1 : 0);
        AppendOverlayLog(targetLog);

        if (wrote && verified) {
            ++lastKillAllSuccessCount_;
        } else {
            ++lastKillAllFailCount_;
            ++lastWriteFailCount_;
        }
    }

    SetStatus("Kill all (client WPM, no RPC confirmation): targets %d, ok %d, fail %d",
        lastKillAllTargetCount_, lastKillAllSuccessCount_, lastKillAllFailCount_);
    return lastKillAllTargetCount_ > 0 && lastKillAllSuccessCount_ == lastKillAllTargetCount_;
}
