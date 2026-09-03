#pragma once

#include <cstdint>

namespace Engine::Serialization
{
    inline constexpr std::uint32_t CURRENT_SCHEMA_VERSION = 1;          //!< 現在のスキーマバージョン
    inline constexpr std::uint32_t CURRENT_MODEL_VERSION = 1;           //!< 現在のモデルバージョン
    inline constexpr std::uint32_t MINIMUM_SUPPORTED_MODEL_VERSION = 1; //!< サポートされている最小のモデルバージョン
}