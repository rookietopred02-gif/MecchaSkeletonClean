#include "MecchaReader.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// =============================================================================
//  Config persistence (table-driven)
// -----------------------------------------------------------------------------
//  Every persisted setting is registered exactly once in kConfigFields below.
//  Save / Load / Hash all iterate that single table, so adding a setting means
//  adding one row here instead of editing three parallel lists.
//
//  On-disk format is unchanged from the old hand-written version (bools as
//  0/1, ints/floats via the default stream formatting), so existing
//  meccha_config.txt files keep working. The one behaviour change is that bool
//  loading is now tolerant: the previous code read bools with std::boolalpha,
//  which could not parse the 0/1 it had itself written and silently forced
//  every bool to false on load. ParseBool below accepts 0/1 and true/false.
// =============================================================================

namespace {

// A single registered setting: its file key and a pointer-to-member. RGB array
// channels are stored as the array member plus the channel index.
struct RgbChannel {
    int (MecchaReader::*Array)[3] = nullptr;
    int Index = 0;
};

using ConfigMember = std::variant<
    bool MecchaReader::*,
    int MecchaReader::*,
    float MecchaReader::*,
    RgbChannel>;

struct ConfigField {
    const char* Key;
    ConfigMember Member;
};

const std::vector<ConfigField>& ConfigFields()
{
    static const std::vector<ConfigField> fields = {
        { "enableEsp", &MecchaReader::enableEsp },
        { "autoAttach", &MecchaReader::autoAttach },
        { "skipLocalPawn", &MecchaReader::skipLocalPawn },
        { "drawFootDot", &MecchaReader::drawFootDot },
        { "drawBoxes", &MecchaReader::drawBoxes },
        { "drawCornerBoxes", &MecchaReader::drawCornerBoxes },
        { "useBoneBoundsForBoxes", &MecchaReader::useBoneBoundsForBoxes },
        { "drawCrosshairEnemyLines", &MecchaReader::drawCrosshairEnemyLines },
        { "drawBoneIndices", &MecchaReader::drawBoneIndices },
        { "drawRoleEsp", &MecchaReader::drawRoleEsp },
        { "drawStateEsp", &MecchaReader::drawStateEsp },
        { "drawHealthBar", &MecchaReader::drawHealthBar },
        { "drawFreezeEsp", &MecchaReader::drawFreezeEsp },
        { "onlyPlayerBodyMesh", &MecchaReader::onlyPlayerBodyMesh },
        { "enableMemoryWrites", &MecchaReader::enableMemoryWrites },
        { "enablePlayerHighlight", &MecchaReader::enablePlayerHighlight },
        { "playerHighlightHitFlash", &MecchaReader::playerHighlightHitFlash },
        { "highlightBlinkSpeed", &MecchaReader::highlightBlinkSpeed },
        { "enableSeekerWarning", &MecchaReader::enableSeekerWarning },
        { "enableFreecam", &MecchaReader::enableFreecam },
        { "enableSpeedHack", &MecchaReader::enableSpeedHack },
        { "speedHackValue", &MecchaReader::speedHackValue },
        { "enableSuperJump", &MecchaReader::enableSuperJump },
        { "superJumpValue", &MecchaReader::superJumpValue },
        { "enableGravityScale", &MecchaReader::enableGravityScale },
        { "gravityScaleValue", &MecchaReader::gravityScaleValue },
        { "enableNoCooldown", &MecchaReader::enableNoCooldown },
        { "enablePenetration", &MecchaReader::enablePenetration },
        { "enableStealthMode", &MecchaReader::enableStealthMode },
        { "enableNoclip", &MecchaReader::enableNoclip },
        { "noclipSpeed", &MecchaReader::noclipSpeed },
        { "enableAimbot", &MecchaReader::enableAimbot },
        { "aimbotKey", &MecchaReader::aimbotKey },
        { "aimbotTargetType", &MecchaReader::aimbotTargetType },
        { "aimbotBoneIndex", &MecchaReader::aimbotBoneIndex },
        { "aimbotFov", &MecchaReader::aimbotFov },
        { "aimbotSmooth", &MecchaReader::aimbotSmooth },
        { "aimbotSmoothSpeed", &MecchaReader::aimbotSmoothSpeed },
        { "maxActorsPerFrame", &MecchaReader::maxActorsPerFrame },
        { "bodyTransformCount", &MecchaReader::bodyTransformCount },
        { "maxBonesPerMesh", &MecchaReader::maxBonesPerMesh },
        { "playerHighlightRgb0", RgbChannel{ &MecchaReader::playerHighlightRgb, 0 } },
        { "playerHighlightRgb1", RgbChannel{ &MecchaReader::playerHighlightRgb, 1 } },
        { "playerHighlightRgb2", RgbChannel{ &MecchaReader::playerHighlightRgb, 2 } },
        { "lineThickness", &MecchaReader::lineThickness },
        { "dotRadius", &MecchaReader::dotRadius },
        { "minCameraDistance", &MecchaReader::minCameraDistance },
        { "boxWorldHeight", &MecchaReader::boxWorldHeight },
        { "boxWidthRatio", &MecchaReader::boxWidthRatio },
        { "boxPaddingPixels", &MecchaReader::boxPaddingPixels },
        { "seekerWarningAngleDegrees", &MecchaReader::seekerWarningAngleDegrees },
        { "seekerWarningMaxDistance", &MecchaReader::seekerWarningMaxDistance },
        { "freecamSpeed", &MecchaReader::freecamSpeed },
        { "freecamFastMultiplier", &MecchaReader::freecamFastMultiplier },
        { "freecamMouseSensitivity", &MecchaReader::freecamMouseSensitivity },
    };
    return fields;
}

bool ParseBool(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

std::string GetConfigFilePath()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string strPath(path);
    const size_t lastSlash = strPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return strPath.substr(0, lastSlash + 1) + "meccha_config.txt";
    }
    return "meccha_config.txt";
}

} // namespace

