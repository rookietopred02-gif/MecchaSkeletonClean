# Overlay — External D3D11 + ImGui tool for PenguinHotel (UE5.6 "Chameleon" / LINK)

A standalone Direct3D 11 + Dear ImGui overlay for the Unreal Engine 5.6 build
`PenguinHotel-Win64-Shipping.exe`. It reads (and, for a few features, writes)
the game process via `ReadProcessMemory` / `WriteProcessMemory`. All engine and
game offsets come from the SDK-derived layer in [`src/Offsets.hpp`](src/Offsets.hpp),
which is generated/verified against the Dumper-7 dump of this exact build
(`5.6.1-44394996 ... Chameleon`).

> This is a single merged project. The old `src/` (Kill All) and `fork/`
> (movement / aimbot / config) lines were consolidated into `src/` and `fork/`
> was removed. Nothing was deleted feature-wise.

## Controls
- **Insert** — toggle the menu UI.
- **F5** — close the overlay (panic).
- **F6** freecam, **F7** sample camo colour, **F8** kill-all (when enabled).

## Features
- **Visuals (read-only):** master ESP, 2D / corner boxes, bone-bounds boxes,
  foot anchor, bone skeleton + indices, role/King labels, distance label,
  **health bar**, **frozen-teammate marker**, crosshair lines.
- **Blinking highlight (overlay-drawn):** a pulsing translucent silhouette.
  Replaces the old custom-depth "chams", which rendered solid black because it
  required an in-game post-process material the tool cannot supply.
- **Memory-write features (gated by the "Enable Memory Writes" master switch):**
  freecam, gravity scale, speed / super-jump (via the UE5 Mover plugin settings),
  noclip, aimbot, kill-all.
- **Config** auto-saves to `meccha_config.txt` next to the executable.

## Offsets
The current build runs the symmetric co-op **LINK** mode (the old asymmetric
"cLeon" Hunter/Survivor mode is gone), and the player pawn uses the UE5 **Mover
plugin** rather than `CharacterMovementComponent`. See the annotated constants
and the OBSOLETE notes in [`src/Offsets.hpp`](src/Offsets.hpp).

## Not implemented (by design)
- **Server-side / replicated effects** (e.g. the game's `SetBodyColorIndex_Server_`
  RPC for camouflage others can see, or hijacking the `DamagedEvent` /
  `DamagedAnimation` networked events). These would require an external
  remote-function-invocation primitive that can call arbitrary server RPCs on an
  online multiplayer game — out of scope for this tool.
- A few toggles (Penetration, Stealth, No-Cooldown) are present but **guarded**:
  the LINK build has no corresponding field, so they intentionally do nothing
  rather than writing to a wrong offset and corrupting memory.

## Building
1. Open `MecchaSkeletonClean.vcxproj` in Visual Studio 2022 / 18 (toolset v145).
2. Select **Release | x64** and build.
3. Output: `x64/Release/MecchaSkeletonClean.exe`.

## License
For educational, security-analysis, and academic research purposes only. Do not
use against online services or other players where competitive integrity or terms
of service are enforced.
