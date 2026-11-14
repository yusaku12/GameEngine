#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "Graphics\PostProcess\PostProcessTargetPool.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\PostProcess\PostProcessSettings.h"

namespace Engine
{
    /** @brief Built-in shader slots; shared by shader registration, PSO rebuilding and execution. */
    enum class PostProcessBuiltin : std::size_t
    {
        CustomComposite, Vignette,
        ChromaticAberration, FilmGrain, Fxaa,
        DepthOfField, AutoExposureAdapt, TemporalAA, MotionBlur, Count,
    };
    inline constexpr std::array POST_PROCESS_BUILTIN_ENTRIES{
        "psCustomComposite", "psVignette", "psChromaticAberration", "psFilmGrain",
        "psFxaa", "psDepthOfField", "psAutoExposureAdapt", "psTemporalAA", "psMotionBlur",
    };
    static_assert(POST_PROCESS_BUILTIN_ENTRIES.size() == static_cast<std::size_t>(PostProcessBuiltin::Count));
    using PostProcessBuiltinShaders = std::array<std::shared_ptr<const DX12Shader>, POST_PROCESS_BUILTIN_ENTRIES.size()>;

    /** @brief Bloom compute stages, shared by shader registration and execution. */
    enum class PostProcessBloom : std::size_t { Prefilter, Downsample, Upsample, Composite, Count };
    inline constexpr std::array POST_PROCESS_BLOOM_ENTRIES{
        "csBloomPrefilter", "csBloomDownsample", "csBloomUpsample", "csBloomComposite",
    };
    static_assert(POST_PROCESS_BLOOM_ENTRIES.size() == static_cast<std::size_t>(PostProcessBloom::Count));
    using PostProcessBloomShaders = std::array<std::shared_ptr<const DX12Shader>, POST_PROCESS_BLOOM_ENTRIES.size()>;

    /**
     * @brief Color入力1枚・出力1枚のFullscreenパス定義。
     * @details ShaderはPostProcess.hlsliのRoot Signature契約に従う。出力はlinear RGBA16F。
     * Shaderの共有Snapshotは所有し、GPU PSOはPipelineが単独所有する。
     */
    struct PostProcessPassDescription
    {
        std::string name; //!< Diagnostic pass name.
        PostProcessStage stage = PostProcessStage::BeforeToneMapping; //!< Linear input/output domain.
        std::shared_ptr<const DX12Shader> pixelShader; //!< Immutable bytecode snapshot.
        std::array<float, 16> parameters{}; //!< b1に渡す16個の32-bit値。
        std::uint32_t resolutionDivisor = 1; //!< Render Target寸法の縮小率。1以上16以下。
        bool enabled = true; //!< Disabled passes do not draw or acquire targets.
    };

    /** @brief OCIO GPU LUT texture dimensionality. */
    enum class PostProcessOcioTextureDimension
    {
        Texture1D, //!< One-dimensional LUT.
        Texture2D, //!< Two-dimensional LUT.
        Texture3D, //!< Three-dimensional LUT.
    };

    /** @brief One OCIO-generated LUT and its explicitly assigned HLSL bindings. */
    struct PostProcessOcioTexture
    {
        PostProcessOcioTextureDimension dimension = PostProcessOcioTextureDimension::Texture2D; //!< Texture type.
        std::uint32_t width = 0; //!< Width in texels.
        std::uint32_t height = 1; //!< Height in texels; one for 1D.
        std::uint32_t depth = 1; //!< Depth in texels; one for 1D and 2D.
        std::uint32_t channels = 3; //!< OCIO values are tightly packed R or RGB floats.
        std::uint32_t textureRegister = 1; //!< t0 is reserved for the source image.
        std::uint32_t samplerRegister = 1; //!< s0 is reserved for the source image.
        bool linearInterpolation = true; //!< Linear sampling; false selects point sampling.
        std::vector<float> values; //!< Tightly packed texels, with X varying fastest.
    };

