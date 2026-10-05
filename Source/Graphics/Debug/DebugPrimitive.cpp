#include "Pch.h"
#include "Graphics\Debug\DebugPrimitive.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\Shader\Shader.h"

namespace Engine
{
    namespace
    {
        bool isFinite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFinite(const Color& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool isFinite(const Matrix& value) noexcept
        {
            return std::isfinite(value._11) && std::isfinite(value._12)
                && std::isfinite(value._13) && std::isfinite(value._14)
                && std::isfinite(value._21) && std::isfinite(value._22)
                && std::isfinite(value._23) && std::isfinite(value._24)
                && std::isfinite(value._31) && std::isfinite(value._32)
                && std::isfinite(value._33) && std::isfinite(value._34)
                && std::isfinite(value._41) && std::isfinite(value._42)
                && std::isfinite(value._43) && std::isfinite(value._44);
        }

        constexpr std::array DEBUG_INPUT_LAYOUT =
        {
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(DebugPrimitive::Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DebugPrimitive::Vertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
    }

    bool DebugPrimitive::Requests::empty() const noexcept
    {
        return lines.empty() && grids.empty() && spheres.empty() && boxes.empty() && cylinders.empty() && capsules.empty();
    }

    void DebugPrimitive::Requests::clear() noexcept
    {
        lines.clear();
        grids.clear();
        spheres.clear();
        boxes.clear();
        cylinders.clear();
        capsules.clear();
    }

    void DebugPrimitive::Requests::swap(Requests& other) noexcept
    {
        lines.swap(other.lines);
        grids.swap(other.grids);
        spheres.swap(other.spheres);
        boxes.swap(other.boxes);
        cylinders.swap(other.cylinders);
        capsules.swap(other.capsules);
    }

    DebugPrimitive& DebugPrimitive::instance() noexcept
    {
        static DebugPrimitive instance;
        return instance;
    }

    DebugPrimitive::DebugPrimitive()
    {
        m_pending.lines.reserve(2048);
        m_pending.grids.reserve(16);
        m_pending.spheres.reserve(128);
        m_pending.boxes.reserve(128);
        m_pending.cylinders.reserve(128);
        m_pending.capsules.reserve(128);
        m_vertices.reserve(INITIAL_VERTEX_CAPACITY);
    }

    void DebugPrimitive::drawLine(const Vector3& start, const Vector3& end, const Color& color)
    {
        if (!isFinite(start) || !isFinite(end) || !isFinite(color))
        {
            LOG_WARNING("[DebugPrimitive] Line request contains non-finite values.");
            return;
        }

        const std::scoped_lock lock(m_mutex);
        m_pending.lines.push_back({ start, end, color });
    }

    void DebugPrimitive::drawGrid(const Vector3& center, const float width, const float depth, float step, const Color& color)
    {
        if (!isFinite(center) || !isFinite(color)
            || !std::isfinite(width) || !std::isfinite(depth) || !std::isfinite(step)
            || width <= 0.0f || depth <= 0.0f)
        {
            LOG_WARNING("[DebugPrimitive] Grid request has invalid dimensions or position.");
            return;
        }
        if (step <= 0.0f)
            step = 1.0f;

        const double maxIntervals = static_cast<double>((std::numeric_limits<std::size_t>::max)() - 1);
        if (static_cast<double>(width) / step >= maxIntervals
            || static_cast<double>(depth) / step >= maxIntervals)
        {
            LOG_WARNING("[DebugPrimitive] Grid request exceeds the supported line count.");
            return;
        }

        const std::scoped_lock lock(m_mutex);
        m_pending.grids.push_back({ center, width, depth, step, color });
    }

    void DebugPrimitive::drawSphere(const Matrix& world, const float radius, const Color& color)
    {
        if (!isFinite(world) || !isFinite(color) || !std::isfinite(radius) || radius <= 0.0f)
        {
            LOG_WARNING("[DebugPrimitive] Sphere request contains invalid values.");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        m_pending.spheres.push_back({ world, radius, color });
    }

    void DebugPrimitive::drawBox(const Matrix& world, const Vector3& extents, const Color& color)
    {
        if (!isFinite(world) || !isFinite(extents) || !isFinite(color)
            || extents.x <= 0.0f || extents.y <= 0.0f || extents.z <= 0.0f)
        {
            LOG_WARNING("[DebugPrimitive] Box request contains invalid values.");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        m_pending.boxes.push_back({ world, extents, color });
    }

    void DebugPrimitive::drawCylinder(const Matrix& world, const float radius, const float height, const Color& color)
    {
        if (!isFinite(world) || !isFinite(color) || !std::isfinite(radius) || !std::isfinite(height)
            || radius <= 0.0f || height <= 0.0f)
        {
            LOG_WARNING("[DebugPrimitive] Cylinder request contains invalid values.");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        m_pending.cylinders.push_back({ world, radius, height, color });
    }

    void DebugPrimitive::drawCapsule(const Matrix& world, const float radius, const float halfHeight, const Color& color)
    {
        if (!isFinite(world) || !isFinite(color) || !std::isfinite(radius) || !std::isfinite(halfHeight)
            || radius <= 0.0f || halfHeight < 0.0f)
        {
            LOG_WARNING("[DebugPrimitive] Capsule request contains invalid values.");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        m_pending.capsules.push_back({ world, radius, halfHeight, color });
    }

    void DebugPrimitive::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.clear();
    }

    bool DebugPrimitive::initialize(DX12Device& device, const DX12Fence& fence)
    {
        if (m_device != nullptr)
            return false;
        m_device = &device;
        m_fence = &fence;
        m_vertexCapacities.fill(0);
        m_frameUsed.fill(false);
        return true;
    }

    bool DebugPrimitive::finalize()
    {
        bool succeeded = true;
        for (DX12UploadBuffer& buffer : m_vertexBuffers)
            succeeded = buffer.finalize() && succeeded;
        if (!succeeded)
        {
            LOG_ERROR("[DebugPrimitive] GPU使用完了を確認できないためFinalizeを中止します");
            return false;
        }

        m_pipeline.finalize();
        clear();
        m_renderRequests.clear();
        m_vertices.clear();
        m_vertexCapacities.fill(0);
        m_frameUsed.fill(false);
        m_device = nullptr;
        m_fence = nullptr;
        return true;
    }

    bool DebugPrimitive::rebuildPipeline(const DX12Shader& vertexShader, const DX12Shader& pixelShader)
    {
        if (m_device == nullptr)
            return false;

        D3D12_ROOT_PARAMETER viewProjectionConstants{};
        viewProjectionConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        viewProjectionConstants.Constants.ShaderRegister = 0;
        viewProjectionConstants.Constants.Num32BitValues = 16;
        viewProjectionConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        const std::array rootParameters = { viewProjectionConstants };
        const DX12GraphicsPipelineConfig config{
            .vertexShader = &vertexShader,
            .pixelShader = &pixelShader,
            .inputLayout = DEBUG_INPUT_LAYOUT,
            .rootParameters = rootParameters,
            .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
            .primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
            .enableAlphaBlend = true,
            .enableDepthWrite = false,
        };
        DX12GraphicsPipeline replacement;
        if (!replacement.initialize(*m_device->get(), config))
            return false;

        m_pipeline.swap(replacement);
        return true;
    }

    bool DebugPrimitive::render(
        DX12CommandList& commandList,
        const std::uint32_t frameIndex,
        const Matrix& viewProjection,
        const bool enabled)
    {
        if (frameIndex >= FRAME_COUNT)
            return false;
        m_frameUsed[frameIndex] = false;
        consumeRequests();
        if (m_renderRequests.empty())
            return true;
        if (!enabled)
        {
            m_renderRequests.clear();
            return true;
        }

        if (!buildVertices())
            return false;
        m_renderRequests.clear();
        if (m_vertices.empty())
            return true;
        if (!ensureFrameCapacity(frameIndex, m_vertices.size()))
            return false;

        const std::size_t byteSize = m_vertices.size() * sizeof(Vertex);
        if (byteSize > (std::numeric_limits<UINT>::max)()
            || m_vertices.size() > (std::numeric_limits<UINT>::max)())
        {
            LOG_ERROR("[DebugPrimitive] Vertex data exceeds DirectX 12 draw limits.");
            return false;
        }

        const std::span bytes(reinterpret_cast<const std::byte*>(m_vertices.data()), byteSize);
        DX12UploadBuffer& buffer = m_vertexBuffers[frameIndex];
        if (!buffer.write(bytes) || !m_pipeline.bind(commandList))
            return false;

        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        if (nativeCommandList == nullptr)
            return false;
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView{
            .BufferLocation = buffer.getGpuVirtualAddress(),
            .SizeInBytes = static_cast<UINT>(byteSize),
            .StrideInBytes = sizeof(Vertex),
        };
        nativeCommandList->SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
        nativeCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        nativeCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        nativeCommandList->DrawInstanced(static_cast<UINT>(m_vertices.size()), 1, 0, 0);
        m_frameUsed[frameIndex] = true;
        return true;
    }

    bool DebugPrimitive::markFrameUsed(const std::uint32_t frameIndex, const std::uint64_t fenceValue)
    {
        if (frameIndex >= FRAME_COUNT)
            return false;
        return !m_frameUsed[frameIndex] || m_vertexBuffers[frameIndex].markUsed(fenceValue);
    }

    void DebugPrimitive::consumeRequests()
    {
        const std::scoped_lock lock(m_mutex);
        m_renderRequests.clear();
        m_renderRequests.swap(m_pending);
    }

    bool DebugPrimitive::buildVertices()
    {
        m_vertices.clear();
        constexpr std::size_t maxVertexCount = (std::numeric_limits<UINT>::max)();
        std::size_t estimate = 0;
        const auto addVertices = [&estimate](const std::size_t count, const std::size_t verticesPerRequest)
            {
                constexpr std::size_t maximum = (std::numeric_limits<UINT>::max)();
                if (count > (maximum - estimate) / verticesPerRequest)
                    return false;
                estimate += count * verticesPerRequest;
                return true;
            };
        if (!addVertices(m_renderRequests.lines.size(), 2)
            || !addVertices(m_renderRequests.spheres.size(), CURVE_SEGMENTS * 6)
            || !addVertices(m_renderRequests.boxes.size(), 24)
            || !addVertices(m_renderRequests.cylinders.size(), CURVE_SEGMENTS * 4 + 8)
            || !addVertices(m_renderRequests.capsules.size(), CURVE_SEGMENTS * 12))
        {
            LOG_ERROR("[DebugPrimitive] Requested vertex count exceeds DirectX 12 draw limits.");
            return false;
        }

        for (const GridRequest& grid : m_renderRequests.grids)
        {
            const double xLineCount = std::floor(static_cast<double>(grid.width) / static_cast<double>(grid.step)) + 1.0;
            const double zLineCount = std::floor(static_cast<double>(grid.depth) / static_cast<double>(grid.step)) + 1.0;
            if (!std::isfinite(xLineCount) || !std::isfinite(zLineCount)
                || xLineCount < 1.0 || zLineCount < 1.0
                || xLineCount > static_cast<double>(maxVertexCount / 2)
                || zLineCount > static_cast<double>(maxVertexCount / 2)
                || xLineCount + zLineCount > static_cast<double>((maxVertexCount - estimate) / 2))
            {
                LOG_ERROR("[DebugPrimitive] Grid vertex count exceeds DirectX 12 draw limits.");
                return false;
            }

            estimate += (static_cast<std::size_t>(xLineCount) + static_cast<std::size_t>(zLineCount)) * 2;
        }

        try
        {
            m_vertices.reserve(estimate);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[DebugPrimitive] Failed to allocate memory for debug vertices.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[DebugPrimitive] Debug vertex request exceeds container capacity.");
            return false;
        }

        for (const LineRequest& line : m_renderRequests.lines)
            appendLine(line.start, line.end, line.color);

        for (const GridRequest& grid : m_renderRequests.grids)
        {
            const float halfWidth = grid.width * 0.5f;
            const float halfDepth = grid.depth * 0.5f;
            const std::size_t xLineCount = static_cast<std::size_t>(
                std::floor(static_cast<double>(grid.width) / static_cast<double>(grid.step))) + 1;
            const std::size_t zLineCount = static_cast<std::size_t>(
                std::floor(static_cast<double>(grid.depth) / static_cast<double>(grid.step))) + 1;
            for (std::size_t index = 0; index < xLineCount; ++index)
            {
                const float x = grid.center.x - halfWidth + static_cast<float>(index) * grid.step;
                appendLine({ x, grid.center.y, grid.center.z - halfDepth },
                    { x, grid.center.y, grid.center.z + halfDepth }, grid.color);
            }
            for (std::size_t index = 0; index < zLineCount; ++index)
            {
                const float z = grid.center.z - halfDepth + static_cast<float>(index) * grid.step;
                appendLine({ grid.center.x - halfWidth, grid.center.y, z },
                    { grid.center.x + halfWidth, grid.center.y, z }, grid.color);
            }
        }

        for (const SphereRequest& sphere : m_renderRequests.spheres)
        {
            for (std::uint32_t axis = 0; axis < 3; ++axis)
            {
                for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS; ++segment)
                {
                    const float angle0 = DirectX::XM_2PI * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS);
                    const float angle1 = DirectX::XM_2PI * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS);
                    Vector3 start;
                    Vector3 end;
                    if (axis == 0)
                    {
                        start = { 0.0f, std::sin(angle0), std::cos(angle0) };
                        end = { 0.0f, std::sin(angle1), std::cos(angle1) };
                    }
                    else if (axis == 1)
                    {
                        start = { std::sin(angle0), 0.0f, std::cos(angle0) };
                        end = { std::sin(angle1), 0.0f, std::cos(angle1) };
                    }
                    else
                    {
                        start = { std::sin(angle0), std::cos(angle0), 0.0f };
                        end = { std::sin(angle1), std::cos(angle1), 0.0f };
                    }
                    appendTransformedLine(start * sphere.radius, end * sphere.radius, sphere.world, sphere.color);
                }
            }
        }

        constexpr std::array<std::array<std::uint32_t, 2>, 12> boxEdges = { {
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        } };
        for (const BoxRequest& box : m_renderRequests.boxes)
        {
            const std::array corners = {
                Vector3(-box.extents.x, box.extents.y, -box.extents.z), Vector3(box.extents.x, box.extents.y, -box.extents.z),
                Vector3(box.extents.x, box.extents.y, box.extents.z), Vector3(-box.extents.x, box.extents.y, box.extents.z),
                Vector3(-box.extents.x, -box.extents.y, -box.extents.z), Vector3(box.extents.x, -box.extents.y, -box.extents.z),
                Vector3(box.extents.x, -box.extents.y, box.extents.z), Vector3(-box.extents.x, -box.extents.y, box.extents.z),
            };
            for (const auto& edge : boxEdges)
                appendTransformedLine(corners[edge[0]], corners[edge[1]], box.world, box.color);
        }

        for (const CylinderRequest& cylinder : m_renderRequests.cylinders)
        {
            const float halfHeight = cylinder.height * 0.5f;
            appendCircle(cylinder.world, cylinder.radius, -halfHeight, cylinder.color);
            appendCircle(cylinder.world, cylinder.radius, halfHeight, cylinder.color);
            for (std::uint32_t side = 0; side < 4; ++side)
            {
                const float angle = DirectX::XM_PIDIV2 * static_cast<float>(side);
                const Vector3 radial(std::sin(angle) * cylinder.radius, 0.0f, std::cos(angle) * cylinder.radius);
                appendTransformedLine(radial + Vector3(0.0f, -halfHeight, 0.0f),
                    radial + Vector3(0.0f, halfHeight, 0.0f), cylinder.world, cylinder.color);
            }
        }

        for (const CapsuleRequest& capsule : m_renderRequests.capsules)
        {
            appendCircle(capsule.world, capsule.radius, -capsule.halfHeight, capsule.color);
            appendCircle(capsule.world, capsule.radius, capsule.halfHeight, capsule.color);
            for (std::uint32_t side = 0; side < 4; ++side)
            {
                const float azimuth = DirectX::XM_PIDIV2 * static_cast<float>(side);
                const Vector3 radial(std::sin(azimuth) * capsule.radius, 0.0f, std::cos(azimuth) * capsule.radius);
                appendTransformedLine(radial + Vector3(0.0f, -capsule.halfHeight, 0.0f),
                    radial + Vector3(0.0f, capsule.halfHeight, 0.0f), capsule.world, capsule.color);
                for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS / 4; ++segment)
                {
                    const float angle0 = DirectX::XM_PIDIV2 * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS / 4);
                    const float angle1 = DirectX::XM_PIDIV2 * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS / 4);
                    const Vector3 horizontal(std::sin(azimuth), 0.0f, std::cos(azimuth));
                    const Vector3 top0 = horizontal * (std::cos(angle0) * capsule.radius)
                        + Vector3(0.0f, capsule.halfHeight + std::sin(angle0) * capsule.radius, 0.0f);
                    const Vector3 top1 = horizontal * (std::cos(angle1) * capsule.radius)
                        + Vector3(0.0f, capsule.halfHeight + std::sin(angle1) * capsule.radius, 0.0f);
                    const Vector3 bottom0 = horizontal * (std::cos(angle0) * capsule.radius)
                        + Vector3(0.0f, -capsule.halfHeight - std::sin(angle0) * capsule.radius, 0.0f);
                    const Vector3 bottom1 = horizontal * (std::cos(angle1) * capsule.radius)
                        + Vector3(0.0f, -capsule.halfHeight - std::sin(angle1) * capsule.radius, 0.0f);
                    appendTransformedLine(top0, top1, capsule.world, capsule.color);
                    appendTransformedLine(bottom0, bottom1, capsule.world, capsule.color);
                }
            }
        }
        if (m_vertices.size() > maxVertexCount)
        {
            LOG_ERROR("[DebugPrimitive] Generated vertex count exceeds DirectX 12 draw limits.");
            return false;
        }
        return true;
    }

