#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Engine
{
    using LayerID = std::uint8_t;
    using LayerMask = std::uint32_t;

    /**
     * @brief Layer名とLayerIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class LayerManager
    {
    public:
        static LayerManager& instance() noexcept;

        LayerID registerLayer(std::string name);
        [[nodiscard]] LayerID find(std::string_view name) const noexcept;
        [[nodiscard]] std::string_view getName(LayerID layer) const noexcept;
        [[nodiscard]] bool contains(LayerID layer) const noexcept;

    private:
        LayerManager();
        std::array<std::string, 32> m_names;
        LayerMask m_registeredMask = 0;
    };
} // namespace Engine