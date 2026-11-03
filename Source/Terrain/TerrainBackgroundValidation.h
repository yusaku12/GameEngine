#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    class DX12Renderer;
    /**
     * @brief Validates asynchronous terrain parity, cancellation/lifetime, transactional physics and live frames.
     * @param renderer Initialized Debug renderer.
     * @param directory Existing output directory.
     * @param passed Receives successful check count.
     * @return True only after all checks pass.
     */
    bool validateTerrainBackground(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& passed);
}
