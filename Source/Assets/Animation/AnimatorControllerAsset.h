#pragma once

#include "Assets\Animation\AnimationTypes.h"

namespace Engine
{
    using AnimatorStateID = std::uint32_t;
    using AnimatorParameterID = std::uint32_t;
    using AnimatorTransitionID = std::uint32_t;
    using AnimatorLayerID = std::uint32_t;

    /**
     * @brief AnimatorControllerのパラメータの種類。
     */
    enum class AnimatorParameterType : std::uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger,
    };

    /**
     * @brief AnimatorControllerの条件の種類。
     */
    enum class AnimatorConditionMode : std::uint8_t
    {
        Greater,   //!< パラメータが閾値より大きい場合に遷移する
        Less,      //!< パラメータが閾値より小さい場合に遷移する
        Equals,    //!< パラメータが閾値と等しい場合に遷移する
        NotEqual,  //!< パラメータが閾値と等しくない場合に遷移する
        If,        //!< パラメータがtrueの場合に遷移する
        IfNot,     //!< パラメータがfalseの場合に遷移する
        Triggered, //!< パラメータがトリガーされた場合に遷移する
    };

    /**
     * @brief AnimatorControllerのラップモードのオーバーライド。
     */
    enum class AnimatorWrapOverride : std::uint8_t
    {
        UseClip,  //!< クリップのラップモードを使用する
        Once,     //!< クリップを1回再生する
        Loop,     //!< クリップをループ再生する
        PingPong, //!< クリップを往復再生する
    };

    /**
     * @brief AnimatorControllerのパラメータ定義。
     */
    struct AnimatorParameter
    {
        AnimatorParameterID id = 0;                                //!< パラメータのID
        std::string name;                                          //!< パラメータの名前
        AnimatorParameterType type = AnimatorParameterType::Float; //!< パラメータの種類
        float defaultFloat = 0.0f;                                 //!< デフォルトのfloat値
        std::int32_t defaultInt = 0;                               //!< デフォルトのint値
        bool defaultBool = false;                                  //!< デフォルトのbool値
    };

    /**
     * @brief AnimatorControllerの状態定義。
     */
    struct AnimatorState
    {
        AnimatorStateID id = 0;                                            //!< 状態のID
        std::string name;                                                  //!< 状態の名前
        AssetGUID clipGuid;                                                //!< 状態に関連付けられたアニメーションクリップのGUID
        float speed = 1.0f;                                                //!< 状態の再生速度
        AnimatorWrapOverride wrapOverride = AnimatorWrapOverride::UseClip; //!< 状態のラップモードのオーバーライド
    };

    /**
     * @brief AnimatorControllerの遷移条件定義。
     */
    struct AnimatorCondition
    {
        AnimatorParameterID parameterId = 0;                        //!< 遷移条件に使用するパラメータのID
        AnimatorConditionMode mode = AnimatorConditionMode::Equals; //!< 遷移条件の種類
        float floatThreshold = 0.0f;                                //!< 遷移条件の閾値（float型）
        std::int32_t intThreshold = 0;                              //!< 遷移条件の閾値（int型）
    };

    /**
     * @brief AnimatorControllerの遷移定義。
     */
    struct AnimatorTransition
    {
        AnimatorTransitionID id = 0;               //!< 遷移のID
        AnimatorStateID sourceState = 0;           //!< 遷移元の状態のID
        AnimatorStateID destinationState = 0;      //!< 遷移先の状態のID
        float duration = 0.0f;                     //!< 遷移の継続時間（秒）
        float exitTime = 0.0f;                     //!< 遷移の終了時間（秒）
        bool hasExitTime = false;                  //!< 遷移に終了時間が設定されているかどうか
        bool anyState = false;                     //!< 遷移がAnyStateからの遷移かどうか
        std::vector<AnimatorCondition> conditions; //!< 遷移条件のリスト
    };

    /**
     * @brief 共有可能な1 Layer Animator Controller定義。
     * @thread_safety 公開後はimmutable。
     */
    struct AnimatorControllerAsset
    {
        AssetGUID guid;                              //!< アセットのGUID
        std::string name;                            //!< アセットの名前
        AssetGUID skeletonGuid;                      //!< アセットが対応するスケルトンのGUID
        SkeletonSignature skeletonSignature;         //!< アセットが対応するスケルトンの署名
        AnimatorLayerID layerId = 0;                 //!< レイヤーのID
        std::string layerName;                       //!< レイヤーの名前
        AnimatorStateID defaultState = 0;            //!< デフォルトの状態のID
        std::vector<AnimatorParameter> parameters;   //!< アセットのパラメータ定義
        std::vector<AnimatorState> states;           //!< アセットの状態定義
        std::vector<AnimatorTransition> transitions; //!< アセットの遷移定義
        std::filesystem::path sourcePath;            //!< アセットのソースファイルパス
    };

    /**
     * @brief 名前から再現可能なstable IDを生成する。0は返さない。
     * @param name 名前
     */
    AnimatorStateID makeAnimatorID(std::string_view name) noexcept;
}
