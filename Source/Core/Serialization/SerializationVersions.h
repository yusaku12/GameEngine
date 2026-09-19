#pragma once

#include <cstdint>

namespace Engine::Serialization
{
    inline constexpr std::uint32_t CURRENT_SCHEMA_VERSION = 1;             //!< 現在のスキーマバージョン
    inline constexpr std::uint32_t CURRENT_MODEL_VERSION = 3;              //!< 現在のモデルバージョン
    inline constexpr std::uint32_t MINIMUM_SUPPORTED_MODEL_VERSION = 1;    //!< サポートされている最小のモデルバージョン
    inline constexpr std::uint32_t CURRENT_SKELETON_VERSION = 1;           //!< 現在のSkeletonバージョン
    inline constexpr std::uint32_t CURRENT_ANIMATION_CLIP_VERSION = 1;     //!< 現在のAnimation Clipバージョン
    inline constexpr std::uint32_t CURRENT_ANIMATOR_CONTROLLER_VERSION = 1; //!< 現在のAnimator Controllerバージョン
    inline constexpr std::uint32_t CURRENT_MATERIAL_VERSION = 3;           //!< 現在のMaterialバージョン
    inline constexpr std::uint32_t MINIMUM_SUPPORTED_MATERIAL_VERSION = 1; //!< サポートされている最小のMaterialバージョン
    inline constexpr std::uint32_t CURRENT_PREFAB_VERSION = 1;             //!< 現在のPrefabバージョン
    inline constexpr std::uint32_t CURRENT_SCENE_VERSION = 1;              //!< 現在のSceneバージョン
}