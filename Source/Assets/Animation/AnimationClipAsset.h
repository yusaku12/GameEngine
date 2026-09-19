#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <ozz/animation/runtime/animation.h>
#include "Assets\Animation\AnimationTypes.h"

namespace Engine
{
    /**
     * @brief アニメーションイベント情報.
     */
    struct AnimationEvent
    {
        float normalizedTime = 0.0f; //!< 正規化された時間（0.0〜1.0）
        std::uint32_t eventId = 0;   //!< イベントの識別子
    };

    /**
     * @brief アニメーションクリップアセット情報.
     */
    struct AnimationClipAsset
    {
        AssetGUID guid;                                       //!< アセットのグローバル一意識別子
        std::string name;                                     //!< アセットの名前
        AssetGUID skeletonGuid;                               //!< 関連するスケルトンのグローバル一意識別子
        SkeletonSignature skeletonSignature;                  //!< 関連するスケルトンの署名
        ozz::animation::Animation animation;                  //!< Ozz アニメーションデータ
        float duration = 0.0f;                                //!< アニメーションの再生時間
        AnimationWrapMode wrapMode = AnimationWrapMode::Loop; //!< アニメーションの再生方法
        bool additive = false;                                //!< 加算アニメーションかどうか
        bool applyRootMotion = false;                         //!< ルートモーションを適用するかどうか
        std::vector<AnimationEvent> events;                   //!< アニメーションイベントのリスト
        std::filesystem::path sourcePath;                     //!< ソースファイルのパス
        std::string sourceClipName;                           //!< ソースクリップの名前
    };
}
