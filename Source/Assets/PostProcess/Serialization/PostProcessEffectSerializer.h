#pragma once

#include "Assets\PostProcess\PostProcessEffectAsset.h"
#include <filesystem>

namespace Engine::Serialization
{
    class PostProcessEffectSerializer
    {
    public:
        bool save(const std::filesystem::path& path, const Engine::PostProcessEffectAsset& asset) const;
        bool load(const std::filesystem::path& path, Engine::PostProcessEffectAsset& asset) const;
    };

    struct PostProcessEffectReference
    {
        ObjectGUID guid;
        std::string path;
    };

    bool writePostProcessEffectReference(PostProcessEffectHandle handle, PostProcessEffectReference& destination);
    bool readPostProcessEffectReference(const PostProcessEffectReference& reference, PostProcessEffectHandle& destination);
}
