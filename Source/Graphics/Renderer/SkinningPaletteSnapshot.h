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
        std::array<Matrix, MAX_SKINNING_BONES> boneMatrices;
        std::array<Matrix, MAX_SKINNING_BONES> boneNormalMatrices;
    };

    struct SkinningPaletteSnapshot
    {
        AssetGUID skeletonGuid;
        SkeletonSignature skeletonSignature;
        SkinningPaletteConstants constants;
        std::uint32_t jointCount = 0;
    };
}
