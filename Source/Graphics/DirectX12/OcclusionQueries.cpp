#include "Pch.h"
#include "Graphics\DirectX12\OcclusionQueries.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace Engine
{
    namespace
    {
        bool projectBounds(const AABB& bounds, const Matrix& viewProjection, const Vector2& viewportSize,
            Vector4& rectangle, float& nearestDepth)
        {
            const Vector3 center(bounds.Center);
            const Vector3 extents(bounds.Extents);
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)
                || !std::isfinite(extents.x) || !std::isfinite(extents.y) || !std::isfinite(extents.z)
                || extents.x < 0.0f || extents.y < 0.0f || extents.z < 0.0f
                || extents.x == 0.0f && extents.y == 0.0f && extents.z == 0.0f)
                return false;

            std::array<DirectX::XMFLOAT3, AABB::CORNER_COUNT> corners;
            bounds.GetCorners(corners.data());
            rectangle = Vector4(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            nearestDepth = 1.0f;
            for (const auto& corner : corners)
            {
                const Vector4 clip = Vector4::Transform(Vector4(corner.x, corner.y, corner.z, 1.0f), viewProjection);
                // Near-plane intersections (including camera-inside bounds) must remain unconditionally visible.
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z)
                    || !std::isfinite(clip.w) || clip.w <= 0.00001f || clip.z <= 0.00001f)
                    return false;
                const float x = clip.x / clip.w;
                const float y = clip.y / clip.w;
                const float depth = clip.z / clip.w;
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth))
                    return false;
                rectangle.x = std::min(rectangle.x, x);
                rectangle.y = std::min(rectangle.y, y);
                rectangle.z = std::max(rectangle.z, x);
                rectangle.w = std::max(rectangle.w, y);
                nearestDepth = std::min(nearestDepth, depth);
            }

            // Enclose every covered pixel and bias toward the camera to avoid rounding-induced false occlusion.
            const float paddingX = 4.0f / viewportSize.x;
            const float paddingY = 4.0f / viewportSize.y;
            rectangle.x = std::max(-1.0f, rectangle.x - paddingX);
            rectangle.y = std::max(-1.0f, rectangle.y - paddingY);
            rectangle.z = std::min(1.0f, rectangle.z + paddingX);
            rectangle.w = std::min(1.0f, rectangle.w + paddingY);
            if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y)
                || !std::isfinite(rectangle.z) || !std::isfinite(rectangle.w))
                return false;
            nearestDepth = std::max(0.0f, nearestDepth - 0.00001f);
            return rectangle.x < rectangle.z && rectangle.y < rectangle.w;
        }
    }

    bool DX12OcclusionQueries::collectCompletedResults(std::uint32_t& tested, std::uint32_t& occluded)
    {
        tested = 0;
        occluded = 0;
        if (m_pendingCount != 0)
        {
            m_completedResults.resize(m_pendingCount);
            if (!m_readback.read(std::as_writable_bytes(std::span(m_completedResults))))
                return false;
            tested = m_pendingCount;
            for (const std::uint64_t result : m_completedResults)
            {
                if (result == 0)
                    ++occluded;
            }
        }
        m_pendingCount = 0;
        m_queries.clear();
        m_itemQueries.clear();
        return true;
    }

    bool DX12OcclusionQueries::ensureCapacity(ID3D12Device& device, const DX12Fence& fence,
        const std::uint32_t count)
    {
        if (count <= m_capacity)
            return true;
        if (count > MAX_QUERIES || m_pendingCount != 0)
        {
            LOG_ERROR("[Occlusion] Cannot resize query resources while query results are pending.");
            return false;
        }
        const std::uint32_t doubledCapacity = m_capacity > MAX_QUERIES / 2
            ? MAX_QUERIES : m_capacity * 2;
        const std::uint32_t capacity = std::min(MAX_QUERIES, std::max(count, std::max(256u, doubledCapacity)));
        if (!m_readback.finalize())
            return false;

        m_results.finalize();
        m_heap.Reset();
        m_capacity = 0;

        Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
        const D3D12_QUERY_HEAP_DESC heapDescription{ D3D12_QUERY_HEAP_TYPE_OCCLUSION, capacity, 0 };
        const HRESULT result = device.CreateQueryHeap(&heapDescription, IID_PPV_ARGS(&heap));
        if (FAILED(result))
        {
            LOG_ERROR("[Occlusion] Query heap creation failed (HRESULT: 0x{:08X}).", static_cast<unsigned long>(result));
            return false;
        }
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = static_cast<std::uint64_t>(capacity) * sizeof(std::uint64_t);
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!m_results.initialize(device, {
                .description = description,
            }))
        {
            LOG_ERROR("[Occlusion] Query result buffer creation failed.");
            return false;
        }

        if (!m_readback.initialize(device, fence, description.Width))
        {
            m_results.finalize();
            LOG_ERROR("[Occlusion] Readback buffer creation failed.");
            return false;
        }

        m_heap = std::move(heap);
        m_capacity = capacity;
        return true;
    }

    bool DX12OcclusionQueries::record(DX12CommandList& commandList, ID3D12Device& device, const DX12Fence& fence,
        const DX12GraphicsPipeline& pipeline, const RenderQueue& queue,
        const Matrix& viewProjection, const Vector2& viewportSize, const bool instancing)
    {
        if (!std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y)
            || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f || m_pendingCount != 0)
        {
            LOG_ERROR("[Occlusion] Invalid viewport or frame reused before collecting results.");
            return false;
        }
        const auto items = queue.getItems();
        m_queries.clear();
        m_itemQueries.assign(items.size(), UINT32_MAX);
        for (std::size_t first = 0; first < items.size() && m_queries.size() < MAX_QUERIES;)
        {
            const std::size_t batchSize = instancing ? queue.getInstanceBatchSize(first) : 1;
            const RenderItem& item = items[first];
            if ((item.pass == RenderPassType::Opaque || item.pass == RenderPassType::AlphaTest)
                && static_cast<std::uint64_t>(item.indexCount) * batchSize >= MIN_BATCH_INDICES)
            {
                AABB bounds = item.worldBounds;
                bool canTest = true;
                for (std::size_t offset = 0; offset < batchSize; ++offset)
                {
                    const RenderItem& instance = items[first + offset];
                    if (instance.skinningPalette != nullptr)
                    {
                        canTest = false;
                        break;
                    }
                    // Validate each bound before merging; invalid instance bounds must never be hidden by a valid union.
                    Query projected;
                    if (!projectBounds(instance.worldBounds, viewProjection, viewportSize,
                        projected.rectangle, projected.nearestDepth))
                    {
                        canTest = false;
                        break;
                    }
                    AABB merged;
                    AABB::CreateMerged(merged, bounds, instance.worldBounds);
                    bounds = merged;
                }
                Query query;
                if (canTest && projectBounds(bounds, viewProjection, viewportSize, query.rectangle, query.nearestDepth))
                {
                    m_itemQueries[first] = static_cast<std::uint32_t>(m_queries.size());
                    m_queries.push_back(query);
                }
            }
            first += batchSize;
        }
        if (m_queries.empty())
            return true;
        if (!ensureCapacity(device, fence, getQueryCount()) || !pipeline.bind(commandList))
            return false;
        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        if (native == nullptr || m_results.get() == nullptr || m_heap == nullptr)
        {
            LOG_ERROR("[Occlusion] Query resources or command list are not initialized.");
            return false;
        }
        native->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        native->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (std::uint32_t index = 0; index < getQueryCount(); ++index)
        {
            native->SetGraphicsRoot32BitConstants(0, 5, &m_queries[index], 0);
            native->BeginQuery(m_heap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, index);
            native->DrawInstanced(6, 1, 0, 0);
            native->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, index);
        }
        if (!m_results.transition(commandList, D3D12_RESOURCE_STATE_COPY_DEST))
            return false;
        native->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, getQueryCount(),
            m_results.get(), 0);
        if (!m_results.transition(commandList, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        native->CopyBufferRegion(m_readback.get(), 0, m_results.get(), 0,
            static_cast<std::uint64_t>(getQueryCount()) * sizeof(std::uint64_t));
        return m_results.transition(commandList, D3D12_RESOURCE_STATE_PREDICATION);
    }

    void DX12OcclusionQueries::setPredicate(ID3D12GraphicsCommandList& commandList,
        const std::size_t itemIndex) const noexcept
    {
        const std::uint32_t query = itemIndex < m_itemQueries.size() ? m_itemQueries[itemIndex] : UINT32_MAX;
        if (query != UINT32_MAX)
            commandList.SetPredication(m_results.get(), static_cast<std::uint64_t>(query) * sizeof(std::uint64_t),
                D3D12_PREDICATION_OP_EQUAL_ZERO);
        else
            commandList.SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    }

    bool DX12OcclusionQueries::markSubmitted(const std::uint64_t fenceValue)
    {
        if (m_queries.empty())
            return true;
        if (!m_readback.markPending(fenceValue))
            return false;
        m_pendingCount = getQueryCount();
        return true;
    }

    bool DX12OcclusionQueries::finalize()
    {
        if (!m_readback.finalize())
            return false;
        m_results.finalize();
        m_heap.Reset();
        m_capacity = 0;
        m_pendingCount = 0;
        m_queries.clear();
        m_itemQueries.clear();
        m_completedResults.clear();
        return true;
    }
} // namespace Engine