    bool DebugPrimitive::ensureFrameCapacity(const std::uint32_t frameIndex, const std::size_t vertexCount)
    {
        if (vertexCount <= m_vertexCapacities[frameIndex])
            return true;
        if (m_device == nullptr || m_fence == nullptr)
            return false;

        const std::size_t capacity = nextPowerOfTwo(std::max(vertexCount, INITIAL_VERTEX_CAPACITY));
        if (capacity == 0 || capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(Vertex))
        {
            LOG_ERROR("[DebugPrimitive] Vertex buffer capacity is too large.");
            return false;
        }

        DX12UploadBuffer& buffer = m_vertexBuffers[frameIndex];
        if (!buffer.finalize()
            || !buffer.initialize(*m_device->get(), *m_fence, capacity * sizeof(Vertex)))
        {
            return false;
        }
        m_vertexCapacities[frameIndex] = capacity;
        return true;
    }

    void DebugPrimitive::appendLine(const Vector3& start, const Vector3& end, const Color& color)
    {
        m_vertices.push_back({ start, color });
        m_vertices.push_back({ end, color });
    }

    void DebugPrimitive::appendTransformedLine(
        const Vector3& start, const Vector3& end, const Matrix& world, const Color& color)
    {
        appendLine(Vector3::Transform(start, world), Vector3::Transform(end, world), color);
    }

    void DebugPrimitive::appendCircle(const Matrix& world, const float radius, const float y, const Color& color)
    {
        for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS; ++segment)
        {
            const float angle0 = DirectX::XM_2PI * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS);
            const float angle1 = DirectX::XM_2PI * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS);
            appendTransformedLine(
                { std::sin(angle0) * radius, y, std::cos(angle0) * radius },
                { std::sin(angle1) * radius, y, std::cos(angle1) * radius }, world, color);
        }
    }
} // namespace Engine