    /**
     * @brief A compiled OCIO fullscreen shader and the LUTs referenced by it.
     * @details The wrapper uses t0/s0 for the input image, b0 for the standard
     * PostProcess frame constants and b1 for the 16 supplied float constants.
     * LUT registers must agree with the compiled shader's bindings. The shader
     * must handle source/target viewport differences using the b0 constants.
     */
    struct PostProcessOcioDescription
    {
        std::shared_ptr<const DX12Shader> pixelShader; //!< Compiled fullscreen shader snapshot.
        std::vector<PostProcessOcioTexture> textures; //!< LUTs and their bindings.
        std::array<float, 16> parameters{}; //!< b1 constants.
        PostProcessStage stage = PostProcessStage::BeforeToneMapping; //!< HDR or linear LDR.
        float contribution = 1.0f; //!< Blending strength in [0,1].
    };

    /** @brief 直近の記録に対する統計。無効パスのDraw/Dispatch/Target取得は含めない。 */
    struct PostProcessStatistics
    {
        std::uint32_t effectPasses = 0; //!< Recorded optional color passes.
        std::uint32_t drawCalls = 0; //!< Optional passes and required output conversion.
        std::uint32_t dispatchCalls = 0; //!< Compute dispatches.
        std::size_t temporaryTargets = 0; //!< Pool textures, including GPU-pending entries.
        std::uint32_t bloomLevels = 0; //!< Recorded levels of the bounded Bloom pyramid.
        std::uint32_t filmGrainFrameIndex = 0; //!< Temporal grain seed used for the last recording.
    };

    /**
     * @brief HDR/LDRのCompute・Fullscreenパス列と必須の表示変換を実行する。
     * @details 空の列はTone Mapping+sRGB変換を融合し、追加Textureを確保しない。
     * @thread_safety Render thread only. Device/Fence/SRV heap must outlive this object.
     */
    class PostProcessPipeline
    {
    public:
        PostProcessPipeline() = default;
        ~PostProcessPipeline();
        GE_DISABLE_COPY_AND_MOVE(PostProcessPipeline);

        /** @brief Device/Fence/Heapへ接続する。Shader/PSOはrebuildで設定する。 */
        bool initialize(ID3D12Device& device, const DX12Fence& fence, DX12DescriptorHeap& srvHeap);
        /** @brief GPU完了後にすべてのリソースを解放する。 */
        bool finalize();
        /** @brief 共通VS・必須出力・組み込みShaderを置換する。失敗時は前回PSOを保持する。 */
        bool rebuild(std::shared_ptr<const DX12Shader> vertex,
            std::shared_ptr<const DX12Shader> toneMapping, std::shared_ptr<const DX12Shader> output,
            const PostProcessBuiltinShaders& builtins, const PostProcessBloomShaders& bloom,
            std::shared_ptr<const DX12Shader> lightShafts,
            std::shared_ptr<const DX12Shader> lightShaftsFallback,
            std::shared_ptr<const DX12Shader> autoExposure);
        /** @brief パス列をSnapshotとして設定する。失敗時は既存列を保持する。GPU完了を待つ。 */
        bool configure(std::span<const PostProcessPassDescription> descriptions);
        /**
         * @brief OCIO ShaderとLUTを設定する。nullptrは設定解除。
         * @details 設定値はコピーされ、最初の描画時にLUTをUploadする。
         * 失敗時は既存の設定を保持する。置換・解除時はGPU完了を待つ。
         * @param description コンパイル済みShaderとLUT。nullptrなら解除。
         * @return 設定または解除に成功した場合はtrue。
         */
        bool configureOcio(const PostProcessOcioDescription* description);
        /**
         * @brief HDR入力から最終sRGB画像へ記録する。input/outputの同一Resourceは不可。
         * @details 記録失敗時は当該コマンド列を提出せず破棄してfinalizeすること。
         * 必須色変換は最終出力だけに適用する。AfterToneMapping入力はLinear LDR。
         * Camera Weightは中立設定から補間する。FXAAのみ処理前後の画像を補間する。
         */
        bool record(DX12CommandList& commandList, const PostProcessImage& input,
            const PostProcessImage& output, const D3D12_VIEWPORT& viewport,
            const PostProcessCameraSnapshot& camera = {}, const PostProcessImage* depth = nullptr,
            const PostProcessImage* motionVectors = nullptr);
        /** @brief 記録した全パスのPSO/一時画像へ提出Fenceを関連付ける。 */
        bool markSubmitted(std::uint64_t fenceValue);
        /** @brief Resize時に一時画像を解放する。パス定義とPSOは保持する。 */
        bool releaseTargets();
        /** @brief 新しいフレームの統計をクリアする。Forward/debug迂回はDraw/Dispatch数0として報告する。 */
        void resetStatistics() noexcept { m_statistics = {}; }
        /** @brief 直近の記録統計。 */
        const PostProcessStatistics& statistics() const noexcept { return m_statistics; }

