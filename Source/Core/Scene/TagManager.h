#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{
    using TagID = std::uint32_t;

    /**
     * @brief Tag名とTagIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class TagManager
    {
    public:
        static TagManager& instance() noexcept;

        TagID registerTag(std::string name);
        [[nodiscard]] TagID find(std::string_view name) const noexcept;
        [[nodiscard]] std::string_view getName(TagID tag) const noexcept;
        [[nodiscard]] bool contains(TagID tag) const noexcept;

    private:
        TagManager();
        std::vector<std::string> m_names;
        std::unordered_map<std::string, TagID> m_ids;
    };
} // namespace Engine