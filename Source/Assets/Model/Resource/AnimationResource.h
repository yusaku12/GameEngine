#pragma once

#include <string>
#include <vector>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief 位置キー。
     */
    struct AnimationKeyPosition
    {
        Vector3 value = Vector3(0.0f, 0.0f, 0.0f); //!< 位置値。
        float time = 0.0f;                         //!< キー時刻（秒単位）。
    };

    /**
     * @brief 回転キー。
     */
    struct AnimationKeyRotation
    {
        Quaternion value = Quaternion::Identity; //!< 回転値。
        float time = 0.0f;                       //!< キー時刻（秒単位）。
    };

    /**
     * @brief スケールキー。
     */
    struct AnimationKeyScale
    {
        Vector3 value = Vector3(1.0f, 1.0f, 1.0f); //!< スケール値。
        float time = 0.0f;                         //!< キー時刻（秒単位）。
    };

    /**
     * @brief ノード単位のアニメーションチャンネル。
     */
    struct AnimationChannel
    {
        std::string nodeName;                        //!< アニメーション対象ノード名。
        std::vector<AnimationKeyPosition> positions; //!< 位置キー。
        std::vector<AnimationKeyRotation> rotations; //!< 回転キー。
        std::vector<AnimationKeyScale> scales;       //!< スケールキー。
    };

    /**
     * @brief モデルのCPU側アニメーションデータ。
     * durationとキー時刻は秒単位で保持する。
     */
    struct AnimationResource
    {
        std::string name;                       //!< アニメーション名。
        float duration = 0.0f;                  //!< アニメーションの長さ（秒単位）。
        float ticksPerSecond = 1.0f;            //!< アニメーションの1秒あたりのティック数。
        std::vector<AnimationChannel> channels; //!< アニメーションチャンネル。
    };
} // namespace Engine