    private:
        struct Pass
        {
            PostProcessPassDescription description;
            DX12GraphicsPipeline pipeline;
        };
        struct CustomPass
        {
            std::shared_ptr<const DX12Shader> shader;
            DX12GraphicsPipeline pipeline;
        };
        struct OcioTexture
        {
            DX12Resource resource;
            Microsoft::WRL::ComPtr<ID3D12Resource> upload;
            std::optional<DX12DescriptorAllocation> srv;
            std::vector<float> pixels;
            std::uint32_t textureRegister = 0;
            std::uint32_t samplerRegister = 0;
            bool linearInterpolation = true;
            std::uint64_t rowPitch = 0;
            std::uint64_t slicePitch = 0;
        };
        struct OcioPass
        {
            ~OcioPass();
            DX12DescriptorHeap* heap = nullptr;
            std::shared_ptr<const DX12Shader> shader;
            std::vector<std::unique_ptr<OcioTexture>> textures;
            std::array<float, 16> parameters{};
            PostProcessStage stage = PostProcessStage::BeforeToneMapping;
            float contribution = 1;
            DX12GraphicsPipeline pipeline;
            std::uint64_t uploadFenceValue = 0;
            bool uploadRecorded = false;
            bool uploaded = false;
        };
        bool createOcioPipeline(DX12GraphicsPipeline& pipeline, const DX12Shader& vertex,
            const OcioPass& pass);
        bool configureOcioPass(const PostProcessOcioDescription* description,
            std::unique_ptr<OcioPass>& destination, bool lutOnly = false);
        bool uploadOcioTextures(DX12CommandList& commandList, OcioPass& pass);
        DX12GraphicsPipeline* getCustomPipeline(const std::shared_ptr<const DX12Shader>& shader);
        bool createPipeline(DX12GraphicsPipeline& pipeline, const DX12Shader& vertex,
            const DX12Shader& pixel, DXGI_FORMAT format, bool outputTransform = false);
        bool createComputePipeline(DX12ComputePipeline& pipeline, const DX12Shader& shader);
        bool createPasses(std::span<const PostProcessPassDescription> descriptions,
            const DX12Shader& vertex, std::vector<std::unique_ptr<Pass>>& destination);
        bool draw(DX12CommandList& commandList, const DX12GraphicsPipeline& pipeline,
            const PostProcessImage& input, const PostProcessImage& output,
            const D3D12_VIEWPORT& sourceViewport, const D3D12_VIEWPORT& targetViewport,
            float exposure, bool toneMapping, std::span<const float> parameters,
            const PostProcessImage* secondary = nullptr, const D3D12_VIEWPORT* secondaryViewport = nullptr,
            const DX12GpuDescriptorHandle* externalSecondary = nullptr,
            const PostProcessImage* tertiary = nullptr, const OcioPass* ocio = nullptr,
            bool outputTransform = false);
        bool dispatchCompute(DX12CommandList& commandList, const DX12ComputePipeline& pipeline,
            const PostProcessImage& input, const PostProcessImage& output,
            const D3D12_VIEWPORT& sourceViewport, const D3D12_VIEWPORT& targetViewport,
            std::span<const float> parameters, const PostProcessImage* secondary = nullptr,
            const D3D12_VIEWPORT* secondaryViewport = nullptr, bool groupPerPixel = false);
        PostProcessImage* recordAutoExposure(DX12CommandList& commandList,
            const PostProcessImage& input, const D3D12_VIEWPORT& sourceViewport);
        bool initializeExposureHistory();
        bool releaseExposureHistory();
        bool initializeTemporalHistory(std::uint32_t width, std::uint32_t height);
        bool releaseTemporalHistory();
        PostProcessImage* recordExposureAdaptation(DX12CommandList& commandList,
            const PostProcessImage& averageLogLuminance, const D3D12_VIEWPORT& viewport,
            const PostProcessSettings& settings, float deltaTimeSeconds);
        bool recordBloom(DX12CommandList& commandList, const PostProcessImage*& current,
            const PostProcessImage*& borrowed, D3D12_VIEWPORT& currentViewport,
            const PostProcessImage& output, const D3D12_VIEWPORT& viewport, float intensity,
            float threshold, float scatter, float sizeScale);

