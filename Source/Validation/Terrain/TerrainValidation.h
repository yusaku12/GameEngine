#pragma once

#include <filesystem>

namespace Engine
{
    class DX12Renderer;

    /**
     * @brief Validates height import, mesh topology, persistence, lifecycle and GPU rendering.
     * @param renderer Initialized UI-free Runtime renderer; shut down after validation.
     * @param directory New or empty output directory for generated fixtures, captures and results.
     * @return True only when every check and output succeeds.
     * @thread_safety Render thread only, with scene updates stopped.
     */
    bool runTerrainValidation(DX12Renderer& renderer, const std::filesystem::path& directory);
}
