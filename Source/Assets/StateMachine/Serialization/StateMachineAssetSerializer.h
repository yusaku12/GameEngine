#pragma once

#include "Assets\StateMachine\StateMachineAsset.h"
#include <filesystem>

namespace Engine::Serialization
{
    /** @brief Versioned FlatBuffers persistence for generic State Machine assets. */
    class StateMachineAssetSerializer
    {
    public:
        /** @brief Validates and atomically saves an asset. */
        bool save(const std::filesystem::path& path, const StateMachineAsset& asset) const;
        /** @brief Verifies, validates, and replaces the destination only on success. */
        bool load(const std::filesystem::path& path, StateMachineAsset& asset) const;
    };
}
