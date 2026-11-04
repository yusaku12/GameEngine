#pragma once

#include "Graphics\PostProcess\PostProcessSettings.h"
#include <filesystem>

namespace Engine
{
    /** @brief Exercises Profile/Camera authoring persistence and returns an enabled, nonneutral GPU test snapshot. */
    bool runPostProcessAuthoringValidation(const std::filesystem::path& directory,
        PostProcessCameraSnapshot& snapshot);
}
