#pragma once

#include "Animation\Runtime\AnimatorInstance.h"
#include "Assets\Animation\AnimationTypes.h"
#include "Core\GameObject\Component.h"

namespace Engine
{
    /**
     * @brief GameObjectごとの単一Animation Clip再生を管理するComponent。
     * @thread_safety Main thread only. Render threadにはimmutable Snapshotを渡す。
     */
    class AnimatorComponent final : public Component
    {
    public:

        AnimatorComponent() noexcept;
        ~AnimatorComponent() override = default;
        GE_DISABLE_COPY_AND_MOVE(AnimatorComponent);

        /**
         * @brief Skeletonと単一Clipを設定する。互換性がない場合はfalseを返す。
         * @param skeleton 再生対象Skeleton Handle
         * @param clip 再生対象Animation Clip Handle
         * @return 互換性がある場合はtrue、ない場合はfalse
         */
        bool setAnimation(SkeletonHandle skeleton, AnimationClipHandle clip);

        /**
         * @brief SkeletonとAnimator Controllerを設定する。
         * @param skeleton 再生対象Skeleton Handle
         * @param controller 再生対象Animator Controller Handle
         */
        bool setController(SkeletonHandle skeleton, AnimatorControllerHandle controller);

        /**
         * @brief 同一GameObjectのModelが参照する最初のClipを設定する。
         * @return 設定に成功した場合はtrue、失敗した場合はfalse
         */
        bool useModelDefaultAnimation();

        /**
         * @brief 指定正規化時刻から再生する。
         * @param normalizedTime 再生を開始する正規化時刻
         * @return 再生に成功した場合はtrue、失敗した場合はfalse
         */
        bool play(float normalizedTime = 0.0f) noexcept;

        /**
         * @brief 指定stateを即時再生する。
         * @param stateId 再生するAnimator State ID
         * @param normalizedTime 再生を開始する正規化時刻
         * @return 再生に成功した場合はtrue、失敗した場合はfalse
         */
        bool play(AnimatorStateID stateId, float normalizedTime = 0.0f) noexcept;

        /**
         * @brief 指定stateへcross fadeする。
         * @param stateId 再生するAnimator State ID
         * @param duration クロスフェードの時間
         * @return 再生に成功した場合はtrue、失敗した場合はfalse
         */
        bool crossFade(AnimatorStateID stateId, float duration) noexcept;

        /**
         * @brief Animator Parameterを設定する。
         * @param parameterId 設定するAnimator Parameter ID
         * @param value 設定する値
         * @return 設定に成功した場合はtrue、失敗した場合はfalse
         */
        bool setFloat(AnimatorParameterID parameterId, float value) noexcept { return m_instance.setFloat(parameterId, value); }
        bool setInt(AnimatorParameterID parameterId, std::int32_t value) noexcept { return m_instance.setInt(parameterId, value); }
        bool setBool(AnimatorParameterID parameterId, bool value) noexcept { return m_instance.setBool(parameterId, value); }
        bool setTrigger(AnimatorParameterID parameterId) noexcept { return m_instance.setTrigger(parameterId); }
        bool resetTrigger(AnimatorParameterID parameterId) noexcept { return m_instance.resetTrigger(parameterId); }

        /**
         * @brief 現在のAnimator State IDを取得する。
         */
        AnimatorStateID getCurrentState() const noexcept { return m_instance.getCurrentState(); }

        /**
         * @brief 遷移進捗を0から1で取得する。
         * @return 遷移進捗
         */
        float getTransitionProgress() const noexcept { return m_instance.getTransitionProgress(); }

        /**
         * @brief 現在姿勢を維持して一時停止する。
         */
        void pause() noexcept { m_instance.pause(); }

        /**
         * @brief 再生速度を設定する。
         * @param speed 再生速度
         */
        void setSpeed(float speed) noexcept { m_instance.setSpeed(speed); }

        /**
         * @brief 再生速度を取得する。
         * @return 再生速度
         */
        float getSpeed() const noexcept { return m_instance.getSpeed(); }

        /**
         * @brief 再生中かを取得する。
         * @return 再生中であればtrue、そうでなければfalse
         */
        bool isPlaying() const noexcept { return m_instance.isPlaying(); }

        /**
         * @brief Wrap Mode適用後の正規化時刻を取得する。
         * @return 正規化時刻
         */
        float getNormalizedTime() const noexcept { return m_instance.getNormalizedTime(); }

        /**
         * @brief 設定中のSkeleton Handleを取得する。
         * @return 設定中のSkeleton Handle
         */
        SkeletonHandle getSkeleton() const noexcept { return m_skeleton; }

