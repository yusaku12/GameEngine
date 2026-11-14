#pragma once

#include "Graphics\PostProcess\PostProcessPipeline.h"
#include <filesystem>
#include <string>

namespace Engine
{
    /** @brief Editor recipe and compiled GPU resources stored in a self-contained OCIO asset. */
    struct PostProcessOcioAsset
    {
        std::filesystem::path assetPath; //!< Saved artifact path; empty before the first save.
        std::string configPath; //!< UTF-8 path to the authoring config.
        std::string sourceSpace; //!< Input color space.
        std::string destinationSpace; //!< Output color space.
        std::string look; //!< Optional OCIO look expression.
        PostProcessOcioDescription description; //!< Compiled shader and LUT snapshot.
    };

    /**
     * @brief Save an OCIO artifact atomically, including shader bytecode and LUTs.
     * @return True when the validated artifact was saved.
     */
    bool savePostProcessOcioAsset(const std::filesystem::path& path, const PostProcessOcioAsset& asset);

    /**
     * @brief Load a precompiled OCIO artifact without accessing the config or invoking DXC.
     * @return True on success; destination is unchanged on failure.
     */
    bool loadPostProcessOcioAsset(const std::filesystem::path& path, PostProcessOcioAsset& destination);
}
