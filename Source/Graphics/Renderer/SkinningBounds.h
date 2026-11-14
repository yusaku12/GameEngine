#pragma once

#include <bitset>
#include <span>
#include <vector>
#include "Graphics\Renderer\SkinningPaletteSnapshot.h"

namespace Engine
{
    /**
     * @brief ボーンごとの頂点範囲を保持し、現在のGPUスキニングを包含するWorld AABBを算出する。
     * @details 通常は非負ウェイト用キャッシュを使用し、失敗時のみcalculateVerticesで頂点を評価できる。
     * @thread_safety initialize requires external synchronization. Calculation methods are read-only and thread-safe.
     */
    class SkinningBounds
    {
    public:
        /**
         * @brief Mesh頂点からボーン影響範囲と非スキニング頂点範囲を構築する。
         * @param vertices GPUへ提出するMesh頂点。
         * @return 成功時true。不正な頂点・ウェイトではログを出してfalse。
         */
        bool initialize(std::span<const ModelVertex> vertices);

        /**
         * @brief 描画と同じPaletteとMesh World行列から保守的なBoundsを算出する。
         * @param palette 描画に使用する不変スキニングSnapshot。
         * @param world Mesh Node変換を含むWorld行列。
         * @param bounds 成功時に書き込むWorld AABB。失敗時は変更しない。
         * @return 成功時true。失敗時はログを出す。呼び出し元はカリングを回避すること。
         */
        bool calculate(const SkinningPaletteSnapshot& palette, const Matrix& world, AABB& bounds) const;

        /**
         * @brief calculate失敗時にGPUの頂点演算を区間評価する低速フォールバック。
         * @details initialize不要。有限の負ウェイト、閾値分岐、丸め、非アフィン行列も包含する。
         * 時間計算量O(vertices.size())、追加メモリO(1)。証明不能な演算は打ち切って失敗する。
         * Model.hlslの現在位置専用で、法線や前フレーム位置の検証は行わない。
         * @param vertices GPUへ提出するMesh頂点。
         * @param palette 描画に使用する不変スキニングSnapshot。
         * @param world Mesh Node変換を含むWorld行列。
         * @param bounds 成功時に書き込むWorld AABB。失敗時は変更しない。
         * @return 保守的な有限float範囲を証明できればtrue。それ以外はログを出してfalse。
         */
        bool calculateVertices(std::span<const ModelVertex> vertices, const SkinningPaletteSnapshot& palette,
            const Matrix& world, AABB& bounds) const;

    private:
        struct Range
        {
            Vector3 minimum;
            Vector3 maximum;
            bool populated = false;
            void include(const Vector3& point) noexcept;
        };
        struct Influence
        {
            Range range;
            std::uint32_t bone = 0;
        };
        std::vector<Influence> m_influences;
        std::bitset<MAX_SKINNING_BONES> m_referencedBones;
        Range m_rigid;
        bool m_initialized = false;
    };
}
