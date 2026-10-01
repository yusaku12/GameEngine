#pragma once

#include <array>
#include "Assets\Animation\AnimationTypes.h"
#include "Assets\Model\Resource\MeshResource.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief Game updateからRender threadへ渡す不変Skinning Palette。
     * @thread_safety 公開後はimmutable。生成中はAnimatorInstanceだけが書き込む。
     */
    struct SkinningPaletteConstants
    {
        std::array<Matrix, MAX_SKINNING_BONES> boneMatrices;       //!< ボーン変換用の行列。スケーリングが入ると法線ベクトルの変換に必要になる。
        std::array<Matrix, MAX_SKINNING_BONES> boneNormalMatrices; //!< 法線変換用の行列。スケーリングが入ると法線ベクトルの変換に必要になる。
    };

    /**
     * @brief Game updateからRender threadへ渡す不変Skinning Paletteのスナップショット。
     * @thread_safety 公開後はimmutable。生成中はAnimatorInstanceだけが書き込む。
     */
    struct SkinningPaletteSnapshot
    {
        AssetGUID skeletonGuid;              //!< スケルトンのGUID。スケルトンが変わるとAnimatorInstanceは新しいSkinningPaletteSnapshotを生成する。
        SkeletonSignature skeletonSignature; //!< スケルトンの署名。スケルトンが変わるとAnimatorInstanceは新しいSkinningPaletteSnapshotを生成する。
        SkinningPaletteConstants constants;  //!< スキニングパレットの定数バッファ
        std::uint32_t jointCount = 0;        //!< スケルトンのジョイント数。スケルトンが変わるとAnimatorInstanceは新しいSkinningPaletteSnapshotを生成する。
    };
}
