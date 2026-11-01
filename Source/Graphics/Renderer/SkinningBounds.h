#pragma once

#include <bitset>
#include <span>
#include <vector>
#include "Graphics\Renderer\SkinningPaletteSnapshot.h"

namespace Engine
{
    /**
     * @brief ボーンごとの頂点範囲を保持し、現在のGPUスキニングを包含するWorld AABBを算出する。
     * @details 非負ウェイトを正規化するModel Shader専用。Meshの不変頂点から一度だけ構築する。
     * @thread_safety initialize requires external synchronization. calculate is read-only and thread-safe.
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