        ID3D12Device* m_device = nullptr;
        const DX12Fence* m_fence = nullptr;
        DX12DescriptorHeap* m_srvHeap = nullptr;
        PostProcessTargetPool m_targets;
        std::array<DX12Resource, 2> m_exposureHistoryResources;
        std::array<PostProcessImage, 2> m_exposureHistoryImages{};
        std::array<std::optional<DX12DescriptorAllocation>, 2> m_exposureHistorySrvs;
        DX12DescriptorHeap m_exposureHistoryRtvHeap;
        std::array<DX12CpuDescriptorHandle, 2> m_exposureHistoryRtvs{};
        ObjectGUID m_exposureHistoryCamera;
        std::size_t m_exposureHistoryReadIndex = 0;
        bool m_exposureHistoryValid = false;
        std::array<DX12Resource, 2> m_temporalHistoryResources;
        std::array<PostProcessImage, 2> m_temporalHistoryImages{};
        std::array<std::optional<DX12DescriptorAllocation>, 2> m_temporalHistorySrvs;
        DX12DescriptorHeap m_temporalHistoryRtvHeap;
        std::array<DX12CpuDescriptorHandle, 2> m_temporalHistoryRtvs{};
        ObjectGUID m_temporalHistoryCamera;
        std::uint32_t m_temporalHistoryWidth = 0;
        std::uint32_t m_temporalHistoryHeight = 0;
        std::size_t m_temporalHistoryReadIndex = 0;
        std::size_t m_temporalHistoryPendingReadIndex = 0;
        bool m_temporalHistoryValid = false;
        bool m_temporalHistoryRecorded = false;
        std::uint32_t m_filmGrainFrameIndex = 0;
        std::shared_ptr<const DX12Shader> m_vertexShader;
        DX12GraphicsPipeline m_toneMapping;
        DX12GraphicsPipeline m_output;
        std::array<DX12GraphicsPipeline, POST_PROCESS_BUILTIN_ENTRIES.size()> m_builtins;
        std::array<DX12ComputePipeline, POST_PROCESS_BLOOM_ENTRIES.size()> m_bloom;
        DX12ComputePipeline m_lightShafts;
        DX12ComputePipeline m_autoExposure;
        DX12GraphicsPipeline m_lightShaftsFallback;
        std::vector<std::unique_ptr<Pass>> m_passes;
        std::unordered_map<const DX12Shader*, std::unique_ptr<CustomPass>> m_customPipelines;
        std::unique_ptr<OcioPass> m_acesLuts;
        std::unique_ptr<OcioPass> m_ocio;
        std::unique_ptr<OcioPass> m_cameraOcio;
        std::shared_ptr<const PostProcessOcioDescription> m_cameraOcioSnapshot;
        std::uint64_t m_lastFenceValue = 0;
        bool m_recorded = false;
        PostProcessStatistics m_statistics;
    };
}
