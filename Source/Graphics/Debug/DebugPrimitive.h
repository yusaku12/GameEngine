#pragma once

#include <span>

#include "Core\CoreDefines.h"
#include "Core\Math\MathTypes.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\DirectX12\Resource\Resource.h"

namespace Engine
{
    class DX12CommandList;
    class DX12Device;
    class DX12Fence;
    class DX12Shader;
    struct CameraFrustum;

    /**
     * @brief 線分を一括転送して描画するデバッグプリミティブサービス。
     * @details 描画要求は任意スレッドから提出でき、線分は一括転送、形状はインスタンシング描画する。
     * @thread_safety Draw API is thread-safe. Lifecycle and render API are render-thread only.
     */
    class DebugPrimitive
    {
    public:

        /** @brief ワールド空間の線分とその色。 */
        struct Line
        {
            Vector3 start;
            Vector3 end;
            Color color;
        };

        /**
         * @brief Singletonを取得する。
         */
        static DebugPrimitive& instance() noexcept;

        GE_DISABLE_COPY_AND_MOVE(DebugPrimitive);

        /**
         * @brief 線分を提出する。
         * @param start 線分の始点
         * @param end 線分の終点
         * @param color 線分の色 (デフォルトは緑)
         */
        void drawLine(const Vector3& start, const Vector3& end, const Color& color = Color(0.0f, 1.0f, 0.0f, 1.0f));

        /**
         * @brief 複数の線分を一度に提出する。不正な線分はログに記録して除外する。
         * @param lines 提出する線分。
         */
        void drawLines(std::span<const Line> lines);

        /**
         * @brief XZ平面のGridを提出する。
         * @param center Gridの中心座標
         * @param width Gridの幅
         * @param depth Gridの奥行き
         * @param step Gridの間隔 (デフォルトは1.0f)
         * @param color Gridの色 (デフォルトは灰色)
         */
        void drawGrid(const Vector3& center, float width, float depth, float step = 1.0f, const Color& color = Color(0.5f, 0.5f, 0.5f, 1.0f));

        /**
         * @brief Wire Sphereを提出する。
         * @param world Sphereのワールド行列
         * @param radius Sphereの半径
         * @param color Sphereの色 (デフォルトは緑)
         */
        void drawSphere(const Matrix& world, float radius, const Color& color = Color(0.0f, 1.0f, 0.0f, 1.0f));

        /**
         * @brief extentsを半径とするWire Boxを提出する。
         * @param world Boxのワールド行列
         * @param extents Boxの各辺の長さの半分
         * @param color Boxの色 (デフォルトは緑)
         */
        void drawBox(const Matrix& world, const Vector3& extents, const Color& color = Color(0.0f, 1.0f, 0.0f, 1.0f));

        /**
         * @brief Y軸方向のWire Cylinderを提出する。
         * @param world Cylinderのワールド行列
         * @param radius Cylinderの半径
         * @param height Cylinderの高さ
         * @param color Cylinderの色 (デフォルトは緑)
         */
        void drawCylinder(const Matrix& world, float radius, float height, const Color& color = Color(0.0f, 1.0f, 0.0f, 1.0f));

        /**
         * @brief Y軸方向のWire Capsuleを提出する。
         * @param world Capsuleのワールド行列
         * @param radius Capsuleの半径
         * @param halfHeight Capsuleの半分の高さ
         * @param color Capsuleの色 (デフォルトは緑)
         */
        void drawCapsule(const Matrix& world, float radius, float halfHeight, const Color& color = Color(0.0f, 1.0f, 0.0f, 1.0f));

        /**
         * @brief 未描画の要求を破棄する。
         */
        void clear() noexcept;

    private:

        friend class DX12Renderer;

        /**
         * @brief 描画要求を消費して頂点バッファを構築する。
         */
        struct Vertex
        {
            Vector3 position; //!< 頂点座標
            Color color;      //!< 頂点色
        };

        struct ShapeVertex
        {
            Vector3 position;
            float heightOffset;
        };

        struct ShapeInstance
        {
            Matrix world;
            Color color;
            Vector4 dimensions;
        };

        enum class ShapeKind : std::size_t
        {
            Sphere,
            Box,
            Cylinder,
            Capsule,
            Count
        };

        struct ShapeRange
        {
            std::uint32_t firstVertex = 0;
            std::uint32_t vertexCount = 0;
            std::uint32_t firstInstance = 0;
            std::uint32_t instanceCount = 0;
        };

        /**
         * @brief 描画要求の構造体
         */
        using LineRequest = Line;
        struct GridRequest { Vector3 center; float width; float depth; float step; Color color; };
        struct SphereRequest { Matrix world; float radius; Color color; };
        struct BoxRequest { Matrix world; Vector3 extents; Color color; };
        struct CylinderRequest { Matrix world; float radius; float height; Color color; };
        struct CapsuleRequest { Matrix world; float radius; float halfHeight; Color color; };

        /**
         * @brief 描画要求の集合
         */
        struct Requests
        {
            std::vector<LineRequest> lines;
            std::vector<GridRequest> grids;
            std::vector<SphereRequest> spheres;
            std::vector<BoxRequest> boxes;
            std::vector<CylinderRequest> cylinders;
            std::vector<CapsuleRequest> capsules;

            /**
             * @brief 描画要求が空かを取得する
             * @return 空の場合は true
             */
            bool empty() const noexcept;

            /**
             * @brief 描画要求を破棄する
             */
            void clear() noexcept;

            /**
             * @brief 描画要求を他の Requests に移動する
             * @param other 移動先の Requests
             */
            void swap(Requests& other) noexcept;
        };