void MecchaReader::SaveConfig()
{
    std::ofstream f(GetConfigFilePath());
    if (!f.is_open()) {
        return;
    }

    for (const ConfigField& field : ConfigFields()) {
        f << field.Key << "=";
        std::visit([&](auto member) {
            using M = decltype(member);
            if constexpr (std::is_same_v<M, RgbChannel>) {
                f << (this->*member.Array)[member.Index];
            } else {
                f << this->*member;
            }
        }, field.Member);
        f << "\n";
    }
}

void MecchaReader::LoadConfig()
{
    std::ifstream f(GetConfigFilePath());
    if (!f.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key.empty() || val.empty()) {
            continue;
        }

        for (const ConfigField& field : ConfigFields()) {
            if (key != field.Key) {
                continue;
            }
            std::visit([&](auto member) {
                using M = decltype(member);
                if constexpr (std::is_same_v<M, bool MecchaReader::*>) {
                    this->*member = ParseBool(val);
                } else if constexpr (std::is_same_v<M, RgbChannel>) {
                    std::stringstream vs(val);
                    vs >> (this->*member.Array)[member.Index];
                } else {
                    std::stringstream vs(val);
                    vs >> this->*member;
                }
            }, field.Member);
            break;
        }
    }
}

std::string MecchaReader::GetConfigHash() const
{
    std::stringstream ss;
    for (const ConfigField& field : ConfigFields()) {
        std::visit([&](auto member) {
            using M = decltype(member);
            if constexpr (std::is_same_v<M, RgbChannel>) {
                ss << (this->*member.Array)[member.Index];
            } else {
                ss << this->*member;
            }
        }, field.Member);
        ss << "|";
    }
    return ss.str();
}

void MecchaReader::CheckAndAutoSave()
{
    // Only auto-save while attached so the not-attached default state cannot
    // overwrite the saved config on startup.
    if (!IsAttached()) {
        return;
    }

    // Throttle FIRST so the hash (which allocates a stringstream + visits ~55
    // fields) is only built about once per 1.5s instead of every frame.
    const ULONGLONG now = GetTickCount64();
    if (now - lastConfigSaveTick_ < 1500) {
        return;
    }
    lastConfigSaveTick_ = now;

    std::string currentHash = GetConfigHash();
    if (currentHash != lastConfigHash_) {
        SaveConfig();
        lastConfigHash_ = std::move(currentHash);
    }
}
