#pragma once

#include <filesystem>
#include <cwchar>
#include "Core\Logging\Logging.h"

namespace Engine::Serialization
{
    /**
     * @brief Resolve a reference relative to its owning asset, or the working directory if no owner is given.
     * @param reference Absolute or owner-relative reference. Empty references remain empty.
     * @param owner Path of the owning asset file.
     * @param resolved Receives the absolute, lexically normalized path on success.
     * @return False if the path is ambiguous or cannot be resolved.
     */
    inline bool resolveAssetReference(const std::filesystem::path& reference,
        const std::filesystem::path& owner, std::filesystem::path& resolved)
    {
        if (reference.empty())
        {
            resolved.clear();
            return true;
        }
        if ((!reference.is_absolute() && reference.has_root_path())
            || (!owner.empty() && !owner.is_absolute() && owner.has_root_path()))
        {
            LOG_ERROR("Ambiguous asset reference: {}", reference.string());
            return false;
        }
        std::error_code error;
        const std::filesystem::path candidate = reference.is_absolute() || owner.empty()
            ? reference : owner.parent_path() / reference;
        const std::filesystem::path absolute = std::filesystem::absolute(candidate, error);
        if (error)
        {
            LOG_ERROR("Cannot resolve asset reference: {} ({})", candidate.string(), error.message());
            return false;
        }
        resolved = absolute.lexically_normal();
        return true;
    }

    /**
     * @brief Convert an asset reference to a path relative to the destination asset file.
     * @param reference Absolute or source-owner-relative reference.
     * @param sourceOwner Original owning asset file; empty means working-directory-relative input.
     * @param destinationOwner Destination asset file.
     * @param relative Receives the destination-relative reference on success.
     * @return False if a relative reference cannot be represented (including different volumes).
     */
    inline bool relativizeAssetReference(const std::filesystem::path& reference,
        const std::filesystem::path& sourceOwner, const std::filesystem::path& destinationOwner,
        std::filesystem::path& relative)
    {
        if (reference.empty())
        {
            relative.clear();
            return true;
        }
        std::filesystem::path absoluteReference;
        std::filesystem::path absoluteOwner;
        if (destinationOwner.empty()
            || !resolveAssetReference(reference, sourceOwner, absoluteReference)
            || !resolveAssetReference(destinationOwner, {}, absoluteOwner))
        {
            LOG_ERROR("Cannot make asset reference relative to destination: {}", destinationOwner.string());
            return false;
        }
        if (_wcsicmp(absoluteReference.root_name().c_str(), absoluteOwner.root_name().c_str()) == 0)
            absoluteReference = absoluteOwner.root_path() / absoluteReference.relative_path();
        const std::filesystem::path result = absoluteReference.lexically_relative(absoluteOwner.parent_path());
        if (result.empty() || result.has_root_path())
        {
            LOG_ERROR("Asset reference cannot be stored as a relative path: {} -> {}",
                absoluteReference.string(), absoluteOwner.string());
            return false;
        }
        relative = result;
        return true;
    }
}
