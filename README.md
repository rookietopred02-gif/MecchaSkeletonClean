# Meccha Box ESP - External Cheat Overlay (PenguinHotel / MECCHA CHAMELEON)

Meccha Box ESP is a highly optimized, external Direct3D 11 & ImGui cheat overlay tailored for MECCHA CHAMELEON (running on Unreal Engine 5 as `PenguinHotel-Win64-Shipping.exe`).

## Features
- **Clean External D3D11 Overlay**: Completely standalone process, rendering via a high-performance transparent window overlay.
  - Press the **`INSERT`** key to toggle the cheat menu UI.
  - Press the **`F5`** key to instantly kill/close the cheat process (Panic switch).
- **Stable Foot Anchor Anchor ESP**: Uses the most stable bone (Index 0 from `CachedComponentSpaceTransforms + 0x928`) on the `Actor + 0x418` body mesh to identify the player's exact bottom-center anchor.
- **Accurate 2D Bounding Boxes**: Computes precise head projections dynamically scaled with world heights, giving beautifully fitting box coordinates without relying on fragile skeletal connections.
- **High-Performance Architecture**: Limitless FPS rendering, dynamic actor frame capping, distance-based culling, and camera POV manager cache projection.
- **External Memory Access**: Designed to interact externally with the game process, providing a robust foundation for memory reading and future writing implementations.

## Offsets Summary (UE5 Double-based)

| Engine Value | Offset | Description |
| :--- | :--- | :--- |
| **GWorld** | `0x9C70620` | Engine World pointer |
| **UWorld_PersistentLevel** | `0x30` | Active levels pointer |
| **UWorld_Levels** | `0x1C8` | TArray of loaded levels |
| **ULevel_Actors** | `0xA0` | Level Actors list |
| **OwningGameInstance** | `0x228` | Local game instance |
| **LocalPlayers** | `0x38` | Local players list |
| **PlayerController** | `0x30` | Active controller |
| **AcknowledgedPawn** | `0x350` | Contrained player pawn |
| **PlayerCameraManager** | `0x360` | Camera manager instance |
| **CameraCachePrivate** | `0x1530` | Direct Camera entries POV |
| **FCameraCacheEntry_POV** | `0x10` | Rotation, Translation, FOV |
| **Body Mesh Component** | `Actor + 0x418` | Skeletal mesh instance |
| **ComponentToWorld** | `Component + 0x1E0` | Mesh local-to-world transform |
| **CachedComponentSpaceTransforms** | `Component + 0x928` | Bone space transform arrays |

## Configuration Controls
- **Enable ESP / Draw Boxes / Draw Foot Anchor**: Toggle specific visual helpers.
- **Skip Local/Spectated Pawn**: Ensures your camera target or active pawn won't clutter the ESP.
- **Customizable Dimension**: Tune Box Height, Width Ratio, Line Thickness, Dot Radius, and Min Camera Distance inside the overlay UI.
- **Performance Limits**: Control Max Actors scanned per frame and Max Body Transform count to optimize CPU overhead.

## Building and Verification
### Requirements
- **Visual Studio 2022** (v143 Toolset)
- **DirectX 11 SDK** (standard on Windows SDK)

### Compile and Run Instructions
1. Open `MecchaSkeletonClean.vcxproj` in Visual Studio 2022.
2. Select target configuration: **Release \| x64** or **Debug \| x64**.
3. Rebuild the solution. 
4. The executable will compile to `x64/Release/MecchaSkeletonClean.exe` or `x64/Debug/MecchaSkeletonClean.exe`.
5. Run the compiled overlay executable alongside the game.
   - Press **`INSERT`** to toggle the cheat menu.
   - Press **`F5`** to kill the cheat process instantly.

## License
This project is intended strictly for educational, security analysis, and academic research purposes only. Avoid using this implementation in environments where security or competitive integrity is actively enforced.
