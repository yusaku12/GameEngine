#include "Pch.h"
#include "Graphics\Debug\DebugPrimitive.h"
#include "Graphics\Debug\DebugPrimitiveCulling.h"
#include "Graphics\DirectX12\Command\Command.h"
#include "Graphics\DirectX12\Device\Device.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\Shader\Shader.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"
#include "Core\Math\MathFunction.h"

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

        bool isFinite(const DebugPrimitive::Line& line) noexcept
        {
            return isFinite(line.start) && isFinite(line.end) && isFinite(line.color);
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
        constexpr std::array SHAPE_INPUT_LAYOUT =
        {
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(DebugPrimitive::ShapeVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "HEIGHTOFFSET", 0, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(DebugPrimitive::ShapeVertex, heightOffset)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCEWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, world)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCEWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, world) + 16), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCEWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, world) + 32), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCEWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, world) + 48), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, color)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "DIMENSIONS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(DebugPrimitive::ShapeInstance, dimensions)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
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
        const Line line{ start, end, color };
        if (!isFinite(line))
        {
            LOG_WARNING("[DebugPrimitive] Line request contains non-finite values.");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        m_pending.lines.push_back(line);
    }

    void DebugPrimitive::drawLines(const std::span<const Line> lines)
    {
        bool allValid = true;
        for (const Line& line : lines)
        {
            if (!isFinite(line))
            {
                LOG_WARNING("[DebugPrimitive] Line request contains non-finite values.");
                allValid = false;
            }
        }
        if (lines.empty())
            return;
        const std::scoped_lock lock(m_mutex);
        if (allValid)
            m_pending.lines.insert(m_pending.lines.end(), lines.begin(), lines.end());
        else
            for (const Line& line : lines)
                if (isFinite(line))
                    m_pending.lines.push_back(line);
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
        m_instanceCapacities.fill(0);
        m_frameUsed.fill(false);
        m_shapeFrameUsed.fill(false);
        if (!initializeShapeVertices())
        {
            static_cast<void>(finalize());
            return false;
        }
        return true;
    }

    bool DebugPrimitive::finalize()
    {
        bool succeeded = true;
        for (DX12UploadBuffer& buffer : m_vertexBuffers)
            succeeded = buffer.finalize() && succeeded;
        for (DX12UploadBuffer& buffer : m_instanceBuffers)
            succeeded = buffer.finalize() && succeeded;
        succeeded = m_shapeVertices.finalize() && succeeded;
        if (!succeeded)
        {
            LOG_ERROR("[DebugPrimitive] GPU使用完了を確認できないためFinalizeを中止します");
            return false;
        }

        m_pipeline.finalize();
        m_shapePipeline.finalize();
        clear();
        m_renderRequests.clear();
        m_vertices.clear();
        m_instances.clear();
        m_vertexCapacities.fill(0);
        m_instanceCapacities.fill(0);
        m_frameUsed.fill(false);
        m_shapeFrameUsed.fill(false);
        m_shapeRanges = {};
        m_device = nullptr;
        m_fence = nullptr;
        return true;
    }

    bool DebugPrimitive::rebuildPipeline(
        const DX12Shader& vertexShader, const DX12Shader& shapeVertexShader, const DX12Shader& pixelShader)
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

        DX12GraphicsPipelineConfig shapeConfig = config;
        shapeConfig.vertexShader = &shapeVertexShader;
        shapeConfig.inputLayout = SHAPE_INPUT_LAYOUT;
        DX12GraphicsPipeline shapeReplacement;
        if (!shapeReplacement.initialize(*m_device->get(), shapeConfig))
            return false;

        m_pipeline.swap(replacement);
        m_shapePipeline.swap(shapeReplacement);
        return true;
    }

    bool DebugPrimitive::render(
        DX12CommandList& commandList,
        const std::uint32_t frameIndex,
        const Matrix& viewProjection,
        const CameraFrustum& frustum,
        const bool enabled)
    {
        if (frameIndex >= FRAME_COUNT)
            return false;
        m_frameUsed[frameIndex] = false;
        m_shapeFrameUsed[frameIndex] = false;
        consumeRequests();
        if (m_renderRequests.empty())
            return true;
        if (!enabled)
        {
            m_renderRequests.clear();
            return true;
        }

        if (!buildVertices(frustum))
            return false;
        m_renderRequests.clear();
        if (m_vertices.empty() && m_instances.empty())
            return true;

        const std::size_t byteSize = m_vertices.size() * sizeof(Vertex);
        const std::size_t instanceBytes = m_instances.size() * sizeof(ShapeInstance);
        if (byteSize > (std::numeric_limits<UINT>::max)()
            || m_vertices.size() > (std::numeric_limits<UINT>::max)()
            || instanceBytes > (std::numeric_limits<UINT>::max)()
            || m_instances.size() > (std::numeric_limits<UINT>::max)())
        {
            LOG_ERROR("[DebugPrimitive] Debug geometry exceeds DirectX 12 draw limits.");
            return false;
        }

        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        if (nativeCommandList == nullptr)
            return false;
        nativeCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        if (!m_vertices.empty())
        {
            if (!ensureFrameCapacity(frameIndex, m_vertices.size()))
                return false;
            DX12UploadBuffer& buffer = m_vertexBuffers[frameIndex];
            if (!buffer.write(std::as_bytes(std::span(m_vertices))) || !m_pipeline.bind(commandList))
                return false;
            const D3D12_VERTEX_BUFFER_VIEW view{
                .BufferLocation = buffer.getGpuVirtualAddress(),
                .SizeInBytes = static_cast<UINT>(byteSize),
                .StrideInBytes = sizeof(Vertex),
            };
            nativeCommandList->SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
            nativeCommandList->IASetVertexBuffers(0, 1, &view);
            nativeCommandList->DrawInstanced(static_cast<UINT>(m_vertices.size()), 1, 0, 0);
            m_frameUsed[frameIndex] = true;
        }
        if (!m_instances.empty())
        {
            if (!ensureInstanceCapacity(frameIndex, m_instances.size()))
                return false;
            DX12UploadBuffer& buffer = m_instanceBuffers[frameIndex];
            if (!buffer.write(std::as_bytes(std::span(m_instances))) || !m_shapePipeline.bind(commandList))
                return false;
            const std::array views = {
                D3D12_VERTEX_BUFFER_VIEW{ m_shapeVertices.getGpuVirtualAddress(),
                    static_cast<UINT>(m_shapeVertices.getSize()), sizeof(ShapeVertex) },
                D3D12_VERTEX_BUFFER_VIEW{ buffer.getGpuVirtualAddress(),
                    static_cast<UINT>(instanceBytes), sizeof(ShapeInstance) },
            };
            nativeCommandList->SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
            nativeCommandList->IASetVertexBuffers(0, static_cast<UINT>(views.size()), views.data());
            for (const ShapeRange& range : m_shapeRanges)
                if (range.instanceCount != 0)
                    nativeCommandList->DrawInstanced(
                        range.vertexCount, range.instanceCount, range.firstVertex, range.firstInstance);
            m_shapeFrameUsed[frameIndex] = true;
        }
        return true;
    }

    bool DebugPrimitive::markFrameUsed(const std::uint32_t frameIndex, const std::uint64_t fenceValue)
    {
        if (frameIndex >= FRAME_COUNT)
            return false;
        const bool linesMarked = !m_frameUsed[frameIndex] || m_vertexBuffers[frameIndex].markUsed(fenceValue);
        const bool instancesMarked = !m_shapeFrameUsed[frameIndex]
            || (m_instanceBuffers[frameIndex].markUsed(fenceValue) && m_shapeVertices.markUsed(fenceValue));
        return linesMarked && instancesMarked;
    }

    void DebugPrimitive::consumeRequests()
    {
        const std::scoped_lock lock(m_mutex);
        m_renderRequests.clear();
        m_renderRequests.swap(m_pending);
    }

    bool DebugPrimitive::buildVertices(const CameraFrustum& frustum)
    {
        m_vertices.clear();
        m_instances.clear();
        if (frustum.valid)
        {
            std::erase_if(m_renderRequests.lines, [&frustum](const LineRequest& line)
                { return !DebugPrimitiveCulling::isVisibleLine(frustum, line.start, line.end); });
            std::erase_if(m_renderRequests.grids, [&frustum](const GridRequest& grid)
                {
                    return !DebugPrimitiveCulling::isVisibleBox(frustum, Matrix::CreateTranslation(grid.center),
                        Vector3(grid.width * 0.5f, 0.0f, grid.depth * 0.5f));
                });
            std::erase_if(m_renderRequests.spheres, [&frustum](const SphereRequest& sphere)
                {
                    return !DebugPrimitiveCulling::isVisibleBox(frustum, sphere.world,
                        Vector3(sphere.radius, sphere.radius, sphere.radius));
                });
            std::erase_if(m_renderRequests.boxes, [&frustum](const BoxRequest& box)
                { return !DebugPrimitiveCulling::isVisibleBox(frustum, box.world, box.extents); });
            std::erase_if(m_renderRequests.cylinders, [&frustum](const CylinderRequest& cylinder)
                {
                    return !DebugPrimitiveCulling::isVisibleBox(frustum, cylinder.world,
                        Vector3(cylinder.radius, cylinder.height * 0.5f, cylinder.radius));
                });
            std::erase_if(m_renderRequests.capsules, [&frustum](const CapsuleRequest& capsule)
                {
                    return !DebugPrimitiveCulling::isVisibleBox(frustum, capsule.world,
                        Vector3(capsule.radius, capsule.halfHeight + capsule.radius, capsule.radius));
                });
        }
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
        if (!addVertices(m_renderRequests.lines.size(), 2))
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
            std::size_t instanceCount = 0;
            for (const std::size_t count : { m_renderRequests.spheres.size(), m_renderRequests.boxes.size(),
                m_renderRequests.cylinders.size(), m_renderRequests.capsules.size() })
            {
                if (count > maxVertexCount - instanceCount)
                {
                    LOG_ERROR("[DebugPrimitive] Requested instance count exceeds DirectX 12 draw limits.");
                    return false;
                }
                instanceCount += count;
            }
            if (instanceCount > maxVertexCount / sizeof(ShapeInstance))
            {
                LOG_ERROR("[DebugPrimitive] Instance data exceeds DirectX 12 buffer limits.");
                return false;
            }
            m_instances.reserve(instanceCount);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[DebugPrimitive] Failed to allocate debug geometry.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[DebugPrimitive] Debug geometry exceeds container capacity.");
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

        const auto rangeFor = [this](const ShapeKind kind) -> ShapeRange&
            { return m_shapeRanges[static_cast<std::size_t>(kind)]; };
        const auto beginRange = [this, &rangeFor](const ShapeKind kind)
            { rangeFor(kind).firstInstance = static_cast<std::uint32_t>(m_instances.size()); };
        const auto endRange = [this, &rangeFor](const ShapeKind kind)
            { rangeFor(kind).instanceCount = static_cast<std::uint32_t>(m_instances.size()) - rangeFor(kind).firstInstance; };
        beginRange(ShapeKind::Sphere);
        for (const SphereRequest& sphere : m_renderRequests.spheres)
            m_instances.push_back({ sphere.world, sphere.color, { sphere.radius, sphere.radius, sphere.radius, 0.0f } });
        endRange(ShapeKind::Sphere);
        beginRange(ShapeKind::Box);
        for (const BoxRequest& box : m_renderRequests.boxes)
            m_instances.push_back({ box.world, box.color, { box.extents.x, box.extents.y, box.extents.z, 0.0f } });
        endRange(ShapeKind::Box);
        beginRange(ShapeKind::Cylinder);
        for (const CylinderRequest& cylinder : m_renderRequests.cylinders)
            m_instances.push_back({ cylinder.world, cylinder.color,
                { cylinder.radius, cylinder.height * 0.5f, cylinder.radius, 0.0f } });
        endRange(ShapeKind::Cylinder);
        beginRange(ShapeKind::Capsule);
        for (const CapsuleRequest& capsule : m_renderRequests.capsules)
            m_instances.push_back({ capsule.world, capsule.color,
                { capsule.radius, capsule.radius, capsule.radius, capsule.halfHeight } });
        endRange(ShapeKind::Capsule);
        if (m_vertices.size() > maxVertexCount)
        {
            LOG_ERROR("[DebugPrimitive] Generated vertex count exceeds DirectX 12 draw limits.");
            return false;
        }
        return true;
    }

    bool DebugPrimitive::initializeShapeVertices()
    {
        std::vector<ShapeVertex> vertices;
        try
        {
            vertices.reserve(512);
            const auto appendLine = [&vertices](const Vector3& start, const Vector3& end,
                const float startOffset = 0.0f, const float endOffset = 0.0f)
                {
                    vertices.push_back({ start, startOffset });
                    vertices.push_back({ end, endOffset });
                };
            const auto rangeFor = [this](const ShapeKind kind) -> ShapeRange&
                { return m_shapeRanges[static_cast<std::size_t>(kind)]; };
            const auto beginRange = [&vertices, &rangeFor](const ShapeKind kind)
                { rangeFor(kind).firstVertex = static_cast<std::uint32_t>(vertices.size()); };
            const auto endRange = [&vertices, &rangeFor](const ShapeKind kind)
                { rangeFor(kind).vertexCount = static_cast<std::uint32_t>(vertices.size()) - rangeFor(kind).firstVertex; };
            const auto appendCircle = [&appendLine](const float y, const float offset)
                {
                    for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS; ++segment)
                    {
                        const float angle0 = DirectX::XM_2PI * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS);
                        const float angle1 = DirectX::XM_2PI * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS);
                        appendLine({ std::sin(angle0), y, std::cos(angle0) },
                            { std::sin(angle1), y, std::cos(angle1) }, offset, offset);
                    }
                };

            beginRange(ShapeKind::Sphere);
            for (std::uint32_t axis = 0; axis < 3; ++axis)
                for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS; ++segment)
                {
                    const float angle0 = DirectX::XM_2PI * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS);
                    const float angle1 = DirectX::XM_2PI * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS);
                    const float s0 = std::sin(angle0), c0 = std::cos(angle0);
                    const float s1 = std::sin(angle1), c1 = std::cos(angle1);
                    if (axis == 0)
                        appendLine({ 0.0f, s0, c0 }, { 0.0f, s1, c1 });
                    else if (axis == 1)
                        appendLine({ s0, 0.0f, c0 }, { s1, 0.0f, c1 });
                    else
                        appendLine({ s0, c0, 0.0f }, { s1, c1, 0.0f });
                }
            endRange(ShapeKind::Sphere);

            beginRange(ShapeKind::Box);
            constexpr std::array<std::array<std::uint32_t, 2>, 12> boxEdges = { {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            } };
            const std::array corners = {
                Vector3(-1, 1, -1), Vector3(1, 1, -1), Vector3(1, 1, 1), Vector3(-1, 1, 1),
                Vector3(-1, -1, -1), Vector3(1, -1, -1), Vector3(1, -1, 1), Vector3(-1, -1, 1),
            };
            for (const auto& edge : boxEdges)
                appendLine(corners[edge[0]], corners[edge[1]]);
            endRange(ShapeKind::Box);

            beginRange(ShapeKind::Cylinder);
            appendCircle(-1.0f, 0.0f);
            appendCircle(1.0f, 0.0f);
            for (std::uint32_t side = 0; side < 4; ++side)
            {
                const float angle = DirectX::XM_PIDIV2 * static_cast<float>(side);
                appendLine({ std::sin(angle), -1.0f, std::cos(angle) },
                    { std::sin(angle), 1.0f, std::cos(angle) });
            }
            endRange(ShapeKind::Cylinder);

            beginRange(ShapeKind::Capsule);
            appendCircle(0.0f, -1.0f);
            appendCircle(0.0f, 1.0f);
            for (std::uint32_t side = 0; side < 4; ++side)
            {
                const float azimuth = DirectX::XM_PIDIV2 * static_cast<float>(side);
                const Vector3 horizontal(std::sin(azimuth), 0.0f, std::cos(azimuth));
                appendLine(horizontal, horizontal, -1.0f, 1.0f);
                for (std::uint32_t segment = 0; segment < CURVE_SEGMENTS / 4; ++segment)
                {
                    const float angle0 = DirectX::XM_PIDIV2 * static_cast<float>(segment) / static_cast<float>(CURVE_SEGMENTS / 4);
                    const float angle1 = DirectX::XM_PIDIV2 * static_cast<float>(segment + 1) / static_cast<float>(CURVE_SEGMENTS / 4);
                    const Vector3 top0 = horizontal * std::cos(angle0) + Vector3(0.0f, std::sin(angle0), 0.0f);
                    const Vector3 top1 = horizontal * std::cos(angle1) + Vector3(0.0f, std::sin(angle1), 0.0f);
                    const Vector3 bottom0 = horizontal * std::cos(angle0) - Vector3(0.0f, std::sin(angle0), 0.0f);
                    const Vector3 bottom1 = horizontal * std::cos(angle1) - Vector3(0.0f, std::sin(angle1), 0.0f);
                    appendLine(top0, top1, 1.0f, 1.0f);
                    appendLine(bottom0, bottom1, -1.0f, -1.0f);
                }
            }
            endRange(ShapeKind::Capsule);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[DebugPrimitive] Failed to allocate reusable shape vertices.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[DebugPrimitive] Reusable shape vertices exceed container capacity.");
            return false;
        }

        const auto bytes = std::as_bytes(std::span(vertices));
        return m_shapeVertices.initialize(*m_device->get(), *m_fence, bytes.size())
            && m_shapeVertices.write(bytes);
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
        if (!buffer.finalize())
            return false;
        m_vertexCapacities[frameIndex] = 0;
        if (!buffer.initialize(*m_device->get(), *m_fence, capacity * sizeof(Vertex)))
            return false;
        m_vertexCapacities[frameIndex] = capacity;
        return true;
    }

    bool DebugPrimitive::ensureInstanceCapacity(const std::uint32_t frameIndex, const std::size_t instanceCount)
    {
        if (instanceCount <= m_instanceCapacities[frameIndex])
            return true;
        if (m_device == nullptr || m_fence == nullptr)
            return false;

        const std::size_t capacity = nextPowerOfTwo(std::max(instanceCount, std::size_t{ 256 }));
        if (capacity == 0 || capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(ShapeInstance))
        {
            LOG_ERROR("[DebugPrimitive] Instance buffer capacity is too large.");
            return false;
        }
        DX12UploadBuffer& buffer = m_instanceBuffers[frameIndex];
        if (!buffer.finalize())
            return false;
        m_instanceCapacities[frameIndex] = 0;
        if (!buffer.initialize(*m_device->get(), *m_fence, capacity * sizeof(ShapeInstance)))
            return false;
        m_instanceCapacities[frameIndex] = capacity;
        return true;
    }

    void DebugPrimitive::appendLine(const Vector3& start, const Vector3& end, const Color& color)
    {
        m_vertices.push_back({ start, color });
        m_vertices.push_back({ end, color });
    }
} // namespace Engine