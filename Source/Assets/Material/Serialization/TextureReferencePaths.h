#pragma once

#include <array>
#include "Assets\Material\MaterialAsset.h"
#include "Core\Serialization\AssetReferencePaths.h"

namespace Engine::Serialization
{
    inline constexpr std::array MATERIAL_TEXTURE_PATH_MEMBERS = {
        &Engine::MaterialTextureReferences::baseColorPath,
        &Engine::MaterialTextureReferences::normalPath,
        &Engine::MaterialTextureReferences::metallicRoughnessPath,
        &Engine::MaterialTextureReferences::ambientOcclusionPath,
        &Engine::MaterialTextureReferences::emissivePath,
    };

    /**
     * @brief Resolve all material texture hints relative to their owning asset file.
     * @param references Texture hints to resolve in place.
     * @param owner Owning asset file, or empty for working-directory-relative input.
     * @return True when every reference was resolved.
     */
    inline bool resolveTextureReferences(Engine::MaterialTextureReferences& references,
        const std::filesystem::path& owner)
    {
        for (const auto member : MATERIAL_TEXTURE_PATH_MEMBERS)
            if (!resolveAssetReference(references.*member, owner, references.*member))
                return false;
        return true;
    }

    /**
     * @brief Rebase all material texture hints for serialization to a destination asset file.
     * @param references Texture hints to rebase in place.
     * @param sourceOwner Original owning asset file, or empty for working-directory-relative input.
     * @param destinationOwner Destination asset file.
     * @return True when every reference can be represented by a relative path.
     */
    inline bool relativizeTextureReferences(Engine::MaterialTextureReferences& references,
        const std::filesystem::path& sourceOwner, const std::filesystem::path& destinationOwner)
    {
        for (const auto member : MATERIAL_TEXTURE_PATH_MEMBERS)
            if (!relativizeAssetReference(references.*member, sourceOwner, destinationOwner, references.*member))
                return false;
        return true;
    }
}
