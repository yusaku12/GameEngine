#pragma once

#include "Assets\PostProcess\PostProcessProfile.h"
#include "Generated\FlatBuffers\PostProcessSettings_generated.h"
#include <filesystem>

namespace Engine::Serialization
{
    /** @brief Shared saved asset reference for Camera and Volume payloads. */
    struct PostProcessProfileReference
    {
        ObjectGUID guid;
        std::string path;
    };
    /** @brief Gets a saved Profile reference; rejects stale/unsaved handles with a diagnostic. */
    bool writePostProcessProfileReference(PostProcessProfileHandle handle, PostProcessProfileReference& destination);
    /** @brief Validates GUID/UTF-8 path pairing and loads the exact asset; failure preserves the handle. */
    bool readPostProcessProfileReference(const PostProcessProfileReference& reference, PostProcessProfileHandle& destination);
    /** @brief Shared FlatBuffers conversion for Profile files and Camera payloads. */
    flatbuffers::Offset<PostProcessSettingsData> writePostProcessSettings(
        flatbuffers::FlatBufferBuilder& builder, const Engine::PostProcessSettings& settings);
    /** @brief Reads all required values and validates ranges; failure preserves destination. */
    bool readPostProcessSettings(const PostProcessSettingsData* source, Engine::PostProcessSettings& settings);

    /** @brief Versioned Profile persistence using the engine atomic writer. No global registry changes. */
    class PostProcessProfileSerializer
    {
    public:
        /** @brief Saves a valid GUID/name/settings atomically. Failure logs and returns false. */
        bool save(const std::filesystem::path& path, const Engine::PostProcessProfile& profile) const;
        /** @brief Verifies identifier/version/settings; only replaces destination on success. */
        bool load(const std::filesystem::path& path, Engine::PostProcessProfile& profile) const;
    };
}
