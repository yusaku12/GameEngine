#pragma once

#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include "Assets\Animation\AnimationClipAsset.h"
#include "Assets\Animation\AnimatorControllerAsset.h"
#include "Assets\Animation\SkeletonAsset.h"
#include "Graphics\Renderer\SkinningPaletteSnapshot.h"

namespace Engine
{
    /**
    * @brief GameObjectごとのController状態とozz評価Bufferを所有する。
     * @thread_safety Main thread only. 公開SnapshotだけをRender threadへ渡せる。
     */
    class AnimatorInstance
    {
    public:

        AnimatorInstance() = default;
        ~AnimatorInstance() = default;
        GE_DISABLE_COPY_AND_MOVE(AnimatorInstance);

        /**
         * @brief 互換性を検証して再生Assetを設定し、Bind Poseを評価する。
         * @param skeleton 再生対象SkeletonAsset。nullptrは不可。
         * @param clip 再生対象AnimationClipAsset。nullptrは不可。
         */
        bool setAssets(std::shared_ptr<const SkeletonAsset> skeleton, std::shared_ptr<const AnimationClipAsset> clip);

        /**
         * @brief Controllerとstate順に解決済みのClipを設定する。
         * @thread_safety Main thread only.
         */
        bool setController(std::shared_ptr<const SkeletonAsset> skeleton,
            std::shared_ptr<const AnimatorControllerAsset> controller,
            std::vector<std::shared_ptr<const AnimationClipAsset>> clips);

        /**
         * @brief Assetと再生状態、評価Bufferを解放する。
         */
        void clear() noexcept;

        /** @brief 指定正規化時刻から再生を開始する。 */
        bool play(float normalizedTime = 0.0f) noexcept;

        /** @brief 指定stateを即時再生する。 */
        bool play(AnimatorStateID stateId, float normalizedTime = 0.0f) noexcept;

        /** @brief 指定stateへozz BlendingJobによる遷移を開始する。 */
        bool crossFade(AnimatorStateID stateId, float duration) noexcept;

        /** @brief Float parameterを設定する。型不一致ではfalse。 */
        bool setFloat(AnimatorParameterID parameterId, float value) noexcept;
        /** @brief Int parameterを設定する。型不一致ではfalse。 */
        bool setInt(AnimatorParameterID parameterId, std::int32_t value) noexcept;
        /** @brief Bool parameterを設定する。型不一致ではfalse。 */
        bool setBool(AnimatorParameterID parameterId, bool value) noexcept;
        /** @brief Trigger parameterを設定する。型不一致ではfalse。 */
        bool setTrigger(AnimatorParameterID parameterId) noexcept;
        /** @brief Trigger parameterを解除する。型不一致ではfalse。 */
        bool resetTrigger(AnimatorParameterID parameterId) noexcept;
        /** @brief Float parameterの現在値を取得する。 */
        bool getFloat(AnimatorParameterID parameterId, float& value) const noexcept;
        /** @brief Int parameterの現在値を取得する。 */
        bool getInt(AnimatorParameterID parameterId, std::int32_t& value) const noexcept;
        /** @brief Bool/Trigger parameterの現在値を取得する。 */
        bool getBool(AnimatorParameterID parameterId, bool& value) const noexcept;

        /** @brief 現在stateのstable IDを返す。 */
        AnimatorStateID getCurrentState() const noexcept;
        /** @brief 遷移進捗を0から1で返す。 */
        float getTransitionProgress() const noexcept;

        /**
         * @brief 現在姿勢を維持して時間更新を停止する。
         */
        void pause() noexcept { m_playing = false; }

        /**
         * @brief 時間を進め、ozz runtimeで新しいPalette Snapshotを生成する。
         * @param deltaTime 前回updateからの経過時間(秒)。負値は逆再生として扱う。
         */
        bool update(float deltaTime);

        /**
         * @brief 再生速度を設定する。負値は逆再生として扱う。
         * @param speed 正規化時刻の進行速度。1.0fで実時間と同じ速度、2.0fで2倍速、-1.0fで逆再生。
         */
        void setSpeed(float speed) noexcept;

        /**
         * @brief 現在の再生速度を取得する。
         */
        float getSpeed() const noexcept { return m_speed; }

        /**
         * @brief 時間が進行中かを取得する。
         */
        bool isPlaying() const noexcept { return m_playing; }

        /**
         * @brief Wrap Mode適用後の正規化時刻を取得する。
         */
        float getNormalizedTime() const noexcept;

        /**
         * @brief 現在までの符号付きLoop回数を取得する。
         */
        std::int64_t getLoopCount() const noexcept
        {
            return m_controller != nullptr && m_currentStateIndex < m_states.size()
                ? m_states[m_currentStateIndex].loopCount : m_loopCount;
        }

        /**
         * @brief 最後に正常評価されたimmutable Palette Snapshotを取得する。
         */
        std::shared_ptr<const SkinningPaletteSnapshot> getSnapshot() const noexcept { return m_publishedSnapshot; }

