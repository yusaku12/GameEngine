#include "Pch.h"
#include "Graphics\PostProcess\Aces2Luts.h"
#include "Graphics\PostProcess\PostProcessPipeline.h"
#include "Graphics\DirectX12\Command\Command.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include <bit>
#include <cmath>
#include <limits>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    namespace
    {
        constexpr std::size_t MAX_OCIO_TEXTURES = 38; // 64 root DWORDs minus input and constants.

        D3D12_VIEWPORT temporaryViewport(const D3D12_VIEWPORT& viewport,
            const PostProcessTargetDescription& description, const D3D12_RESOURCE_DESC& fullSize)
        {
            if (description.width == fullSize.Width && description.height == fullSize.Height)
                return viewport;
            const float scaleX = static_cast<float>(description.width) / static_cast<float>(fullSize.Width);
            const float scaleY = static_cast<float>(description.height) / static_cast<float>(fullSize.Height);
            const float left = std::floor(viewport.TopLeftX * scaleX);
            const float top = std::floor(viewport.TopLeftY * scaleY);
            return { left, top,
                std::min(static_cast<float>(description.width),
                    std::ceil((viewport.TopLeftX + viewport.Width) * scaleX)) - left,
                std::min(static_cast<float>(description.height),
                    std::ceil((viewport.TopLeftY + viewport.Height) * scaleY)) - top, 0, 1 };
        }
    }

    PostProcessPipeline::~PostProcessPipeline()
    {
        if (!finalize())
            LOG_CRITICAL("[PostProcess] Pipeline shutdown failed.");
    }

    PostProcessPipeline::OcioPass::~OcioPass()
    {
        for (const auto& texture : textures)
        {
            if (texture->srv && !heap->release(texture->srv->cpu.index))
                LOG_ERROR("[PostProcess] Cannot release an OCIO LUT descriptor.");
        }
    }

    bool PostProcessPipeline::initialize(ID3D12Device& device, const DX12Fence& fence,
        DX12DescriptorHeap& srvHeap)
    {
        if (!finalize() || !m_targets.initialize(device, fence, srvHeap))
            return false;
        m_device = &device;
        m_fence = &fence;
        m_srvHeap = &srvHeap;
        try
        {
            PostProcessOcioDescription description;
            auto& reach = description.textures.emplace_back();
            reach.dimension = PostProcessOcioTextureDimension::Texture1D;
            reach.width = static_cast<std::uint32_t>(Aces2Luts::reach.size());
            reach.channels = 1;
            reach.textureRegister = 3;
            reach.samplerRegister = 1;
            reach.linearInterpolation = false;
            reach.values.assign(Aces2Luts::reach.begin(), Aces2Luts::reach.end());
            auto& cusp = description.textures.emplace_back();
            cusp.dimension = PostProcessOcioTextureDimension::Texture1D;
            cusp.width = static_cast<std::uint32_t>(Aces2Luts::cusp.size() / 3);
            cusp.channels = 3;
            cusp.textureRegister = 4;
            cusp.samplerRegister = 1;
            cusp.linearInterpolation = false;
            cusp.values.assign(Aces2Luts::cusp.begin(), Aces2Luts::cusp.end());
            return configureOcioPass(&description, m_acesLuts, true);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcess] Cannot allocate ACES 2 lookup tables.");
            return false;
        }
    }

    bool PostProcessPipeline::finalize()
    {
        if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        if (!m_targets.finalize() || !releaseExposureHistory() || !releaseTemporalHistory())
            return false;
        m_passes.clear();
        m_customPipelines.clear();
        m_acesLuts.reset();
        m_ocio.reset();
        m_cameraOcio.reset();
        m_cameraOcioSnapshot.reset();
        m_output.finalize();
        m_toneMapping.finalize();
        for (auto& pipeline : m_builtins)
            pipeline.finalize();
        m_vertexShader.reset();
        m_lastFenceValue = 0;
        m_recorded = false;
        m_statistics = {};
        m_filmGrainFrameIndex = 0;
        m_device = nullptr;
        m_fence = nullptr;
        m_srvHeap = nullptr;
        return true;
    }

    bool PostProcessPipeline::createPipeline(DX12GraphicsPipeline& pipeline,
        const DX12Shader& vertex, const DX12Shader& pixel, const DXGI_FORMAT format,
        const bool outputTransform)
    {
        const D3D12_DESCRIPTOR_RANGE range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        const D3D12_DESCRIPTOR_RANGE secondaryRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
        const D3D12_DESCRIPTOR_RANGE tertiaryRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 };
        const D3D12_DESCRIPTOR_RANGE reachRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0 };
        const D3D12_DESCRIPTOR_RANGE cuspRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, 0 };
        std::array<D3D12_ROOT_PARAMETER, 6> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = { 1, &range };
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = { 0, 0, 9 };
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants = { 1, 0, 48 };
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3].DescriptorTable = { 1, &secondaryRange };
        if (outputTransform)
        {
            parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[4].DescriptorTable = { 1, &reachRange };
            parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[5].DescriptorTable = { 1, &cuspRange };
        }
        else
        {
            parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[4].Constants = { 2, 0, 4 };
            parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[5].DescriptorTable = { 1, &tertiaryRange };
        }
        for (auto& parameter : parameters)
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        const D3D12_STATIC_SAMPLER_DESC sampler{
            .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .MaxAnisotropy = 1,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
            .MaxLOD = D3D12_FLOAT32_MAX,
            .ShaderRegister = 0,
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
        };
        auto pointSampler = sampler;
        pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointSampler.ShaderRegister = 1;
        const std::array samplers{ sampler, pointSampler };
        return pipeline.initialize(*m_device, {
            .vertexShader = &vertex,
            .pixelShader = &pixel,
            .rootParameters = parameters,
            .staticSamplers = std::span(samplers).first(outputTransform ? 2 : 1),
            .renderTargetFormat = format,
            .enableDepthWrite = false,
            .cullMode = D3D12_CULL_MODE_NONE,
        });
    }

    bool PostProcessPipeline::createOcioPipeline(DX12GraphicsPipeline& pipeline,
        const DX12Shader& vertex, const OcioPass& pass)
    {
        std::array<D3D12_DESCRIPTOR_RANGE, MAX_OCIO_TEXTURES + 1> ranges{};
        std::array<D3D12_ROOT_PARAMETER, MAX_OCIO_TEXTURES + 3> parameters{};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = { 1, &ranges[0] };
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = { 0, 0, 9 };
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants = { 1, 0, 16 };

        std::array<D3D12_STATIC_SAMPLER_DESC, D3D12_COMMONSHADER_SAMPLER_REGISTER_COUNT> samplers{};
        std::size_t samplerCount = 0;
        const auto addSampler = [&samplers, &samplerCount](const std::uint32_t slot, const bool linear)
        {
            auto& sampler = samplers[samplerCount++];
            sampler.Filter = linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.MaxAnisotropy = 1;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = slot;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        };
        addSampler(0, true);
        for (std::size_t index = 0; index < pass.textures.size(); ++index)
        {
            const auto& texture = *pass.textures[index];
            ranges[index + 1] = {
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, texture.textureRegister, 0, 0 };
            auto& parameter = parameters[index + 3];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable = { 1, &ranges[index + 1] };
            if (std::none_of(samplers.begin(), samplers.begin() + samplerCount, [&texture](const auto& sampler)
                { return sampler.ShaderRegister == texture.samplerRegister; }))
                addSampler(texture.samplerRegister, texture.linearInterpolation);
        }
        const auto activeParameters = std::span(parameters).first(pass.textures.size() + 3);
        for (auto& parameter : activeParameters)
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return pipeline.initialize(*m_device, {
            .vertexShader = &vertex,
            .pixelShader = pass.shader.get(),
            .rootParameters = activeParameters,
            .staticSamplers = std::span(samplers).first(samplerCount),
            .renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
            .enableDepthWrite = false,
            .cullMode = D3D12_CULL_MODE_NONE,
        });
    }

    bool PostProcessPipeline::configureOcio(const PostProcessOcioDescription* description)
    {
        return configureOcioPass(description, m_ocio);
    }

    bool PostProcessPipeline::configureOcioPass(const PostProcessOcioDescription* description,
        std::unique_ptr<OcioPass>& destination, const bool lutOnly)
    {
        if (m_device == nullptr || m_recorded || (!lutOnly && !m_vertexShader))
        {
            LOG_ERROR("[PostProcess] Cannot configure OCIO on an uninitialized or unsubmitted pipeline.");
            return false;
        }
        if (description == nullptr)
        {
            if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
                return false;
            destination.reset();
            return true;
        }
        if ((!lutOnly && (!description->pixelShader || !description->pixelShader->isCompiled()))
            || (description->stage != PostProcessStage::BeforeToneMapping
                && description->stage != PostProcessStage::AfterToneMapping)
            || !std::isfinite(description->contribution)
            || description->contribution < 0 || description->contribution > 1
            || description->textures.size() > MAX_OCIO_TEXTURES
            || !std::all_of(description->parameters.begin(), description->parameters.end(),
                [](float value) { return std::isfinite(value); }))
        {
            LOG_ERROR("[PostProcess] Invalid OCIO shader, stage, constants, contribution or LUT count.");
            return false;
        }
        try
        {
            auto candidate = std::make_unique<OcioPass>();
            candidate->heap = m_srvHeap;
            candidate->shader = description->pixelShader;
            candidate->parameters = description->parameters;
            candidate->stage = description->stage;
            candidate->contribution = description->contribution;
            candidate->textures.reserve(description->textures.size());
            for (const auto& source : description->textures)
            {
                const bool validDimensions = source.width > 0 && source.height > 0 && source.depth > 0
                    && ((source.dimension == PostProcessOcioTextureDimension::Texture1D
                            && source.height == 1 && source.depth == 1
                            && source.width <= D3D12_REQ_TEXTURE1D_U_DIMENSION)
                        || (source.dimension == PostProcessOcioTextureDimension::Texture2D
                            && source.depth == 1
                            && source.width <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
                            && source.height <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
                        || (source.dimension == PostProcessOcioTextureDimension::Texture3D
                            && source.width <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && source.height <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && source.depth <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION));
                const std::uint64_t count = static_cast<std::uint64_t>(source.width)
                    * source.height * source.depth * source.channels;
                const auto duplicateTexture = [&source](const auto& texture)
                    { return texture->textureRegister == source.textureRegister; };
                const auto conflictingSampler = [&source](const auto& texture)
                    { return texture->samplerRegister == source.samplerRegister
                        && texture->linearInterpolation != source.linearInterpolation; };
                if (!validDimensions || (source.channels != 1 && source.channels != 3)
                    || source.textureRegister == 0
                    || source.textureRegister >= D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT
                    || source.samplerRegister >= D3D12_COMMONSHADER_SAMPLER_REGISTER_COUNT
                    || (source.samplerRegister == 0 && !source.linearInterpolation)
                    || count != source.values.size()
                    || std::any_of(candidate->textures.begin(), candidate->textures.end(), duplicateTexture)
                    || std::any_of(candidate->textures.begin(), candidate->textures.end(), conflictingSampler)
                    || !std::all_of(source.values.begin(), source.values.end(),
                        [](float value) { return std::isfinite(value); }))
                {
                    LOG_ERROR("[PostProcess] Invalid OCIO LUT dimensions, data or shader bindings.");
                    return false;
                }
                auto texture = std::make_unique<OcioTexture>();
                auto& entry = *texture;
                candidate->textures.push_back(std::move(texture));
                entry.textureRegister = source.textureRegister;
                entry.samplerRegister = source.samplerRegister;
                entry.linearInterpolation = source.linearInterpolation;
                const std::uint32_t gpuChannels = source.channels == 3 ? 4 : 1;
                const std::size_t texelCount = static_cast<std::size_t>(count / source.channels);
                entry.pixels.resize(texelCount * gpuChannels);
                if (source.channels == 1)
                    std::copy(source.values.begin(), source.values.end(), entry.pixels.begin());
                else
                {
                    for (std::size_t index = 0; index < texelCount; ++index)
                    {
                        std::copy_n(source.values.begin() + index * 3, 3, entry.pixels.begin() + index * 4);
                        entry.pixels[index * 4 + 3] = 1;
                    }
                }
                entry.rowPitch = static_cast<std::uint64_t>(source.width) * gpuChannels * sizeof(float);
                entry.slicePitch = entry.rowPitch * source.height;
                D3D12_RESOURCE_DESC resource{};
                resource.Dimension = source.dimension == PostProcessOcioTextureDimension::Texture1D
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                    : source.dimension == PostProcessOcioTextureDimension::Texture2D
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                resource.Width = source.width;
                resource.Height = source.height;
                resource.DepthOrArraySize = static_cast<UINT16>(source.depth);
                resource.MipLevels = 1;
                resource.Format = source.channels == 1 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
                resource.SampleDesc = { 1, 0 };
                D3D12_FEATURE_DATA_FORMAT_SUPPORT support{ .Format = resource.Format };
                if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))
                    || (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == 0
                    || !entry.resource.initialize(*m_device, {
                        .description = resource, .initialState = D3D12_RESOURCE_STATE_COPY_DEST }))
                {
                    LOG_ERROR("[PostProcess] Cannot create an OCIO LUT texture.");
                    return false;
                }
                const UINT64 uploadSize = GetRequiredIntermediateSize(entry.resource.get(), 0, 1);
                const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
                if (uploadSize == 0 || FAILED(m_device->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr, IID_PPV_ARGS(&entry.upload))))
                {
                    LOG_ERROR("[PostProcess] Cannot create an OCIO LUT upload buffer.");
                    return false;
                }
                entry.srv = m_srvHeap->allocate();
                if (!entry.srv || !entry.srv->gpu)
                {
                    LOG_ERROR("[PostProcess] Cannot allocate an OCIO LUT descriptor.");
                    return false;
                }
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = resource.Format;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                if (resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
                {
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                    srv.Texture1D.MipLevels = 1;
                }
                else if (resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Texture2D.MipLevels = 1;
                }
                else
                {
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    srv.Texture3D.MipLevels = 1;
                }
                m_device->CreateShaderResourceView(entry.resource.get(), &srv, entry.srv->cpu.native);
            }
            if (!lutOnly && !createOcioPipeline(candidate->pipeline, *m_vertexShader, *candidate))
                return false;
            if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
                return false;
            destination.swap(candidate);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcess] Cannot allocate OCIO LUT resources.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcess] OCIO LUT resources exceed available capacity.");
        }
        return false;
    }

    bool PostProcessPipeline::uploadOcioTextures(DX12CommandList& commandList, OcioPass& pass)
    {
        if (pass.uploaded)
        {
            if (pass.uploadFenceValue != 0 && m_fence->isComplete(pass.uploadFenceValue))
            {
                for (const auto& texture : pass.textures)
                    texture->upload.Reset();
                pass.uploadFenceValue = 0;
            }
            return true;
        }
        if (pass.uploadRecorded)
            return true;
        auto* const native = commandList.getForRecording();
        if (!native)
        {
            LOG_ERROR("[PostProcess] Cannot upload OCIO LUTs without a recording command list.");
            return false;
        }
        for (const auto& texture : pass.textures)
        {
            const D3D12_SUBRESOURCE_DATA data{
                texture->pixels.data(), static_cast<LONG_PTR>(texture->rowPitch),
                static_cast<LONG_PTR>(texture->slicePitch) };
            if (UpdateSubresources(native, texture->resource.get(), texture->upload.Get(), 0, 0, 1, &data) == 0
                || !texture->resource.transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            {
                LOG_ERROR("[PostProcess] Failed to upload an OCIO LUT.");
                return false;
            }
        }
        pass.uploadRecorded = true;
        return true;
    }

    bool PostProcessPipeline::createPasses(const std::span<const PostProcessPassDescription> descriptions,
        const DX12Shader& vertex, std::vector<std::unique_ptr<Pass>>& destination)
    {
        destination.reserve(descriptions.size());
        for (const auto& description : descriptions)
        {
            if (description.name.empty() || !description.pixelShader || !description.pixelShader->isCompiled()
                || (description.stage != PostProcessStage::BeforeToneMapping
                    && description.stage != PostProcessStage::AfterToneMapping)
                || description.resolutionDivisor == 0 || description.resolutionDivisor > 16
                || !std::all_of(description.parameters.begin(), description.parameters.end(),
                    [](const float value) { return std::isfinite(value); }))
            {
                LOG_ERROR("[PostProcess] Invalid fullscreen pass '{}'.", description.name);
                return false;
            }
            auto pass = std::make_unique<Pass>();
            pass->description = description;
            if (!createPipeline(pass->pipeline, vertex, *description.pixelShader, DXGI_FORMAT_R16G16B16A16_FLOAT))
                return false;
            destination.push_back(std::move(pass));
        }
        return true;
    }

    DX12GraphicsPipeline* PostProcessPipeline::getCustomPipeline(
        const std::shared_ptr<const DX12Shader>& shader)
    {
        if (!shader || !shader->isCompiled() || !m_vertexShader || !m_vertexShader->isCompiled())
        {
            LOG_ERROR("[PostProcess] Custom Effect has no compiled shader snapshot.");
            return nullptr;
        }
        if (const auto found = m_customPipelines.find(shader.get()); found != m_customPipelines.end())
            return &found->second->pipeline;
        try
        {
            auto candidate = std::make_unique<CustomPass>();
            candidate->shader = shader;
            if (!createPipeline(candidate->pipeline, *m_vertexShader, *shader, DXGI_FORMAT_R16G16B16A16_FLOAT))
                return nullptr;
            auto* const result = &candidate->pipeline;
            m_customPipelines.emplace(shader.get(), std::move(candidate));
            return result;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcess] Cannot allocate a custom Effect pipeline.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcess] Custom Effect pipeline cache capacity exceeded.");
        }
        return nullptr;
    }

    bool PostProcessPipeline::rebuild(std::shared_ptr<const DX12Shader> vertex,
        std::shared_ptr<const DX12Shader> toneMapping, std::shared_ptr<const DX12Shader> output,
        const PostProcessBuiltinShaders& builtins)
    {
        if (m_device == nullptr || m_recorded || !vertex || !toneMapping || !output
            || !vertex->isCompiled() || !toneMapping->isCompiled() || !output->isCompiled())
        {
            LOG_ERROR("[PostProcess] Invalid shader rebuild or unsubmitted commands.");
            return false;
        }
        if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        DX12GraphicsPipeline nextToneMapping;
        DX12GraphicsPipeline nextOutput;
        DX12GraphicsPipeline nextOcio;
        DX12GraphicsPipeline nextCameraOcio;
        std::array<DX12GraphicsPipeline, POST_PROCESS_BUILTIN_ENTRIES.size()> nextBuiltins;
        for (std::size_t index = 0; index < builtins.size(); ++index)
        {
            if (!builtins[index] || !builtins[index]->isCompiled())
            {
                LOG_ERROR("[PostProcess] Missing built-in shader '{}'.", POST_PROCESS_BUILTIN_ENTRIES[index]);
                return false;
            }
            const auto format = index == static_cast<std::size_t>(PostProcessBuiltin::AutoExposureDownsample)
                ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!createPipeline(nextBuiltins[index], *vertex, *builtins[index], format))
                return false;
        }
        std::vector<PostProcessPassDescription> descriptions;
        for (const auto& pass : m_passes)
            descriptions.push_back(pass->description);
        std::vector<std::unique_ptr<Pass>> nextPasses;
        std::unordered_map<const DX12Shader*, std::unique_ptr<CustomPass>> nextCustomPipelines;
        nextCustomPipelines.reserve(m_customPipelines.size());
        for (const auto& [key, pass] : m_customPipelines)
        {
            auto candidate = std::make_unique<CustomPass>();
            candidate->shader = pass->shader;
            if (!createPipeline(candidate->pipeline, *vertex, *candidate->shader, DXGI_FORMAT_R16G16B16A16_FLOAT))
                return false;
            nextCustomPipelines.emplace(key, std::move(candidate));
        }
        if (!createPipeline(nextToneMapping, *vertex, *toneMapping, DXGI_FORMAT_R16G16B16A16_FLOAT, true)
            || !createPipeline(nextOutput, *vertex, *output, DXGI_FORMAT_R8G8B8A8_UNORM, true)
            || !createPasses(descriptions, *vertex, nextPasses)
            || (m_ocio && !createOcioPipeline(nextOcio, *vertex, *m_ocio))
            || (m_cameraOcio && !createOcioPipeline(nextCameraOcio, *vertex, *m_cameraOcio)))
            return false;
        m_toneMapping.swap(nextToneMapping);
        m_output.swap(nextOutput);
        m_passes.swap(nextPasses);
        m_customPipelines.swap(nextCustomPipelines);
        if (m_ocio)
            m_ocio->pipeline.swap(nextOcio);
        if (m_cameraOcio)
            m_cameraOcio->pipeline.swap(nextCameraOcio);
        m_vertexShader = std::move(vertex);
        for (std::size_t index = 0; index < builtins.size(); ++index)
            m_builtins[index].swap(nextBuiltins[index]);
        return true;
    }

    bool PostProcessPipeline::configure(const std::span<const PostProcessPassDescription> descriptions)
    {
        if (m_device == nullptr || m_recorded || !m_vertexShader)
        {
            LOG_ERROR("[PostProcess] Cannot configure an uninitialized or unsubmitted pipeline.");
            return false;
        }
        if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        std::vector<std::unique_ptr<Pass>> nextPasses;
        if (!createPasses(descriptions, *m_vertexShader, nextPasses))
            return false;
        if (!releaseTargets())
            return false;
        m_passes.swap(nextPasses);
        return true;
    }

    bool PostProcessPipeline::draw(DX12CommandList& commandList, const DX12GraphicsPipeline& pipeline,
        const PostProcessImage& input, const PostProcessImage& output,
        const D3D12_VIEWPORT& sourceViewport, const D3D12_VIEWPORT& targetViewport,
        const float exposure, const bool toneMapping, const std::span<const float> parameters,
        const PostProcessImage* secondary, const D3D12_VIEWPORT* secondaryViewport,
        const DX12GpuDescriptorHandle* externalSecondary, const PostProcessImage* tertiary,
        const OcioPass* ocio, const bool outputTransform)
    {
        if (input.resource == output.resource || (secondary && secondary->resource == output.resource)
            || (tertiary && tertiary->resource == output.resource)
            || parameters.size() > (ocio ? 16u : 48u)
            || (ocio && (secondary || secondaryViewport || externalSecondary || tertiary || outputTransform)))
        {
            LOG_ERROR("[PostProcess] A texture cannot be read and rendered to in the same pass.");
            return false;
        }
        if (outputTransform && (!m_acesLuts || !uploadOcioTextures(commandList, *m_acesLuts)))
            return false;
        if (!input.resource->transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            || (secondary && !secondary->resource->transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            || (tertiary && !tertiary->resource->transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            || (externalSecondary && externalSecondary->native.ptr == 0)
            || !output.resource->transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET)
            || !pipeline.bind(commandList))
            return false;
        struct Constants
        {
            Vector4 source;
            Vector4 target;
            std::uint32_t exposureMode;
        };
        static_assert(sizeof(Constants) == sizeof(float) * 9);
        std::uint32_t exposureMode = std::bit_cast<std::uint32_t>(exposure) & 0x7fffffffu;
        if (toneMapping)
            exposureMode |= 0x80000000u;
        const Constants constants{
            Vector4(sourceViewport.TopLeftX, sourceViewport.TopLeftY, sourceViewport.Width, sourceViewport.Height),
            Vector4(targetViewport.TopLeftX, targetViewport.TopLeftY, targetViewport.Width, targetViewport.Height),
            exposureMode,
        };
        auto* const native = commandList.getForRecording();
        ID3D12DescriptorHeap* const heap = m_srvHeap->get();
        native->SetDescriptorHeaps(1, &heap);
        native->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        native->OMSetRenderTargets(1, &output.rtv.native, FALSE, nullptr);
        native->RSSetViewports(1, &targetViewport);
        const D3D12_RECT scissor{
            static_cast<LONG>(std::floor(targetViewport.TopLeftX)),
            static_cast<LONG>(std::floor(targetViewport.TopLeftY)),
            static_cast<LONG>(std::ceil(targetViewport.TopLeftX + targetViewport.Width)),
            static_cast<LONG>(std::ceil(targetViewport.TopLeftY + targetViewport.Height)),
        };
        native->RSSetScissorRects(1, &scissor);
        native->SetGraphicsRootDescriptorTable(0, input.srv.native);
        native->SetGraphicsRoot32BitConstants(1, 9, &constants, 0);
        std::array<float, 48> rootParameters{};
        std::copy(parameters.begin(), parameters.end(), rootParameters.begin());
        native->SetGraphicsRoot32BitConstants(2, ocio ? 16 : static_cast<UINT>(rootParameters.size()),
            rootParameters.data(), 0);
        if (ocio)
        {
            for (std::size_t index = 0; index < ocio->textures.size(); ++index)
                native->SetGraphicsRootDescriptorTable(static_cast<UINT>(index + 3),
                    ocio->textures[index]->srv->gpu->native);
        }
        else
        {
            native->SetGraphicsRootDescriptorTable(3, secondary ? secondary->srv.native
                : externalSecondary ? externalSecondary->native : input.srv.native);
            if (outputTransform)
            {
                native->SetGraphicsRootDescriptorTable(4, m_acesLuts->textures[0]->srv->gpu->native);
                native->SetGraphicsRootDescriptorTable(5, m_acesLuts->textures[1]->srv->gpu->native);
            }
            else
            {
                const auto& auxiliaryViewport = secondaryViewport ? *secondaryViewport : sourceViewport;
                const Vector4 auxiliary(auxiliaryViewport.TopLeftX, auxiliaryViewport.TopLeftY,
                    auxiliaryViewport.Width, auxiliaryViewport.Height);
                native->SetGraphicsRoot32BitConstants(4, 4, &auxiliary, 0);
                native->SetGraphicsRootDescriptorTable(5, tertiary ? tertiary->srv.native : input.srv.native);
            }
        }
        native->DrawInstanced(3, 1, 0, 0);
        ++m_statistics.drawCalls;
        return true;
    }

    bool PostProcessPipeline::record(DX12CommandList& commandList, const PostProcessImage& input,
        const PostProcessImage& output, const D3D12_VIEWPORT& viewport,
        const PostProcessCameraSnapshot& camera, const PostProcessImage* depth,
        const PostProcessImage* motionVectors)
    {
        m_statistics = {};
        if (m_device == nullptr || m_recorded || commandList.getForRecording() == nullptr
            || m_output.get() == nullptr || input.resource == nullptr || output.resource == nullptr
            || input.resource->get() == nullptr || output.resource->get() == nullptr
            || input.resource == output.resource || input.srv.native.ptr == 0 || output.rtv.native.ptr == 0
            || !std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY)
            || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || !std::isfinite(viewport.MinDepth) || !std::isfinite(viewport.MaxDepth)
            || !std::isfinite(camera.deltaTimeSeconds) || camera.deltaTimeSeconds < 0
            || viewport.MinDepth < 0 || viewport.MaxDepth > 1 || viewport.MinDepth > viewport.MaxDepth
            || viewport.TopLeftX < 0 || viewport.TopLeftY < 0 || viewport.Width <= 0 || viewport.Height <= 0)
        {
            LOG_ERROR("[PostProcess] Invalid frame input or unsubmitted commands.");
            return false;
        }
        if (!std::isfinite(camera.weight) || camera.weight < 0 || camera.weight > 1
            || !std::isfinite(camera.nearClip) || !std::isfinite(camera.farClip)
            || camera.nearClip <= 0 || camera.farClip <= camera.nearClip
            || !std::isfinite(camera.lightShafts.sourcePosition.x)
            || !std::isfinite(camera.lightShafts.sourcePosition.y)
            || !std::isfinite(camera.lightShafts.bloomScale)
            || camera.lightShafts.bloomScale < 0 || camera.lightShafts.bloomScale > 10
            || !std::isfinite(camera.lightShafts.bloomThreshold)
            || camera.lightShafts.bloomThreshold < 0 || camera.lightShafts.bloomThreshold > 64
            || !std::isfinite(camera.lightShafts.tint.x) || !std::isfinite(camera.lightShafts.tint.y)
            || !std::isfinite(camera.lightShafts.tint.z)
            || camera.lightShafts.tint.x < 0 || camera.lightShafts.tint.x > 1
            || camera.lightShafts.tint.y < 0 || camera.lightShafts.tint.y > 1
            || camera.lightShafts.tint.z < 0 || camera.lightShafts.tint.z > 1
            || !std::isfinite(camera.lightShafts.occlusionMaskDarkness)
            || camera.lightShafts.occlusionMaskDarkness < 0 || camera.lightShafts.occlusionMaskDarkness > 1
            || !std::all_of(POST_PROCESS_WEIGHT_MEMBERS.begin(), POST_PROCESS_WEIGHT_MEMBERS.end(),
                [&camera](const auto member) { const float value = camera.effects.*member;
                    return std::isfinite(value) && value >= 0 && value <= 1; })
            || (camera.settings.ocioEnabled.value && !camera.ocio)
            || !validatePostProcessSettings(camera.settings)
            || !std::all_of(camera.customEffects.begin(), camera.customEffects.end(), [](const auto& effect)
            {
                return effect.pixelShader && effect.pixelShader->isCompiled()
                    && (effect.stage == PostProcessStage::BeforeToneMapping
                        || effect.stage == PostProcessStage::AfterToneMapping)
                    && effect.resolutionDivisor > 0 && effect.resolutionDivisor <= 16
                    && std::isfinite(effect.contribution) && effect.contribution >= 0 && effect.contribution <= 1
                    && std::all_of(effect.parameters.begin(), effect.parameters.end(),
                        [](const float value) { return std::isfinite(value); });
            }))
        {
            LOG_ERROR("[PostProcess] Invalid Camera PostEffect snapshot.");
            return false;
        }
        const auto& settings = camera.settings;
        const float weight = camera.enabled ? camera.weight : 0;
        const float ocioWeight = camera.ocio
            ? (settings.ocioEnabled.value ? weight * camera.effects.ocio * settings.ocioIntensity.value : 0)
            : weight;
        const float bloomWeight = weight * camera.effects.bloom;
        const float exposureWeight = weight * camera.effects.exposure;
        const float vignetteWeight = weight * camera.effects.vignette;
        const float chromaticAberrationWeight = weight * camera.effects.chromaticAberration;
        const float filmGrainWeight = weight * camera.effects.filmGrain;
        const float fxaaWeight = weight * camera.effects.fxaa;
        const float temporalAAWeight = weight * camera.effects.temporalAA;
        const float motionBlurWeight = weight * camera.effects.motionBlur;
        const float depthOfFieldWeight = weight * camera.effects.depthOfField;
        const float autoExposureWeight = weight * camera.effects.autoExposure;
        const bool bloom = bloomWeight > 0 && settings.bloomEnabled.value && settings.bloomIntensity.value > 0;
        const bool vignette = vignetteWeight > 0 && settings.vignetteEnabled.value && settings.vignetteIntensity.value > 0;
        const bool chromaticAberration = chromaticAberrationWeight > 0
            && settings.chromaticAberrationEnabled.value && settings.chromaticAberrationIntensity.value > 0;
        const bool filmGrain = filmGrainWeight > 0
            && settings.filmGrainEnabled.value && settings.filmGrainIntensity.value > 0;
        const bool temporalAA = temporalAAWeight > 0 && settings.temporalAAEnabled.value;
        const bool motionBlur = motionBlurWeight > 0 && settings.motionBlurEnabled.value
            && settings.motionBlurAmount.value > 0 && settings.motionBlurMaxVelocity.value > 0;
        const bool fxaa = !temporalAA && fxaaWeight > 0 && settings.fxaaEnabled.value;
        const bool depthOfField = depthOfFieldWeight > 0 && settings.depthOfFieldEnabled.value
            && settings.depthOfFieldMaxBlur.value > 0;
        const bool lightShafts = camera.lightShafts.bloomEnabled && camera.lightShafts.bloomScale > 0;
        const bool autoExposure = autoExposureWeight > 0 && settings.autoExposureEnabled.value;
        if (autoExposure && camera.cameraGuid.isValid())
        {
            if (m_exposureHistoryCamera != camera.cameraGuid)
            {
                m_exposureHistoryCamera = camera.cameraGuid;
                m_exposureHistoryReadIndex = 0;
                m_exposureHistoryValid = false;
            }
            if (camera.temporalHistoryReset)
            {
                m_exposureHistoryReadIndex = 0;
                m_exposureHistoryValid = false;
            }
        }
        else
        {
            m_exposureHistoryCamera = camera.cameraGuid;
            m_exposureHistoryReadIndex = 0;
            m_exposureHistoryValid = false;
        }
        if (temporalAA)
        {
            if (!camera.cameraGuid.isValid())
            {
                LOG_ERROR("[PostProcess] Temporal anti-aliasing requires a stable Camera GUID.");
                return false;
            }
            if (m_temporalHistoryCamera != camera.cameraGuid)
            {
                m_temporalHistoryCamera = camera.cameraGuid;
                m_temporalHistoryReadIndex = 0;
                m_temporalHistoryValid = false;
            }
            if (camera.temporalHistoryReset)
            {
                m_temporalHistoryReadIndex = 0;
                m_temporalHistoryValid = false;
            }
        }
        else
        {
            m_temporalHistoryCamera = {};
            m_temporalHistoryValid = false;
            m_temporalHistoryReadIndex = 0;
        }
        const float manualWeight = settings.autoExposureEnabled.value ? 1.0f - camera.effects.autoExposure : 1.0f;
        const float effectiveExposure = exposureWeight > 0 && settings.exposureEnabled.value
            ? std::exp2(settings.exposureEV.value * exposureWeight * manualWeight) : 1.0f;
        if (!std::isfinite(effectiveExposure))
        {
            LOG_ERROR("[PostProcess] Camera exposure overflowed.");
            return false;
        }
        std::array<float, 48> displayParameters{};
        if (autoExposure)
        {
            displayParameters[12] = autoExposureWeight;
        }
        if (filmGrain)
        {
            m_statistics.filmGrainFrameIndex = m_filmGrainFrameIndex;
            // The shader receives this seed as float, so keep its integer representation exact.
            m_filmGrainFrameIndex = (m_filmGrainFrameIndex + 1u) & 0x00ffffffu;
        }
        const auto& inputDesc = input.resource->getDescription();
        const auto& outputDesc = output.resource->getDescription();
        if (inputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
            || outputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
            || inputDesc.SampleDesc.Count != 1 || outputDesc.SampleDesc.Count != 1
            || inputDesc.DepthOrArraySize != 1 || outputDesc.DepthOrArraySize != 1
            || inputDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT
            || outputDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM
            || inputDesc.Width != outputDesc.Width || inputDesc.Height != outputDesc.Height
            || viewport.TopLeftX + viewport.Width > static_cast<float>(inputDesc.Width)
            || viewport.TopLeftY + viewport.Height > static_cast<float>(inputDesc.Height))
        {
            LOG_ERROR("[PostProcess] Incompatible HDR/display textures or viewport.");
            return false;
        }
        if (depthOfField || lightShafts)
        {
            if (depth == nullptr || depth->resource == nullptr || depth->resource->get() == nullptr
                || depth->srv.native.ptr == 0
                || depth->resource->getDescription().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
                || depth->resource->getDescription().Width != inputDesc.Width
                || depth->resource->getDescription().Height != inputDesc.Height)
            {
                LOG_ERROR("[PostProcess] Depth effects require a matching Camera depth texture.");
                return false;
            }
        }
        if (temporalAA || motionBlur)
        {
            if (motionVectors == nullptr || motionVectors->resource == nullptr
                || motionVectors->resource->get() == nullptr || motionVectors->srv.native.ptr == 0
                || motionVectors->resource->getDescription().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
                || motionVectors->resource->getDescription().Format != DXGI_FORMAT_R16G16B16A16_FLOAT
                || motionVectors->resource->getDescription().Width != inputDesc.Width
                || motionVectors->resource->getDescription().Height != inputDesc.Height)
            {
                LOG_ERROR("[PostProcess] Temporal AA and Motion Blur require a matching deferred velocity texture.");
                return false;
            }
        }
        m_temporalHistoryRecorded = false;
        if (temporalAA && !initializeTemporalHistory(
            static_cast<std::uint32_t>(inputDesc.Width), inputDesc.Height))
            return false;
        if (temporalAA)
            m_temporalHistoryCamera = camera.cameraGuid;
        if (camera.ocio && ocioWeight > 0 && camera.ocio != m_cameraOcioSnapshot)
        {
            if (!configureOcioPass(camera.ocio.get(), m_cameraOcio))
                return false;
            m_cameraOcioSnapshot = camera.ocio;
        }
        OcioPass* const activeOcio = camera.ocio ? (ocioWeight > 0 ? m_cameraOcio.get() : nullptr) : m_ocio.get();
        if (!m_targets.beginRecording())
            return false;
        m_recorded = true;
        // Fractional viewports can have no pixels after Direct3D's implicit scissor.
        if (std::ceil(viewport.TopLeftX - 0.5f) >= std::floor(viewport.TopLeftX + viewport.Width)
            || std::ceil(viewport.TopLeftY - 0.5f) >= std::floor(viewport.TopLeftY + viewport.Height))
        {
            const std::array<float, 16> parameters{};
            if (!draw(commandList, m_output, input, output, viewport, viewport, effectiveExposure, true, parameters,
                nullptr, nullptr, nullptr, nullptr, nullptr, true))
                return false;
            m_statistics.temporaryTargets = m_targets.size();
            return true;
        }
        const PostProcessImage* current = &input;
        const PostProcessImage* borrowed = nullptr;
        D3D12_VIEWPORT currentViewport = viewport;
        const std::array<float, 16> emptyParameters{};
        const auto runTemporary = [&](const DX12GraphicsPipeline& pipeline, const std::uint32_t divisor,
            const bool toneMapping, const std::span<const float> parameters,
            const PostProcessImage* secondary = nullptr, const D3D12_VIEWPORT* secondaryViewport = nullptr,
            const DX12GpuDescriptorHandle* externalSecondary = nullptr)
        {
            const PostProcessTargetDescription description{
                static_cast<std::uint32_t>((outputDesc.Width + divisor - 1) / divisor),
                (outputDesc.Height + divisor - 1) / divisor,
            };
            PostProcessImage* const next = m_targets.acquire(description);
            if (next == nullptr)
                return false;
            const auto nextViewport = temporaryViewport(viewport, description, outputDesc);
            if (!draw(commandList, pipeline, *current, *next, currentViewport, nextViewport,
                effectiveExposure, toneMapping, parameters, secondary, secondaryViewport, externalSecondary,
                nullptr, nullptr, toneMapping))
                return false;
            if (borrowed && !m_targets.release(*borrowed))
                return false;
            borrowed = next;
            current = next;
            currentViewport = nextViewport;
            return true;
        };
        bool depthWasUsed = false;
        const auto runDepthEffect = [&](const PostProcessBuiltin slot, const std::span<const float> parameters,
            const std::uint32_t resolutionDivisor)
        {
            const PostProcessTargetDescription description{
                static_cast<std::uint32_t>((outputDesc.Width + resolutionDivisor - 1) / resolutionDivisor),
                (outputDesc.Height + resolutionDivisor - 1) / resolutionDivisor,
            };
            PostProcessImage* const next = m_targets.acquire(description);
            const auto targetViewport = temporaryViewport(viewport, description, outputDesc);
            if (next == nullptr || !draw(commandList, m_builtins[static_cast<std::size_t>(slot)],
                *current, *next, currentViewport, targetViewport, effectiveExposure, false, parameters,
                depth, &viewport))
                return false;
            if (borrowed && !m_targets.release(*borrowed))
                return false;
            borrowed = next;
            current = next;
            currentViewport = targetViewport;
            depthWasUsed = true;
            ++m_statistics.effectPasses;
            return true;
        };
        const auto runCustomEffects = [&](const PostProcessStage stage)
        {
            for (const auto& effect : camera.customEffects)
            {
                const float contribution = weight * effect.contribution;
                if (effect.stage != stage || contribution <= 0)
                    continue;
                const auto* const pipeline = getCustomPipeline(effect.pixelShader);
                if (!pipeline)
                    return false;
                const PostProcessTargetDescription description{
                    static_cast<std::uint32_t>((outputDesc.Width + effect.resolutionDivisor - 1)
                        / effect.resolutionDivisor),
                    (outputDesc.Height + effect.resolutionDivisor - 1) / effect.resolutionDivisor,
                };
                const auto targetViewport = temporaryViewport(viewport, description, outputDesc);
                PostProcessImage* const effectImage = m_targets.acquire(description);
                if (!effectImage || !draw(commandList, *pipeline, *current, *effectImage,
                    currentViewport, targetViewport, effectiveExposure, false, effect.parameters))
                    return false;
                PostProcessImage* const blendedImage = m_targets.acquire(description);
                if (!blendedImage)
                    return false;
                std::array<float, 16> blendParameters{};
                blendParameters[0] = contribution;
                const auto& blendPipeline = m_builtins[static_cast<std::size_t>(PostProcessBuiltin::CustomComposite)];
                if (!draw(commandList, blendPipeline, *current, *blendedImage, currentViewport,
                    targetViewport, effectiveExposure, false, blendParameters, effectImage, &targetViewport)
                    || !m_targets.release(*effectImage)
                    || (borrowed && !m_targets.release(*borrowed)))
                    return false;
                borrowed = blendedImage;
                current = blendedImage;
                currentViewport = targetViewport;
                ++m_statistics.effectPasses;
            }
            return true;
        };
        const auto runOcio = [&](const PostProcessStage stage)
        {
            if (!activeOcio || activeOcio->stage != stage || ocioWeight <= 0 || activeOcio->contribution <= 0)
                return true;
            if (!uploadOcioTextures(commandList, *activeOcio))
                return false;
            const PostProcessTargetDescription description{
                static_cast<std::uint32_t>(outputDesc.Width), outputDesc.Height };
            PostProcessImage* const effectImage = m_targets.acquire(description);
            if (!effectImage || !draw(commandList, activeOcio->pipeline, *current, *effectImage,
                currentViewport, viewport, effectiveExposure, false, activeOcio->parameters,
                nullptr, nullptr, nullptr, nullptr, activeOcio))
                return false;
            PostProcessImage* const blendedImage = m_targets.acquire(description);
            if (!blendedImage)
                return false;
            const std::array<float, 16> blendParameters{ ocioWeight * activeOcio->contribution };
            const auto& blendPipeline = m_builtins[static_cast<std::size_t>(PostProcessBuiltin::CustomComposite)];
            if (!draw(commandList, blendPipeline, *current, *blendedImage, currentViewport,
                viewport, effectiveExposure, false, blendParameters, effectImage, &viewport)
                || !m_targets.release(*effectImage)
                || (borrowed && !m_targets.release(*borrowed)))
                return false;
            borrowed = blendedImage;
            current = blendedImage;
            currentViewport = viewport;
            ++m_statistics.effectPasses;
            return true;
        };
        if (depthOfField)
        {
            std::array<float, 16> parameters{};
            parameters[0] = settings.depthOfFieldFocusDistance.value;
            parameters[1] = settings.depthOfFieldFocusRange.value;
            parameters[2] = settings.depthOfFieldMaxBlur.value;
            parameters[3] = camera.nearClip;
            parameters[4] = camera.farClip;
            parameters[5] = depthOfFieldWeight;
            parameters[8] = camera.perspectiveProjection ? 1.0f : 0.0f;
            if (!runDepthEffect(PostProcessBuiltin::DepthOfField, parameters, 1))
                return false;
        }
        if (lightShafts)
        {
            const std::array<float, 16> parameters{
                camera.lightShafts.sourcePosition.x,
                camera.lightShafts.sourcePosition.y,
                camera.lightShafts.bloomScale,
                camera.lightShafts.bloomThreshold,
                camera.lightShafts.tint.x,
                camera.lightShafts.tint.y,
                camera.lightShafts.tint.z,
                camera.lightShafts.occlusionMaskDarkness,
                camera.lightShafts.occlusionEnabled ? 1.0f : 0.0f,
            };
            if (!runDepthEffect(PostProcessBuiltin::LightShafts, parameters, 2))
                return false;
        }
        if (motionBlur)
        {
            PostProcessImage* const next = m_targets.acquire({
                static_cast<std::uint32_t>(outputDesc.Width), static_cast<std::uint32_t>(outputDesc.Height),
            });
            const std::array<float, 16> parameters{
                settings.motionBlurAmount.value * motionBlurWeight,
                settings.motionBlurMaxVelocity.value,
            };
            if (next == nullptr || !draw(commandList,
                m_builtins[static_cast<std::size_t>(PostProcessBuiltin::MotionBlur)],
                *current, *next, currentViewport, viewport, effectiveExposure, false, parameters,
                motionVectors, &viewport))
                return false;
            if (borrowed && !m_targets.release(*borrowed))
                return false;
            borrowed = next;
            current = next;
            currentViewport = viewport;
            ++m_statistics.effectPasses;
        }
        for (const auto& pass : m_passes)
        {
            if (pass->description.enabled && pass->description.stage == PostProcessStage::BeforeToneMapping)
            {
                if (!runTemporary(pass->pipeline, pass->description.resolutionDivisor, false,
                    pass->description.parameters))
                    return false;
                ++m_statistics.effectPasses;
            }
        }
        if (!runCustomEffects(PostProcessStage::BeforeToneMapping))
            return false;
        if (temporalAA)
        {
            const std::size_t writeIndex = 1 - m_temporalHistoryReadIndex;
            const PostProcessImage& history = m_temporalHistoryImages[m_temporalHistoryReadIndex];
            PostProcessImage& outputHistory = m_temporalHistoryImages[writeIndex];
            const std::array<float, 16> parameters{
                settings.temporalAAFeedback.value * temporalAAWeight,
                m_temporalHistoryValid ? 1.0f : 0.0f,
                camera.nearClip,
                camera.farClip,
                camera.perspectiveProjection ? 1.0f : 0.0f,
            };
            if (!draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::TemporalAA)],
                *current, outputHistory, currentViewport, viewport, effectiveExposure, false, parameters,
                motionVectors, &viewport, nullptr, &history)
                || !outputHistory.resource->transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            if (borrowed && !m_targets.release(*borrowed))
                return false;
            borrowed = nullptr;
            current = &outputHistory;
            currentViewport = viewport;
            m_temporalHistoryPendingReadIndex = writeIndex;
            m_temporalHistoryRecorded = true;
            ++m_statistics.effectPasses;
        }
        PostProcessImage* exposureTarget = nullptr;
        D3D12_VIEWPORT exposureViewport{};
        if (autoExposure)
        {
            exposureTarget = recordAutoExposure(commandList, *current, currentViewport);
            if (exposureTarget == nullptr)
                return false;
            exposureViewport = { 0, 0, 1, 1, 0, 1 };
            exposureTarget = recordExposureAdaptation(commandList, *exposureTarget, exposureViewport,
                settings, camera.deltaTimeSeconds);
            if (exposureTarget == nullptr)
                return false;
        }
        if (bloom && !recordBloom(commandList, current, borrowed, currentViewport, output, viewport,
            settings.bloomIntensity.value * bloomWeight, settings.bloomThreshold.value,
            settings.bloomScatter.value, settings.bloomSizeScale.value))
            return false;
        if (!runOcio(PostProcessStage::BeforeToneMapping))
            return false;
        const bool hasLdrPasses = vignette || chromaticAberration || filmGrain || fxaa
            || (activeOcio && activeOcio->stage == PostProcessStage::AfterToneMapping
                && ocioWeight > 0 && activeOcio->contribution > 0)
            || std::any_of(m_passes.begin(), m_passes.end(), [](const auto& pass)
        {
            return pass->description.enabled && pass->description.stage == PostProcessStage::AfterToneMapping;
        }) || (weight > 0 && std::any_of(camera.customEffects.begin(), camera.customEffects.end(),
            [](const auto& effect)
            { return effect.stage == PostProcessStage::AfterToneMapping && effect.contribution > 0; }));
        if (hasLdrPasses)
        {
            if (!runTemporary(m_toneMapping, 1, true, displayParameters, exposureTarget,
                exposureTarget ? &exposureViewport : nullptr))
                return false;
            for (const auto& pass : m_passes)
            {
                if (pass->description.enabled && pass->description.stage == PostProcessStage::AfterToneMapping)
                {
                    if (!runTemporary(pass->pipeline, pass->description.resolutionDivisor, false,
                        pass->description.parameters))
                        return false;
                    ++m_statistics.effectPasses;
                }
            }
            if (!runCustomEffects(PostProcessStage::AfterToneMapping))
                return false;
            if (!runOcio(PostProcessStage::AfterToneMapping))
                return false;
            if (vignette)
            {
                const auto& color = settings.vignetteColor.value;
                const std::array<float, 16> parameters{
                    settings.vignetteIntensity.value * vignetteWeight, settings.vignetteSmoothness.value,
                    settings.vignetteRoundness.value, viewport.Width / viewport.Height,
                    color.x, color.y, color.z,
                };
                if (!runTemporary(m_builtins[static_cast<std::size_t>(PostProcessBuiltin::Vignette)], 1, false, parameters))
                    return false;
                ++m_statistics.effectPasses;
            }
            if (chromaticAberration)
            {
                const std::array<float, 16> parameters{
                    settings.chromaticAberrationIntensity.value * chromaticAberrationWeight,
                    settings.chromaticAberrationStartOffset.value,
                };
                if (!runTemporary(m_builtins[static_cast<std::size_t>(PostProcessBuiltin::ChromaticAberration)],
                    1, false, parameters))
                    return false;
                ++m_statistics.effectPasses;
            }
            if (fxaa)
            {
                const std::array<float, 16> parameters{
                    settings.fxaaSubpixel.value, settings.fxaaEdgeThreshold.value, settings.fxaaEdgeThresholdMin.value, fxaaWeight,
                };
                if (!runTemporary(m_builtins[static_cast<std::size_t>(PostProcessBuiltin::Fxaa)], 1, false, parameters))
                    return false;
                ++m_statistics.effectPasses;
            }
            if (filmGrain)
            {
                const std::array<float, 16> parameters{
                    settings.filmGrainIntensity.value * filmGrainWeight,
                    settings.filmGrainResponse.value,
                    static_cast<float>(m_statistics.filmGrainFrameIndex),
                };
                if (!runTemporary(m_builtins[static_cast<std::size_t>(PostProcessBuiltin::FilmGrain)],
                    1, false, parameters))
                    return false;
                ++m_statistics.effectPasses;
            }
        }
        const std::span<const float> outputParameters = hasLdrPasses
            ? std::span<const float>(emptyParameters) : std::span<const float>(displayParameters);
        if (!draw(commandList, m_output, *current, output, currentViewport, viewport,
            effectiveExposure, !hasLdrPasses, outputParameters,
            !hasLdrPasses ? exposureTarget : nullptr,
            !hasLdrPasses && exposureTarget ? &exposureViewport : nullptr,
            nullptr, nullptr, nullptr, true))
            return false;
        if (borrowed && !m_targets.release(*borrowed))
            return false;
        if (depthWasUsed && !depth->resource->transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE))
            return false;
        m_statistics.temporaryTargets = m_targets.size();
        return true;
    }

    PostProcessImage* PostProcessPipeline::recordAutoExposure(DX12CommandList& commandList,
        const PostProcessImage& input, const D3D12_VIEWPORT& sourceViewport)
    {
        const auto& inputDescription = input.resource->getDescription();
        const float left = std::clamp(std::ceil(sourceViewport.TopLeftX - 0.5f),
            0.0f, static_cast<float>(inputDescription.Width));
        const float top = std::clamp(std::ceil(sourceViewport.TopLeftY - 0.5f),
            0.0f, static_cast<float>(inputDescription.Height));
        const float right = std::clamp(std::ceil(sourceViewport.TopLeftX + sourceViewport.Width - 0.5f),
            0.0f, static_cast<float>(inputDescription.Width));
        const float bottom = std::clamp(std::ceil(sourceViewport.TopLeftY + sourceViewport.Height - 0.5f),
            0.0f, static_cast<float>(inputDescription.Height));
        if (right <= left || bottom <= top)
        {
            LOG_ERROR("[PostProcess] Automatic exposure viewport contains no pixels.");
            return nullptr;
        }
        const auto width = static_cast<std::uint32_t>(right - left);
        const auto height = static_cast<std::uint32_t>(bottom - top);
        PostProcessTargetDescription description{
            (width + 1) / 2, (height + 1) / 2, DXGI_FORMAT_R32G32B32A32_FLOAT,
        };
        const PostProcessImage* source = &input;
        PostProcessImage* previous = nullptr;
        D3D12_VIEWPORT sourcePixels{ left, top, right - left, bottom - top, 0, 1 };
        constexpr std::size_t MAX_LEVELS = 32;
        for (std::size_t level = 0; level < MAX_LEVELS; ++level)
        {
            PostProcessImage* const target = m_targets.acquire(description);
            if (target == nullptr)
                return nullptr;
            const D3D12_VIEWPORT targetViewport{ 0, 0, static_cast<float>(description.width),
                static_cast<float>(description.height), 0, 1 };
            std::array<float, 16> parameters{};
            parameters[0] = level == 0 ? 1.0f : 0.0f;
            if (!draw(commandList,
                m_builtins[static_cast<std::size_t>(PostProcessBuiltin::AutoExposureDownsample)],
                *source, *target, sourcePixels, targetViewport, 1, false, parameters))
                return nullptr;
            if (previous && !m_targets.release(*previous))
                return nullptr;
            previous = target;
            source = target;
            sourcePixels = targetViewport;
            ++m_statistics.effectPasses;
            if (description.width == 1 && description.height == 1)
                return target;
            description.width = (description.width + 1) / 2;
            description.height = (description.height + 1) / 2;
        }
        LOG_ERROR("[PostProcess] Automatic exposure downsample exceeded its bounded mip chain.");
        return nullptr;
    }

    bool PostProcessPipeline::recordBloom(DX12CommandList& commandList, const PostProcessImage*& current,
        const PostProcessImage*& borrowed, D3D12_VIEWPORT& currentViewport, const PostProcessImage& output,
        const D3D12_VIEWPORT& viewport, const float intensity, const float threshold, const float scatter,
        const float sizeScale)
    {
        constexpr std::size_t MAX_LEVELS = 5;
        std::array<PostProcessImage*, MAX_LEVELS> levels{};
        std::array<D3D12_VIEWPORT, MAX_LEVELS> viewports{};
        std::array<PostProcessImage*, MAX_LEVELS * 2 + 1> trackedTargets{};
        const auto trackTarget = [&trackedTargets](PostProcessImage* const image)
        {
            for (auto& target : trackedTargets)
            {
                if (target == nullptr)
                {
                    target = image;
                    return true;
                }
            }
            return false;
        };
        const auto acquireTarget = [this, &trackTarget](const PostProcessTargetDescription& description)
        {
            auto* const image = m_targets.acquire(description);
            if (image != nullptr && !trackTarget(image))
            {
                if (!m_targets.release(*image))
                    return static_cast<PostProcessImage*>(nullptr);
                LOG_ERROR("[PostProcess] Bloom temporary target tracking capacity exceeded.");
                return static_cast<PostProcessImage*>(nullptr);
            }
            return image;
        };
        const auto releaseTarget = [this, &trackedTargets](const PostProcessImage& image)
        {
            for (auto& target : trackedTargets)
            {
                if (target == &image)
                {
                    if (!m_targets.release(image))
                        return false;
                    target = nullptr;
                    return true;
                }
            }
            LOG_ERROR("[PostProcess] Cannot return an untracked Bloom target.");
            return false;
        };
        const auto releaseTrackedTargets = [this, &trackedTargets]
        {
            bool succeeded = true;
            for (auto& target : trackedTargets)
            {
                if (target != nullptr)
                {
                    if (m_targets.release(*target))
                        target = nullptr;
                    else
                        succeeded = false;
                }
            }
            return succeeded;
        };
        const auto fail = [&releaseTrackedTargets]
        {
            if (!releaseTrackedTargets())
                LOG_ERROR("[PostProcess] Cannot release all temporary Bloom targets after recording failure.");
            return false;
        };
        const auto& fullSize = output.resource->getDescription();
        PostProcessTargetDescription description{
            static_cast<std::uint32_t>((fullSize.Width + 1) / 2), (fullSize.Height + 1) / 2 };
        std::size_t count = 0;
        levels[count] = acquireTarget(description);
        if (!levels[count])
            return fail();
        viewports[count] = temporaryViewport(viewport, description, fullSize);
        const std::array<float, 16> prefilterParameters{ threshold };
        if (!draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::BloomPrefilter)],
            *current, *levels[count], currentViewport, viewports[count], 1, false, prefilterParameters))
            return fail();
        ++count;
        ++m_statistics.effectPasses;

        for (; count < MAX_LEVELS; ++count)
        {
            const auto& sourceDescription = levels[count - 1]->resource->getDescription();
            if (sourceDescription.Width == 1 && sourceDescription.Height == 1)
                break;

            const PostProcessTargetDescription nextDescription{
                static_cast<std::uint32_t>((sourceDescription.Width + 1) / 2),
                (sourceDescription.Height + 1) / 2 };
            auto* const horizontal = acquireTarget({
                static_cast<std::uint32_t>(sourceDescription.Width),
                sourceDescription.Height });
            if (!horizontal)
                return fail();
            levels[count] = acquireTarget(nextDescription);
            if (!levels[count])
            {
                if (!releaseTarget(*horizontal))
                    return fail();
                return fail();
            }
            viewports[count] = temporaryViewport(viewport, nextDescription, fullSize);
            const std::array<float, 16> horizontalParameters{ 0.0f, sizeScale };
            const std::array<float, 16> verticalParameters{ 1.0f, sizeScale };
            const bool blurred = draw(commandList,
                m_builtins[static_cast<std::size_t>(PostProcessBuiltin::BloomDownsample)],
                *levels[count - 1], *horizontal, viewports[count - 1], viewports[count - 1],
                1, false, horizontalParameters)
                && draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::BloomDownsample)],
                    *horizontal, *levels[count], viewports[count - 1], viewports[count],
                    1, false, verticalParameters);
            if (!releaseTarget(*horizontal) || !blurred)
                return fail();
            m_statistics.effectPasses += 2;
        }
        m_statistics.bloomLevels = static_cast<std::uint32_t>(count);
        for (std::size_t index = count - 1; index > 0; --index)
        {
            const auto& highDescription = levels[index - 1]->resource->getDescription();
            auto* const next = acquireTarget({
                static_cast<std::uint32_t>(highDescription.Width), highDescription.Height });
            if (!next)
                return fail();
            const std::array<float, 16> parameters{ scatter };
            if (!draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::BloomUpsample)],
                *levels[index - 1], *next, viewports[index - 1], viewports[index - 1], 1, false, parameters,
                levels[index], &viewports[index])
                || !releaseTarget(*levels[index]) || !releaseTarget(*levels[index - 1]))
                return fail();
            ++m_statistics.effectPasses;
            levels[index - 1] = next;
        }
        auto* const composite = acquireTarget({
            static_cast<std::uint32_t>(fullSize.Width), fullSize.Height });
        if (!composite)
            return fail();
        const std::array<float, 16> parameters{ intensity };
        if (!draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::BloomComposite)],
            *current, *composite, currentViewport, viewport, 1, false, parameters,
            levels[0], &viewports[0])
            || !releaseTarget(*levels[0]) || (borrowed && !m_targets.release(*borrowed)))
            return fail();
        ++m_statistics.effectPasses;
        for (auto& target : trackedTargets)
        {
            if (target == composite)
            {
                target = nullptr;
                break;
            }
        }
        current = borrowed = composite;
        currentViewport = viewport;
        return true;
    }

    bool PostProcessPipeline::markSubmitted(const std::uint64_t fenceValue)
    {
        if (!m_recorded || !m_targets.markSubmitted(fenceValue))
        {
            LOG_ERROR("[PostProcess] Cannot mark an unrecorded pipeline submitted.");
            return false;
        }
        if (m_temporalHistoryRecorded)
        {
            m_temporalHistoryReadIndex = m_temporalHistoryPendingReadIndex;
            m_temporalHistoryValid = true;
            m_temporalHistoryRecorded = false;
        }
        for (auto* const pass : { m_acesLuts.get(), m_ocio.get(), m_cameraOcio.get() })
        {
            if (!pass || !pass->uploadRecorded)
                continue;
            pass->uploaded = true;
            pass->uploadRecorded = false;
            pass->uploadFenceValue = fenceValue;
            for (const auto& texture : pass->textures)
                std::vector<float>{}.swap(texture->pixels);
        }
        m_lastFenceValue = fenceValue;
        m_recorded = false;
        return true;
    }

    bool PostProcessPipeline::releaseTargets()
    {
        if (m_lastFenceValue != 0 && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        if (!m_targets.clear() || !releaseExposureHistory() || !releaseTemporalHistory())
            return false;
        m_statistics.temporaryTargets = 0;
        return true;
    }

    bool PostProcessPipeline::initializeExposureHistory()
    {
        if (m_exposureHistoryRtvHeap.get() != nullptr)
            return true;
        if (m_device == nullptr || m_srvHeap == nullptr
            || !m_exposureHistoryRtvHeap.initialize(*m_device, {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .capacity = static_cast<std::uint32_t>(m_exposureHistoryResources.size()),
            }))
        {
            LOG_ERROR("[PostProcess] Cannot initialize automatic exposure history descriptors.");
            return false;
        }

        for (std::size_t index = 0; index < m_exposureHistoryResources.size(); ++index)
        {
            const auto renderTarget = m_exposureHistoryRtvHeap.allocate();
            m_exposureHistorySrvs[index] = m_srvHeap->allocate();
            if (!renderTarget || !m_exposureHistorySrvs[index] || !m_exposureHistorySrvs[index]->gpu)
            {
                LOG_ERROR("[PostProcess] Cannot allocate automatic exposure history views.");
                releaseExposureHistory();
                return false;
            }
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = 1;
            description.Height = 1;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            description.SampleDesc = { 1, 0 };
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (!m_exposureHistoryResources[index].initialize(*m_device, {
                .description = description,
                .initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            }))
            {
                LOG_ERROR("[PostProcess] Cannot create automatic exposure history texture.");
                releaseExposureHistory();
                return false;
            }
            m_exposureHistoryRtvs[index] = renderTarget->cpu;
            m_device->CreateRenderTargetView(
                m_exposureHistoryResources[index].get(), nullptr, renderTarget->cpu.native);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = description.Format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(
                m_exposureHistoryResources[index].get(), &srv, m_exposureHistorySrvs[index]->cpu.native);
            m_exposureHistoryImages[index] = {
                &m_exposureHistoryResources[index], *m_exposureHistorySrvs[index]->gpu, renderTarget->cpu,
            };
        }
        return true;
    }

    bool PostProcessPipeline::releaseExposureHistory()
    {
        if (m_lastFenceValue != 0 && m_fence != nullptr && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        for (auto& srv : m_exposureHistorySrvs)
        {
            if (srv && m_srvHeap != nullptr && !m_srvHeap->release(srv->cpu.index))
            {
                LOG_ERROR("[PostProcess] Cannot release an automatic exposure history SRV.");
                return false;
            }
            srv.reset();
        }
        for (auto& resource : m_exposureHistoryResources)
            resource.finalize();
        m_exposureHistoryRtvHeap.finalize();
        m_exposureHistoryRtvs = {};
        m_exposureHistoryImages = {};
        m_exposureHistoryCamera = {};
        m_exposureHistoryReadIndex = 0;
        m_exposureHistoryValid = false;
        return true;
    }

    bool PostProcessPipeline::initializeTemporalHistory(const std::uint32_t width, const std::uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LOG_ERROR("[PostProcess] Cannot initialize temporal history with an empty viewport.");
            return false;
        }
        if (m_temporalHistoryWidth == width && m_temporalHistoryHeight == height
            && m_temporalHistoryRtvHeap.get() != nullptr)
            return true;
        if (!releaseTemporalHistory() || m_device == nullptr || m_srvHeap == nullptr
            || !m_temporalHistoryRtvHeap.initialize(*m_device, {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .capacity = static_cast<std::uint32_t>(m_temporalHistoryResources.size()),
            }))
        {
            LOG_ERROR("[PostProcess] Cannot initialize temporal anti-aliasing history.");
            return false;
        }

        for (std::size_t index = 0; index < m_temporalHistoryResources.size(); ++index)
        {
            const auto renderTarget = m_temporalHistoryRtvHeap.allocate();
            m_temporalHistorySrvs[index] = m_srvHeap->allocate();
            if (!renderTarget || !m_temporalHistorySrvs[index] || !m_temporalHistorySrvs[index]->gpu)
            {
                LOG_ERROR("[PostProcess] Cannot allocate temporal history views.");
                releaseTemporalHistory();
                return false;
            }
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = width;
            description.Height = height;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            description.SampleDesc = { 1, 0 };
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (!m_temporalHistoryResources[index].initialize(*m_device, {
                .description = description,
                .initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            }))
            {
                LOG_ERROR("[PostProcess] Cannot create temporal history texture.");
                releaseTemporalHistory();
                return false;
            }
            m_temporalHistoryRtvs[index] = renderTarget->cpu;
            m_device->CreateRenderTargetView(
                m_temporalHistoryResources[index].get(), nullptr, renderTarget->cpu.native);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = description.Format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(
                m_temporalHistoryResources[index].get(), &srv, m_temporalHistorySrvs[index]->cpu.native);
            m_temporalHistoryImages[index] = {
                &m_temporalHistoryResources[index], *m_temporalHistorySrvs[index]->gpu, renderTarget->cpu,
            };
        }
        m_temporalHistoryWidth = width;
        m_temporalHistoryHeight = height;
        return true;
    }

    bool PostProcessPipeline::releaseTemporalHistory()
    {
        if (m_lastFenceValue != 0 && m_fence != nullptr && !m_fence->waitOnCpu(m_lastFenceValue))
            return false;
        for (auto& srv : m_temporalHistorySrvs)
        {
            if (srv && m_srvHeap != nullptr && !m_srvHeap->release(srv->cpu.index))
            {
                LOG_ERROR("[PostProcess] Cannot release a temporal history SRV.");
                return false;
            }
            srv.reset();
        }
        for (auto& resource : m_temporalHistoryResources)
            resource.finalize();
        m_temporalHistoryRtvHeap.finalize();
        m_temporalHistoryRtvs = {};
        m_temporalHistoryImages = {};
        m_temporalHistoryCamera = {};
        m_temporalHistoryWidth = 0;
        m_temporalHistoryHeight = 0;
        m_temporalHistoryReadIndex = 0;
        m_temporalHistoryPendingReadIndex = 0;
        m_temporalHistoryValid = false;
        m_temporalHistoryRecorded = false;
        return true;
    }

    PostProcessImage* PostProcessPipeline::recordExposureAdaptation(
        DX12CommandList& commandList, const PostProcessImage& averageLogLuminance,
        const D3D12_VIEWPORT& viewport, const PostProcessSettings& settings, const float deltaTimeSeconds)
    {
        if (!initializeExposureHistory())
            return nullptr;
        const std::size_t writeIndex = 1 - m_exposureHistoryReadIndex;
        const PostProcessImage& previous = m_exposureHistoryImages[m_exposureHistoryReadIndex];
        auto& output = m_exposureHistoryImages[writeIndex];
        const std::array<float, 16> parameters{
            deltaTimeSeconds,
            settings.autoExposureSpeedUp.value,
            settings.autoExposureSpeedDown.value,
            settings.autoExposureKey.value,
            settings.autoExposureMinEV.value,
            settings.autoExposureMaxEV.value,
            settings.autoExposureCompensation.value,
            m_exposureHistoryValid ? 1.0f : 0.0f,
        };
        if (!draw(commandList, m_builtins[static_cast<std::size_t>(PostProcessBuiltin::AutoExposureAdapt)],
            averageLogLuminance, output, viewport, viewport, 1, false, parameters, &previous, &viewport)
            || !output.resource->transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            || !m_targets.release(averageLogLuminance))
            return nullptr;
        m_exposureHistoryReadIndex = writeIndex;
        m_exposureHistoryValid = true;
        ++m_statistics.effectPasses;
        return &output;
    }
}
