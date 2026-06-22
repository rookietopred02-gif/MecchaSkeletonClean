#pragma once

#include <cstdint>

// =============================================================================
//  MecchaSkeletonClean — SDK-derived offset layer
// -----------------------------------------------------------------------------
//  Single source of truth: the Dumper-7 generated CppSDK for
//      PenguinHotel-Win64-Shipping.exe
//      UE 5.6.1  build 44394996  ("Chameleon", LINK multiplayer mode)
//      C:\Dumper-7\5.6.1-44394996+++UE5+Release-5.6-Chameleon\CppSDK
//
//  Every constant below is annotated with the SDK class::member it was taken
//  from and the exact Dumper-7 offset comment. All values were cross-checked
//  against the dump on 2026-06-22 (see analysis in the repository history).
//
//  GAME-BUILD NOTE (important):
//    The game was updated from the OLD asymmetric "cLeon" mode (Hunter vs
//    Survivor, paint mechanics) to the CURRENT symmetric co-op "LINK" mode.
//    Because of that the old role/paint/visibility member block (0xB78..0xC50)
//    and the cLeon GameState survivor/hunter arrays (0x348 / 0x3A0) NO LONGER
//    EXIST in this build. They have been removed from the live code path and
//    are documented in the OBSOLETE section at the bottom so nobody re-adds
//    them by mistake. Role is therefore derived from the LINK GameState's
//    KingCharacter pointer instead (the only readable role-ish signal).
// =============================================================================

