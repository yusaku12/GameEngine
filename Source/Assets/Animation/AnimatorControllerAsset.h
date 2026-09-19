#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "Assets\Animation\AnimationTypes.h"

namespace Engine
{
    using AnimatorStateID = std::uint32_t;
    using AnimatorParameterID = std::uint32_t;
    using AnimatorTransitionID = std::uint32_t;
    using AnimatorLayerID = std::uint32_t;

    enum class AnimatorParameterType : std::uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger,
    };

    enum class AnimatorConditionMode : std::uint8_t
    {
        Greater,
        Less,
        Equals,
        NotEqual,
        If,
        IfNot,
        Triggered,
    };

    enum class AnimatorWrapOverride : std::uint8_t
    {
        UseClip,
        Once,
        Loop,
        PingPong,
    };

    struct AnimatorParameter
    {
        AnimatorParameterID id = 0;
        std::string name;
        AnimatorParameterType type = AnimatorParameterType::Float;
        float defaultFloat = 0.0f;
        std::int32_t defaultInt = 0;
        bool defaultBool = false;
    };

    struct AnimatorState
    {
        AnimatorStateID id = 0;
        std::string name;
        AssetGUID clipGuid;
        float speed = 1.0f;
        AnimatorWrapOverride wrapOverride = AnimatorWrapOverride::UseClip;
    };

    struct AnimatorCondition
    {
        AnimatorParameterID parameterId = 0;
        AnimatorConditionMode mode = AnimatorConditionMode::Equals;
        float floatThreshold = 0.0f;
        std::int32_t intThreshold = 0;
    };

    struct AnimatorTransition
    {
        AnimatorTransitionID id = 0;
        AnimatorStateID sourceState = 0;
        AnimatorStateID destinationState = 0;
        float duration = 0.0f;
        float exitTime = 0.0f;
        bool hasExitTime = false;
        bool anyState = false;
        std::vector<AnimatorCondition> conditions;
    };

    /**
     * @brief 共有可能な1 Layer Animator Controller定義。
     * @thread_safety 公開後はimmutable。
     */
    struct AnimatorControllerAsset
    {
        AssetGUID guid;
        std::string name;
        AssetGUID skeletonGuid;
        SkeletonSignature skeletonSignature;
        AnimatorLayerID layerId = 0;
        std::string layerName;
        AnimatorStateID defaultState = 0;
        std::vector<AnimatorParameter> parameters;
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;
        std::filesystem::path sourcePath;
    };

    /** @brief 名前から再現可能なstable IDを生成する。0は返さない。 */
    AnimatorStateID makeAnimatorID(std::string_view name) noexcept;
}
