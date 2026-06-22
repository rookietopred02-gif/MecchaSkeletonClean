#include "MecchaReader.h"
#include "imgui.h"

// =============================================================================
//  ImGui control panel. Pure UI: reads/writes the public MecchaReader feature
//  flags and tunables and triggers Attach/Detach. The actual work happens in
//  DrawEsp (read/render) and the write-side helpers.
// =============================================================================
void MecchaReader::DrawControls()
{
    ImGui::TextColored(ImVec4(0.68f, 0.25f, 0.98f, 1.00f), "Overlay");
    ImGui::Separator();

    if (!IsAttached()) {
        if (ImGui::Button("Attach to game", ImVec2(-1, 30))) {
            Attach();
        }
        ImGui::Separator();
        ImGui::Text("Status: %s", status_);
        return;
    }

    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Attached");
    ImGui::SameLine();
    ImGui::Text("| PID: %lu | Base: 0x%llX", pid_, static_cast<unsigned long long>(moduleBase_));
    if (ImGui::Button("Detach Process", ImVec2(-1, 24))) {
        Detach();
        SetStatus("Detached");
        return;
    }

    ImGui::Checkbox("Enable Memory Writes (master)", &enableMemoryWrites);

    ImGui::Separator();

    if (ImGui::BeginTabBar("MecchaTabBar", ImGuiTabBarFlags_None)) {

        // --- TAB 1: VISUALS (ESP) ---
        if (ImGui::BeginTabItem("Visuals (ESP)")) {
            ImGui::Checkbox("Enable Master ESP", &enableEsp);
            ImGui::Checkbox("Skip Local/Spectated Pawn", &skipLocalPawn);
            ImGui::Checkbox("Only Player Body Mesh", &onlyPlayerBodyMesh);

            ImGui::Separator();
            ImGui::Checkbox("Draw 2D Boxes", &drawBoxes);
            if (drawBoxes) {
                ImGui::Checkbox("Use Corner Boxes", &drawCornerBoxes);
                ImGui::Checkbox("Use Bone Bounds for Boxes", &useBoneBoundsForBoxes);
                ImGui::SliderFloat("Box Padding (px)", &boxPaddingPixels, 0.0f, 24.0f, "%.0f");
                ImGui::SliderFloat("Box World Height", &boxWorldHeight, 80.0f, 260.0f, "%.0f");
                ImGui::SliderFloat("Box Width Ratio", &boxWidthRatio, 0.20f, 0.90f, "%.2f");
            }

            ImGui::Separator();
            ImGui::Checkbox("Draw Crosshair Player Lines", &drawCrosshairEnemyLines);
            ImGui::Checkbox("Draw Foot Anchor Dot", &drawFootDot);
            if (drawFootDot) {
                ImGui::SliderFloat("Dot Radius", &dotRadius, 1.0f, 6.0f, "%.1f");
            }

            ImGui::Checkbox("Draw Role/King Labels", &drawRoleEsp);
            ImGui::Checkbox("Draw State Labels (distance)", &drawStateEsp);
            ImGui::Checkbox("Draw Health Bar", &drawHealthBar);
            ImGui::Checkbox("Draw Frozen Marker", &drawFreezeEsp);
            ImGui::Checkbox("Draw Bone Indices", &drawBoneIndices);

            ImGui::Separator();
            ImGui::SliderFloat("Line Thickness", &lineThickness, 0.5f, 5.0f, "%.1f");
            ImGui::SliderFloat("Min Camera Distance", &minCameraDistance, 0.0f, 600.0f, "%.0f");

            ImGui::Separator();
            ImGui::Checkbox("Player Highlight (overlay)", &enablePlayerHighlight);
            if (enablePlayerHighlight) {
                ImGui::Checkbox("Hit-Flash Mode (sharp white flash)", &playerHighlightHitFlash);
                ImGui::SliderInt3("Highlight RGB Color", playerHighlightRgb, 0, 255);
                ImGui::SliderFloat(playerHighlightHitFlash ? "Flash Rate (per sec)" : "Blink Speed",
                    &highlightBlinkSpeed, 0.5f, 12.0f, "%.1f");
            }

            ImGui::EndTabItem();
        }

        // --- TAB 2: MOVEMENT & HACKS ---
        if (ImGui::BeginTabItem("Movement & Hacks")) {
            ImGui::TextColored(ImVec4(0.68f, 0.25f, 0.98f, 1.00f), "Local Physics Modifier (Mover plugin):");
            ImGui::Checkbox("Speed Hack (Speedup)", &enableSpeedHack);
            if (enableSpeedHack) {
                ImGui::SliderFloat("Speed Value", &speedHackValue, 300.0f, 3000.0f, "%.0f");
            }

            ImGui::Checkbox("Super Jump (JumpZ)", &enableSuperJump);
            if (enableSuperJump) {
                ImGui::SliderFloat("Jump Velocity", &superJumpValue, 300.0f, 3000.0f, "%.0f");
            }

            ImGui::Checkbox("Gravity Scale Modifier", &enableGravityScale);
            if (enableGravityScale) {
                ImGui::SliderFloat("Gravity Scale", &gravityScaleValue, -2.0f, 2.0f, "%.2f");
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.68f, 0.25f, 0.98f, 1.00f), "Pawn & Stealth Hacks:");
            ImGui::Checkbox("Noclip (Wall Pass Mode)", &enableNoclip);
            if (enableNoclip) {
                ImGui::SliderFloat("Noclip Fly Speed", &noclipSpeed, 100.0f, 4000.0f, "%.0f");
            }
            ImGui::Checkbox("Penetration Mode", &enablePenetration);
            ImGui::Checkbox("Stealth Mode (Local Invisibility)", &enableStealthMode);
            ImGui::Checkbox("Hunter Weapon No Cooldown", &enableNoCooldown);
            if (enablePenetration || enableStealthMode || enableNoCooldown) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                    "Note: these 3 have no field in the current LINK build\n(guarded no-op to avoid memory corruption).");
            }

            ImGui::Separator();
            if (ImGui::Button("Sample Camo Color (F7)", ImVec2(-1, 26))) {
                autoCamoRequested_ = true;
            }

            ImGui::EndTabItem();
        }

        // --- TAB 3: AIMBOT ---
        if (ImGui::BeginTabItem("Master Aimbot")) {
            ImGui::Checkbox("Master Aimbot", &enableAimbot);
            if (enableAimbot) {
                ImGui::SliderFloat("Aimbot FOV Radius", &aimbotFov, 5.0f, 300.0f, "%.0f");

                const char* targetTypes[] = { "Stable Head", "Stable Chest", "Bone Index" };
                ImGui::Combo("Aimbot Target", &aimbotTargetType, targetTypes, IM_ARRAYSIZE(targetTypes));

                if (aimbotTargetType == 2) {
                    ImGui::SliderInt("Aimbot Target Bone", &aimbotBoneIndex, 0, 100);
                }

                ImGui::Checkbox("Aimbot Smooth", &aimbotSmooth);
                if (aimbotSmooth) {
                    ImGui::SliderFloat("Aimbot Smooth Speed", &aimbotSmoothSpeed, 1.0f, 100.0f, "%.1f");
                }
            }
            ImGui::EndTabItem();
        }

        // --- TAB 4: COMBAT (Kill All) ---
        if (ImGui::BeginTabItem("Combat")) {
            const bool canKill = IsAttached() && enableMemoryWrites && localPawnValid_;
            if (!canKill) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Kill All Other Players (F8)", ImVec2(-1, 26))) {
                killAllRequested_ = true;
            }
            if (!canKill) {
                ImGui::EndDisabled();
            }
            ImGui::Text("Players: %d | Kill targets: %d | OK: %d | Fail: %d",
                lastPlayerCount_, lastKillAllTargetCount_, lastKillAllSuccessCount_, lastKillAllFailCount_);
            ImGui::TextWrapped(
                "Writes Health=0 on every other player character (offsets SDK-verified). "
                "Client-side only: a server-authoritative game may revert it. Requires "
                "'Enable Memory Writes'.");
            ImGui::EndTabItem();
        }

        // --- TAB 5: DIAGNOSTICS & INFO ---
        if (ImGui::BeginTabItem("Diagnostics & Info")) {
            ImGui::Checkbox("Seeker View Warning (Anti-Target)", &enableSeekerWarning);
            if (enableSeekerWarning) {
                ImGui::SliderFloat("Warning Cone (Deg)", &seekerWarningAngleDegrees, 3.0f, 60.0f, "%.0f");
                ImGui::SliderFloat("Warning Max Distance", &seekerWarningMaxDistance, 500.0f, 20000.0f, "%.0f");
            }

            ImGui::Separator();
            if (ImGui::Checkbox("Freecam Mode (F6)", &enableFreecam) && !enableFreecam) {
                RestoreFreecamCamera();
            }
            if (enableFreecam) {
                ImGui::SliderFloat("Freecam speed", &freecamSpeed, 50.0f, 4000.0f, "%.0f");
                ImGui::SliderFloat("Freecam fast multiplier", &freecamFastMultiplier, 1.0f, 10.0f, "%.1f");
                ImGui::SliderFloat("Freecam mouse sensitivity", &freecamMouseSensitivity, 0.02f, 1.0f, "%.2f");
            }

            ImGui::Separator();
            ImGui::SliderInt("Max actors/frame", &maxActorsPerFrame, 128, 20000);
            ImGui::SliderInt("Body transform count", &bodyTransformCount, 1, 128);
            ImGui::SliderInt("Max bones/mesh", &maxBonesPerMesh, 8, 256);

            ImGui::Separator();
            ImGui::Text("World: 0x%llX | PC: 0x%llX", static_cast<unsigned long long>(lastWorld_), static_cast<unsigned long long>(lastPlayerController_));
            ImGui::Text("LocalPawn: 0x%llX | King: 0x%llX",
                static_cast<unsigned long long>(lastLocalPawn_), static_cast<unsigned long long>(lastKingActor_));
            ImGui::Text("Actors: %d | Comp: %d | Players: %d", lastActorCount_, lastComponentCandidates_, lastPlayerCount_);
            ImGui::Text("Render: Targets: %d | Boxes: %d | Lines: %d | Bones: %d",
                lastTargetCount_, lastBoxCount_, lastCrosshairLineCount_, lastIndexCount_);
            ImGui::Text("Highlights: %d | Warnings: %d | Write fails: %d | Xform off: 0x%llX",
                lastHighlightedCount_, lastWarningCount_, lastWriteFailCount_,
                static_cast<unsigned long long>(lastTransformArrayOffset_));
            ImGui::TextWrapped("Status: %s", status_);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
