#include "Pch.h"
#include <tuple>
#include "Graphics\Renderer\RenderQueue.h"

namespace Engine
{
    namespace
    {
        bool isValidBounds(const AABB& bounds) noexcept
        {
            return std::isfinite(bounds.Center.x) && std::isfinite(bounds.Center.y)
                && std::isfinite(bounds.Center.z) && std::isfinite(bounds.Extents.x)
                && std::isfinite(bounds.Extents.y) && std::isfinite(bounds.Extents.z)
                && bounds.Extents.x >= 0.0f && bounds.Extents.y >= 0.0f && bounds.Extents.z >= 0.0f;
        }
    }

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
        if (!isValidBounds(item.worldBounds) || !std::isfinite(item.cameraDepth)
            || item.pass > RenderPassType::Transparent)
        {
            LOG_ERROR("[RenderQueue] Rejecting item with invalid bounds, depth, or render pass.");
            return false;
        }

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
                    return std::tie(left.pipelineID, left.shaderVariantID, left.material.index, left.material.generation,
                        left.textureID, left.meshID, left.objectID)
                        < std::tie(right.pipelineID, right.shaderVariantID, right.material.index, right.material.generation,
                            right.textureID, right.meshID, right.objectID);
                }
                const auto leftState = std::tie(left.pipelineID, left.shaderVariantID, left.material.index,
                    left.material.generation, left.textureID);
                const auto rightState = std::tie(right.pipelineID, right.shaderVariantID, right.material.index,
                    right.material.generation, right.textureID);
                if (leftState != rightState)
                    return leftState < rightState;
                if (left.vertexBuffer != right.vertexBuffer)
                    return std::less<const D3D12_VERTEX_BUFFER_VIEW*>{}(left.vertexBuffer, right.vertexBuffer);
                if (left.indexBuffer != right.indexBuffer)
                    return std::less<const D3D12_INDEX_BUFFER_VIEW*>{}(left.indexBuffer, right.indexBuffer);
                return std::tie(left.indexStart, left.indexCount, left.baseVertex, left.meshID, left.objectID)
                    < std::tie(right.indexStart, right.indexCount, right.baseVertex, right.meshID, right.objectID);
            });
    }

    std::size_t RenderQueue::getInstanceBatchSize(const std::size_t first) const noexcept
    {
        if (first >= m_items.size())
            return 0;

        const RenderItem& item = m_items[first];
        if (item.pass == RenderPassType::Transparent || item.skinningPalette != nullptr
            || !item.materialProperties.empty())
            return 1;

        std::size_t end = first + 1;
        for (; end < m_items.size(); ++end)
        {
            const RenderItem& candidate = m_items[end];
            if (candidate.pass != item.pass || candidate.pipelineID != item.pipelineID
                || candidate.shaderVariantID != item.shaderVariantID || candidate.material != item.material
                || candidate.surfaceType != item.surfaceType || candidate.textureID != item.textureID
                || candidate.vertexBuffer != item.vertexBuffer || candidate.indexBuffer != item.indexBuffer
                || candidate.indexStart != item.indexStart || candidate.indexCount != item.indexCount
                || candidate.baseVertex != item.baseVertex || candidate.bonePaletteBuffer != item.bonePaletteBuffer
                || candidate.skinningPalette != nullptr || !candidate.materialProperties.empty())
                break;
        }
        return end - first;
    }
} // namespace Engine