#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Engine
{
    /**
     * @brief Normalized, row-major height samples. Row zero maps to local -Z.
     * @thread_safety Read-only access is thread-safe. Mutations require external synchronization.
     */
    class Heightmap
    {
    public:
        static constexpr std::uint32_t MAX_DIMENSION = 1025;

        /** @brief Creates a valid 2 by 2 flat heightmap. */
        Heightmap() = default;

        /**
         * @brief Replaces samples after validating dimensions and finite values in [0, 1].
         * @param width Number of X samples, in [2, MAX_DIMENSION].
         * @param height Number of Z samples, in [2, MAX_DIMENSION].
         * @param samples Exactly width * height samples.
         * @return True on success. Failure preserves the previous heightmap and is logged.
         */
        bool assign(std::uint32_t width, std::uint32_t height, std::span<const float> samples);

        /**
         * @brief Replaces the heightmap with a flat grid.
         * @param width Number of X samples.
         * @param height Number of Z samples.
         * @param value Normalized height in [0, 1].
         * @return True on success. Failure preserves the previous heightmap and is logged.
         */
        bool createFlat(std::uint32_t width, std::uint32_t height, float value = 0.0f);

        /**
         * @brief Loads a WIC image without gamma conversion, using its red channel.
         * @details Preserves 8/16-bit grayscale precision. Image dimensions become grid dimensions.
         * @param path PNG, TIFF, BMP or another installed WIC-supported image.
         * @return True on success. Failure preserves the previous heightmap and is logged.
         */
        bool loadImage(const std::filesystem::path& path);

        /** @brief Gets the number of X samples. */
        std::uint32_t width() const noexcept { return m_width; }
        /** @brief Gets the number of Z samples. */
        std::uint32_t height() const noexcept { return m_height; }
        /** @brief Gets immutable normalized samples, indexed by z * width() + x. */
        std::span<const float> samples() const noexcept { return m_samples; }

    private:
        std::uint32_t m_width = 2;
        std::uint32_t m_height = 2;
        std::vector<float> m_samples = std::vector<float>(4, 0.0f);
    };
}