        /**
         * @brief 設定中のAnimation Clip Handleを取得する。
         * @return 設定中のAnimation Clip Handle
         */
        AnimationClipHandle getClip() const noexcept { return m_clip; }

        /**
         * @brief 設定中のAnimator Controller Handleを取得する。
         */
        AnimatorControllerHandle getController() const noexcept { return m_controller; }

        /**
         * @brief 永続化対象Skeleton GUIDを取得する。
         */
        const AssetGUID& getSkeletonGuid() const noexcept { return m_skeletonGuid; }

        /**
         * @brief 永続化対象Clip GUIDを取得する。
         */
        const AssetGUID& getClipGuid() const noexcept { return m_clipGuid; }

        /**
         * @brief 永続化対象Controller GUIDを取得する。
         */
        const AssetGUID& getControllerGuid() const noexcept { return m_controllerGuid; }

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

        /**
         * @brief Render threadへ提出可能なimmutable Palette Snapshotを取得する。
         * @return immutable Palette Snapshot
         */
        std::shared_ptr<const SkinningPaletteSnapshot> getSkinningSnapshot() const noexcept { return m_instance.getSnapshot(); }

        /**
         * @brief Root Motion適用設定を保持する。評価は将来のRoot Motion段階で行う。
         * @param apply Root Motionを適用するかどうか
         */
        void setApplyRootMotion(bool apply) noexcept { m_applyRootMotion = apply; }

        /**
         * @brief Root Motion適用設定を取得する。
         * @return Root Motionを適用するかどうか
         */
        bool getApplyRootMotion() const noexcept { return m_applyRootMotion; }

    protected:

        void onAwake() override;
        void onUpdate(float deltaTime) override;
        void onImGui() override;

    private:

        /**
         * @brief 再生中のAnimator Instanceに適用するために保持されるパラメータ値。
         */
        struct PersistedParameterValue
        {
            AnimatorParameterID id = 0;                                //!< parameterのstable ID。AnimatorControllerAsset内で一意。
            AnimatorParameterType type = AnimatorParameterType::Float; //!< parameterの型。AnimatorControllerAsset内で一意。
            float floatValue = 0.0f;                                   //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
            std::int32_t intValue = 0;                                 //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
            bool boolValue = false;                                    //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
        };

        /**
         * @brief SkeletonとAnimation Clipの互換性を検証し、ozz runtimeに設定する。
         * @return 成功した場合はtrue、失敗した場合はfalse
         */
        bool bindAssets();

        AnimatorInstance m_instance;                              //!< AnimatorInstanceはozz runtimeをラップし、Animation Clipの再生と評価を管理する。
        SkeletonHandle m_skeleton;                                //!< Skeleton Handleは再生対象Skeleton Assetを識別するためのハンドル。
        AnimationClipHandle m_clip;                               //!< Animation Clip Handleは再生対象Animation Clip Assetを識別するためのハンドル。
        AnimatorControllerHandle m_controller;                    //!< Animator Controller Handleは再生対象Animator Controller Assetを識別するためのハンドル。
        AssetGUID m_skeletonGuid;                                 //!< 永続化対象SkeletonのGUID。
        AssetGUID m_clipGuid;                                     //!< 永続化対象Skeleton/ClipのGUID。
        AssetGUID m_controllerGuid;                               //!< 永続化対象Skeleton/Clip/ControllerのGUID。
        std::vector<PersistedParameterValue> m_pendingParameters; //!< 再生中のAnimator Instanceに適用するために保持されるパラメータ値のリスト。
        AnimatorStateID m_pendingState = 0;                       //!< 再生中のAnimator Instanceに適用するために保持される再生対象のAnimator State ID。
        float m_pendingNormalizedTime = 0.0f;                     //!< 再生中のAnimator Instanceに適用するために保持される再生開始時の正規化時刻。
        bool m_pendingPlaying = true;                             //!< 再生中のAnimator Instanceに適用するために保持される再生状態。
        bool m_restorePending = false;                            //!< 再生中のAnimator Instanceに適用するために保持される再生状態を復元する必要があるかどうかを示すフラグ。
        bool m_bindingDirty = false;                              //!< SkeletonとAnimation Clipの互換性を検証する必要があるかどうかを示すフラグ。
        bool m_evaluationErrorLogged = false;                     //!< Animation Clipの評価に失敗した場合にエラーログを出力したかどうかを示すフラグ。
        bool m_applyRootMotion = false;                           //!< Root Motionを適用するかどうかを示すフラグ。
    };
}