namespace Offsets {

// ---------------------------------------------------------------------------
//  Global RVAs (module-base relative) — SDK Basic.hpp, namespace Offsets
// ---------------------------------------------------------------------------
constexpr uintptr_t GWorld   = 0x09C70620; // Basic.hpp Offsets::GWorld   (matches live)
constexpr uintptr_t GObjects = 0x09F26BD0; // Basic.hpp Offsets::GObjects (reserved/future)
constexpr uintptr_t GNames   = 0x09E0A578; // Basic.hpp Offsets::GNames   (reserved/future)

// ---------------------------------------------------------------------------
//  Engine: world / level / player / camera chain (Engine_classes.hpp)
// ---------------------------------------------------------------------------
constexpr uintptr_t UWorld_PersistentLevel    = 0x30;  // UWorld::PersistentLevel              // 0x0030
constexpr uintptr_t UWorld_GameState          = 0x1B0; // UWorld::GameState                    // 0x01B0
constexpr uintptr_t UWorld_Levels             = 0x1C8; // UWorld::Levels TArray<ULevel*>       // 0x01C8
constexpr uintptr_t UWorld_OwningGameInstance = 0x228; // UWorld::OwningGameInstance           // 0x0228

constexpr uintptr_t ULevel_Actors             = 0xA0;  // ULevel::Actors TArray<AActor*>       // 0x00A0

constexpr uintptr_t AGameStateBase_PlayerArray = 0x2C0; // AGameStateBase::PlayerArray TArray<APlayerState*> // 0x02C0

constexpr uintptr_t UGameInstance_LocalPlayers = 0x38; // UGameInstance::LocalPlayers TArray<ULocalPlayer*>  // 0x0038
constexpr uintptr_t UPlayer_PlayerController   = 0x30; // UPlayer::PlayerController             // 0x0030

constexpr uintptr_t APlayerController_AcknowledgedPawn    = 0x350; // APlayerController::AcknowledgedPawn    // 0x0350
constexpr uintptr_t APlayerController_PlayerCameraManager = 0x360; // APlayerController::PlayerCameraManager // 0x0360
constexpr uintptr_t AController_ControlRotation          = 0x320; // AController::ControlRotation FRotator (3x double) // 0x0320

constexpr uintptr_t APlayerCameraManager_CameraCachePrivate          = 0x1530; // APlayerCameraManager::CameraCachePrivate FCameraCacheEntry          // 0x1530
constexpr uintptr_t APlayerCameraManager_LastFrameCameraCachePrivate = 0x1E00; // APlayerCameraManager::LastFrameCameraCachePrivate FCameraCacheEntry // 0x1E00
constexpr uintptr_t FCameraCacheEntry_POV = 0x10; // FCameraCacheEntry::POV FMinimalViewInfo (Location@0x0, Rotation@0x18, FOV@0x30) // 0x0010

constexpr uintptr_t APlayerState_PawnPrivate = 0x320; // APlayerState::PawnPrivate             // 0x0320

// AActor base members (valid on any actor, incl. the player pawn)
constexpr uintptr_t AActor_RootComponent          = 0x1B8; // AActor::RootComponent USceneComponent* // 0x01B8
constexpr uintptr_t AActor_CollisionFlagsByte     = 0x5D;  // bitfield byte (bActorEnableCollision = bit0) // 0x005D
constexpr uint8_t   AActor_EnableCollisionBitMask = 0x01;  //   mask of bActorEnableCollision within 0x5D

// ---------------------------------------------------------------------------
//  Scene / primitive / skeletal components (Engine_classes.hpp)
// ---------------------------------------------------------------------------
constexpr uintptr_t USceneComponent_ComponentToWorld = 0x1E0; // USceneComponent cached FTransform (UE5 std; class size 0x240) // 0x01E0
constexpr uintptr_t USceneComponent_RelativeLocation = 0x140; // USceneComponent::RelativeLocation FVector // 0x0140
constexpr uintptr_t USceneComponent_RelativeRotation = 0x158; // USceneComponent::RelativeRotation FRotator // 0x0158

constexpr uintptr_t UPrimitiveComponent_RenderFlags                 = 0x271; // byte holding render flags; read to validate a primitive component // 0x0271

constexpr uintptr_t USkeletalMeshComponent_CachedComponentSpaceTransforms_Dumper       = 0x9B8; // USkeletalMeshComponent::CachedComponentSpaceTransforms TArray<FTransform> // 0x09B8
constexpr uintptr_t USkeletalMeshComponent_CachedComponentSpaceTransforms_LiveFallback = 0x928; // heuristic alternate offset (NOT a named member); tried if 0x9B8 fails

// ---------------------------------------------------------------------------
//  Player character: ABP_FirstPersonCharacter_LINK_C : _Main_C :
//                    AMoverExamplesCharacter : APawn
//  (BP_FirstPersonCharacter_Main_classes.hpp / _LINK_classes.hpp /
//   MoverExamples_classes.hpp)
// ---------------------------------------------------------------------------
constexpr uintptr_t AActor_PlayerBodyMesh = 0x418; // Main::Mesh USkeletalMeshComponent* (3rd-person body) // 0x0418
constexpr uintptr_t BP_Main_HandBone         = 0x490; // Main::HandBone          // 0x0490
constexpr uintptr_t BP_Main_LeftItemPositon3 = 0x4A0; // Main::LeftItemPositon_3 // 0x04A0
constexpr uintptr_t BP_Main_LeftItemPositon2 = 0x4A8; // Main::LeftItemPositon_2 // 0x04A8
constexpr uintptr_t BP_Main_LeftItemPositon1 = 0x4B8; // Main::LeftItemPositon_1 // 0x04B8 (0x4B0 is SpotLight, skipped)
constexpr uintptr_t BP_Main_RightItemPositon = 0x4C0; // Main::RightItemPositon  // 0x04C0
constexpr uintptr_t BP_Main_FirstPersonMesh  = 0x4C8; // Main::FirstPersonMesh USkeletalMeshComponent* // 0x04C8

constexpr uintptr_t BP_Main_Dead               = 0x5AA; // Main::Dead bool               // 0x05AA
constexpr uintptr_t BP_Main_Invincible         = 0x5AB; // Main::Invincible bool         // 0x05AB
constexpr uintptr_t BP_Main_Health             = 0x640; // Main::Health double            // 0x0640
constexpr uintptr_t BP_Main_MaxHealthValue     = 0x648; // Main::MaxHealthValue double    // 0x0648
constexpr uintptr_t BP_Main_ChangeBeforeHealth = 0x650; // Main::ChangeBeforeHealth double // 0x0650

// Movement: these are BP designer "default" scalars. The LIVE movement values
// are inside the UE5 Mover plugin component (see Mover_* below). We still poke
// the defaults because some BP paths re-apply them; the Mover path is primary.
constexpr uintptr_t BP_Main_DefaultSpeed        = 0x5B0; // Main::DefaultSpeed double        // 0x05B0
constexpr uintptr_t BP_Main_DefaultMaxWalkSpeed = 0x750; // Main::DefaultMaxWalkSpeed double // 0x0750
constexpr uintptr_t BP_Main_DefaultJumpSpeed    = 0x8BC; // Main::DefaultJumpSpeed float     // 0x08BC

constexpr uintptr_t BP_Main_CharacterMotionComponent = 0x358; // AMoverExamplesCharacter::CharacterMotionComponent UCharacterMoverComponent* // 0x0358
constexpr uintptr_t BP_Main_ExtendedPhysicsMover     = 0x3F8; // Main::ExtendedPhysicsCharacterMoverComponent // 0x03F8

constexpr uintptr_t BP_Main_IsSpectating = 0x9C0; // Main::IsSpectating bool // 0x09C0

constexpr uintptr_t BP_LINK_MyPlayerState = 0xC90; // LINK::MyPlayerState_LINK ABP_FirstPersonPlayerState_LINK_C* // 0x0C90
constexpr uintptr_t BP_LINK_BodyMaterial  = 0xC98; // LINK::BodyMaterial UMaterialInstanceDynamic* (stealth lever; not a plain bool) // 0x0C98
constexpr uintptr_t BP_LINK_IsFreeze      = 0xC80; // LINK::IsFreeze bool // 0x0C80
constexpr uintptr_t BP_Main_AbilitySystem = 0x438; // Main::AbilitySystem UAbilitySystemComponent* (gun cooldowns live here, not a scalar) // 0x0438

// ---------------------------------------------------------------------------
//  UE5 Mover plugin (Mover_classes.hpp) — reached via
//  Character pawn + BP_Main_CharacterMotionComponent
// ---------------------------------------------------------------------------
constexpr uintptr_t Mover_GravityAccelOverride   = 0x438; // UMoverComponent::GravityAccelOverride FVector (inline) // 0x0438
constexpr uintptr_t Mover_bHasGravityOverride    = 0x489; // UMoverComponent::bHasGravityOverride bool (inline)     // 0x0489
constexpr uintptr_t Mover_SharedSettingsArray    = 0x428; // UMoverComponent::SharedSettings TArray<UObject*>       // 0x0428
// fields inside a UCommonLegacyMovementSettings element of SharedSettings:
constexpr uintptr_t MoverSettings_MaxSpeed         = 0x58; // UCommonLegacyMovementSettings::MaxSpeed float         // 0x0058
constexpr uintptr_t MoverSettings_Acceleration     = 0x74; // UCommonLegacyMovementSettings::Acceleration float     // 0x0074
constexpr uintptr_t MoverSettings_JumpUpwardsSpeed = 0x84; // UCommonLegacyMovementSettings::JumpUpwardsSpeed float // 0x0084

// ---------------------------------------------------------------------------
//  Game state (ABP_GameState_LINK_C) and player state
// ---------------------------------------------------------------------------
constexpr uintptr_t GameState_KingCharacter    = 0x3B8; // ABP_GameState_LINK_C::KingCharacter ABP_FirstPersonCharacter_Main_C* // 0x03B8
constexpr uintptr_t GameState_CurrentGamePhase = 0x350; // ABP_GameState_LINK_C::CurrentGamePhase EN_LINK_GamePhase            // 0x0350

constexpr uintptr_t BP_FirstPersonPlayerState_TargetCharacter = 0x378; // ABP_FirstPersonPlayerState_C::TargetCharacter // 0x0378

// =============================================================================
//  OBSOLETE — do NOT re-introduce (kept as documentation of removed offsets).
//  These belonged to the old "cLeon" build and now read garbage / out-of-bounds:
//    * BP_Character role/paint/visibility block 0xB78..0xC50
//        (IsPaintViewLock/IsPaintMode/CurrentPaintColor/IsBrushing/CurrentPlayEmote/
//         BodyShadow/IsHunter/IsLiveSelf/BodyVisibility/HideBlock)
//        -> in LINK class these land inside HandTransform_*/HandWeight_* FTransforms.
//    * BP_GameState_cLeon survivor/hunter arrays 0x348 / 0x3A0
//        -> 0x348 = ExitShopWidget*, 0x3A0 = GameMode FName / TaskWidgets TMap.
//    * noclip teleport pawn+0x1A0 (= AActor::Instigator) and pawn+0x1A8 (= Children TArray)
//        -> real RootComponent is AActor_RootComponent (0x1B8); RelativeLocation 0x140.
//    * penetration PlayerController+0x788 -> OUT OF BOUNDS (LINK PC object ends 0x0770).
//    * stealth pawn+0xC40 (= HandWeight_L double), no-cooldown pawn+0xCE8/0xA88 (wrong fields).
// =============================================================================

} // namespace Offsets
