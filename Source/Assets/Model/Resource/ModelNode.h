#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief モデルノードの階層とメッシュ参照。
     */
    struct ModelNode
    {
        std::string name;                         //!< ノード名
        std::int32_t parentIndex = -1;            //!< 親ノードのインデックス。-1の場合はルートノード。
        Matrix localTransform = Matrix::Identity; //!< ローカル変換行列
        std::vector<std::uint32_t> children;      //!< 子ノードのインデックス
        std::vector<std::uint32_t> meshIndices;   //!< メッシュのインデックス
    };
} // namespace Engine
