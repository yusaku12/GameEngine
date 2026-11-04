#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    class DX12Renderer;
    /**
     * @brief Validates terrain LOD topology, persistence, full-resolution physics and per-view GPU culling.
     * @param renderer Initialized Debug renderer.
     * @param directory Existing validation output directory.
     * @param passed Receives successful check count.
     * @return True only when all checks pass.
     */
    bool validateTerrainLod(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& passed);
}
