#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief スケルトンを構成するボーン。
     */
    struct Bone
    {
        std::uint32_t index = 0;                      //!< ボーンのインデックス。
        std::string name;                             //!< ボーンの名前。
        std::int32_t parentIndex = -1;                //!< 親ボーンのインデックス。-1の場合はルートボーン。
        Matrix inverseBindPose = Matrix::Identity;    //!< ボーンの逆バインドポーズ行列。
        Matrix localBindTransform = Matrix::Identity; //!< ボーンのローカルバインド変換行列。
    };

    /**
     * @brief モデルのCPU側スケルトンデータ。
     */
    struct SkeletonResource
    {
        std::vector<Bone> bones;                                //!< ボーンの配列。
        std::unordered_map<std::string, std::uint32_t> boneMap; //!< ボーン名からインデックスへのマップ。
    };
} // namespace Engine
