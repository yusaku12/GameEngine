#include "Pch.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace Engine
{
    void RenderQueue::clear() noexcept
    {
        m_items.clear();
        m_statistics = {};
    }

    void RenderQueue::reserve(const std::size_t capacity)
    {
        m_items.reserve(capacity);
    }

    bool RenderQueue::submit(const RenderItem& item, const Frustum* const frustum)
    {
        if (item.vertexBuffer == nullptr || item.indexBuffer == nullptr || item.indexCount == 0)
            return false;

        if (frustum != nullptr && !isVisible(*frustum, item.worldBounds))
        {
            ++m_statistics.culledItems;
            return false;
        }

        m_items.push_back(item);
        ++m_statistics.visibleItems;
        return true;
    }

    void RenderQueue::sort()
    {
        std::sort(m_items.begin(), m_items.end(), [](const RenderItem& left, const RenderItem& right)
            {
                if (left.pass != right.pass)
                    return left.pass < right.pass;
                if (left.pass == RenderPassType::Transparent)
                    return left.cameraDepth > right.cameraDepth;
                return createSortKey(left) < createSortKey(right);
            });
    }

    std::uint64_t RenderQueue::createSortKey(const RenderItem& item) noexcept
    {
        constexpr std::uint64_t FIELD_MASK = 0xFFF;
        return (static_cast<std::uint64_t>(item.pass) << 60)
            | ((static_cast<std::uint64_t>(item.pipelineID) & FIELD_MASK) << 48)
            | ((static_cast<std::uint64_t>(item.materialID) & FIELD_MASK) << 36)
            | ((static_cast<std::uint64_t>(item.textureID) & FIELD_MASK) << 24)
            | ((static_cast<std::uint64_t>(item.meshID) & FIELD_MASK) << 12)
            | (static_cast<std::uint64_t>(item.objectID) & FIELD_MASK);
    }
} // namespace Engine