    private:

        struct RuntimeParameter
        {
            AnimatorParameterID id = 0;
            AnimatorParameterType type = AnimatorParameterType::Float;
            float floatValue = 0.0f;
            std::int32_t intValue = 0;
            bool boolValue = false;
        };

        struct RuntimeState
        {
            AnimatorStateID id = 0;
            std::shared_ptr<const AnimationClipAsset> clip;
            std::unique_ptr<ozz::animation::SamplingJob::Context> context;
            std::vector<ozz::math::SoaTransform> localTransforms;
            float speed = 1.0f;
            float time = 0.0f;
            std::int64_t loopCount = 0;
            AnimationWrapMode wrapMode = AnimationWrapMode::Loop;
        };

        struct RuntimeCondition
        {
            std::size_t parameterIndex = 0;
            AnimatorConditionMode mode = AnimatorConditionMode::Equals;
            float floatThreshold = 0.0f;
            std::int32_t intThreshold = 0;
        };

        struct RuntimeTransition
        {
            AnimatorTransitionID id = 0;
            std::size_t sourceStateIndex = 0;
            std::size_t destinationStateIndex = 0;
            float duration = 0.0f;
            float exitTime = 0.0f;
            bool hasExitTime = false;
            bool anyState = false;
            std::vector<RuntimeCondition> conditions;
        };

        /**
         * @brief 現在の正規化時刻でozz runtimeを評価し、Palette Snapshotを生成する。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        bool evaluate(float ratio);
        bool evaluateCurrentPose();
        bool sampleState(RuntimeState& state);
        bool beginTransition(std::size_t destinationStateIndex, float duration) noexcept;
        bool conditionsPass(const RuntimeTransition& transition) const noexcept;
        void consumeTriggers(const RuntimeTransition& transition) noexcept;
        void advanceState(RuntimeState& state, float deltaTime) noexcept;
        std::size_t findState(AnimatorStateID stateId) const noexcept;
        std::size_t findParameter(AnimatorParameterID parameterId) const noexcept;
        float normalizedTime(const RuntimeState& state) const noexcept;

        /**
         * @brief 次の空きSnapshotを取得する。Ring Bufferが全て使用中の場合はnullptrを返す。
         */
        SkinningPaletteSnapshot* acquireSnapshot() noexcept;

        std::shared_ptr<const SkeletonAsset> m_skeleton;                         //!< 再生対象SkeletonAsset。nullptrは未設定状態。
        std::shared_ptr<const AnimationClipAsset> m_clip;                        //!< 再生対象AnimationClipAsset。nullptrは未設定状態。
        std::shared_ptr<const AnimatorControllerAsset> m_controller;
        std::vector<RuntimeState> m_states;
        std::vector<RuntimeParameter> m_parameters;
        std::vector<RuntimeTransition> m_transitions;
        std::vector<ozz::math::SoaTransform> m_blendedTransforms;
        std::size_t m_currentStateIndex = 0;
        std::size_t m_destinationStateIndex = 0;
        float m_transitionTime = 0.0f;
        float m_transitionDuration = 0.0f;
        bool m_transitioning = false;
        std::unique_ptr<ozz::animation::SamplingJob::Context> m_samplingContext; //!< ozz runtimeのSamplingJob用Context。ozz runtimeが必要とする中間バッファを保持する。
        std::vector<ozz::math::SoaTransform> m_localTransforms;                  //!< ozz runtimeのSamplingJob用出力バッファ。ozz runtimeが評価したLocal Transformを保持する。
        std::vector<ozz::math::Float4x4> m_modelMatrices;                        //!< ozz runtimeのSamplingJob用出力バッファ。ozz runtimeが評価したModel Matrixを保持する。
        std::array<std::shared_ptr<SkinningPaletteSnapshot>, 3> m_snapshotRing;  //!< Ring Bufferとして保持するPalette Snapshot。Game threadが評価中のSnapshotとRender threadが参照中のSnapshotを分離する。
        std::shared_ptr<const SkinningPaletteSnapshot> m_publishedSnapshot;      //!< Render threadが参照中のPalette Snapshot。Game threadはこのSnapshotを更新しない。
        std::size_t m_nextSnapshot = 0;                                          //!< Ring Bufferの次に使用するSnapshotのインデックス。Game threadが評価中のSnapshotを指す。
        float m_time = 0.0f;                                                     //!< 再生中のClip内の現在時刻(秒)。ClipのWrap Modeに応じてClampまたはWrapされる。
        float m_speed = 1.0f;                                                    //!< 再生速度。1.0fで実時間と同じ速度、2.0fで2倍速、-1.0fで逆再生。
        std::int64_t m_loopCount = 0;                                            //!< 再生中のClipのLoop回数。ClipのWrap Modeに応じて符号付きで増減する。
        bool m_playing = false;                                                  //!< 再生中かどうか。trueで再生中、falseで停止中。
    };
}
