#include "Pch.h"
#include <tuple>
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
                {
                    if (left.cameraDepth != right.cameraDepth)
                        return left.cameraDepth > right.cameraDepth;
                }
                return std::tie(left.pipelineID, left.shaderVariantID, left.material.index, left.material.generation,
                    left.textureID, left.meshID, left.objectID)
                    < std::tie(right.pipelineID, right.shaderVariantID, right.material.index, right.material.generation,
                        right.textureID, right.meshID, right.objectID);
            });
    }
} // namespace Engine