        //! フレーム別バッファ数
        static constexpr std::uint32_t FRAME_COUNT = 2;

        //! 円弧を描画する際の分割数
        static constexpr std::uint32_t CURVE_SEGMENTS = 24;

        //! 初期頂点バッファ容量 (頂点数)
        static constexpr std::size_t INITIAL_VERTEX_CAPACITY = 65536;

        DebugPrimitive();
        ~DebugPrimitive() = default;

        /**
         * @brief DirectX 12 デバイスと Fence を設定して初期化する
         * @param device 描画に使用する DirectX 12 デバイス
         * @param fence 描画完了を待機する Fence
         * @return 初期化に成功した場合は true
         */
        bool initialize(DX12Device& device, const DX12Fence& fence);

        /**
         * @brief 保持している DirectX 12 オブジェクトを解放する
         */
        bool finalize();

        /**
         * @brief パイプラインステートを構築する
         * @param vertexShader 頂点シェーダー
         * @param shapeVertexShader インスタンシング用頂点シェーダー
         * @param pixelShader ピクセルシェーダー
         * @return 構築に成功した場合は true
         */
        bool rebuildPipeline(const DX12Shader& vertexShader, const DX12Shader& shapeVertexShader,
            const DX12Shader& pixelShader);

        /**
         * @brief 描画要求を消費して頂点バッファを構築し、描画する
         * @param commandList 描画に使用するコマンドリスト
         * @param frameIndex 描画対象のフレームインデックス
         * @param viewProjection ビュー射影行列
         * @param frustum 描画に使用するビュー射影行列の視錐台
         * @param enabled 描画可能なCameraがある場合は true
         * @return 描画に成功した場合は true
         */
        bool render(
            DX12CommandList& commandList,
            std::uint32_t frameIndex,
            const Matrix& viewProjection,
            const CameraFrustum& frustum,
            bool enabled);

        /**
         * @brief フレームインデックスに対応する頂点バッファを使用済みにする
         * @param frameIndex 使用済みにするフレームインデックス
         * @param fenceValue 使用済みとする Fence 値
         * @return 使用済みにできた場合は true
         */
        bool markFrameUsed(std::uint32_t frameIndex, std::uint64_t fenceValue);

        /**
         * @brief 描画要求を消費して頂点バッファを構築する
         */
        void consumeRequests();

        /**
         * @brief 描画要求を線分頂点と形状インスタンスに変換する。
         * @param frustum 描画に使用する視錐台。
         * @return 変換に成功した場合はtrue。
         */
        bool buildVertices(const CameraFrustum& frustum);

        /**
         * @brief 再利用する形状頂点を作成してGPUへ転送する。
         * @return 作成に成功した場合はtrue。
         */
        bool initializeShapeVertices();

        /**
         * @brief フレームインデックスに対応する頂点バッファの容量を確保する
         * @param frameIndex 対象のフレームインデックス
         * @param vertexCount 確保する頂点数
         * @return 確保に成功した場合は true
         */
        bool ensureFrameCapacity(std::uint32_t frameIndex, std::size_t vertexCount);

        /**
         * @brief 指定フレームのインスタンスバッファ容量を確保する。
         * @param frameIndex 対象フレーム。
         * @param instanceCount 必要なインスタンス数。
         * @return 確保に成功した場合はtrue。
         */
        bool ensureInstanceCapacity(std::uint32_t frameIndex, std::size_t instanceCount);

        /**
         * @brief 線分を頂点バッファに展開する
         * @param start 線分の始点
         * @param end 線分の終点
         * @param color 線分の色
         */
        void appendLine(const Vector3& start, const Vector3& end, const Color& color);

        DX12Device* m_device = nullptr;                            //!< 描画に使用する DirectX 12 デバイス
        const DX12Fence* m_fence = nullptr;                        //!< 描画完了を待機する Fence
        DX12GraphicsPipeline m_pipeline;                           //!< DirectX 12 グラフィックスパイプライン
        DX12GraphicsPipeline m_shapePipeline;                      //!< インスタンシング描画用パイプライン
        DX12UploadBuffer m_shapeVertices;                          //!< 全フレームで共有する形状頂点
        std::array<DX12UploadBuffer, FRAME_COUNT> m_vertexBuffers; //!< 頂点バッファ (Upload Heap)
        std::array<DX12UploadBuffer, FRAME_COUNT> m_instanceBuffers; //!< フレーム別インスタンスバッファ
        std::array<std::size_t, FRAME_COUNT> m_vertexCapacities{}; //!< 頂点バッファの容量 (頂点数)
        std::array<std::size_t, FRAME_COUNT> m_instanceCapacities{}; //!< インスタンスバッファの容量
        std::array<bool, FRAME_COUNT> m_frameUsed{};               //!< フレームインデックスに対応する頂点バッファが使用済みか
        std::array<bool, FRAME_COUNT> m_shapeFrameUsed{};          //!< インスタンスバッファが使用済みか
        std::array<ShapeRange, static_cast<std::size_t>(ShapeKind::Count)> m_shapeRanges{}; //!< 形状ごとの範囲
        std::mutex m_mutex;                                        //!< 描画要求のスレッドセーフなアクセス用ミューテックス
        Requests m_pending;                                        //!< 描画要求の保留リスト (任意スレッドから提出される)
        Requests m_renderRequests;                                 //!< 描画要求のレンダリング用リスト (レンダースレッドから消費される)
        std::vector<Vertex> m_vertices;                            //!< 頂点バッファに展開する頂点リスト
        std::vector<ShapeInstance> m_instances;                    //!< 形状ごとに並べたインスタンス
    };
} // namespace Engine