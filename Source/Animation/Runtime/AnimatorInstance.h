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

        /**
         * @brief 指定正規化時刻から再生を開始する。
         * @param normalizedTime 正規化時刻(0.0〜1.0)。負値は逆再生として扱う。
         */
        bool play(float normalizedTime = 0.0f) noexcept;

        /**
         * @brief 指定stateを即時再生する。
         * @param stateId 再生するstateのID。
         * @param normalizedTime 正規化時刻(0.0〜1.0)。負値は逆再生として扱う。
         * @return 再生に成功した場合はtrue、それ以外はfalse。
         */
        bool play(AnimatorStateID stateId, float normalizedTime = 0.0f) noexcept;

        /**
         * @brief 指定stateへozz BlendingJobによる遷移を開始する。
         * @param stateId 遷移先のstateのID。
         * @param duration 遷移時間(秒)。0.0f以下は即時遷移として扱う。
         */
        bool crossFade(AnimatorStateID stateId, float duration) noexcept;

        /**
         * @brief Float parameterを設定する。型不一致ではfalse。
         * @param parameterId 設定するparameterのID。
         * @param value 設定する値。
         */
        bool setFloat(AnimatorParameterID parameterId, float value) noexcept;
        bool setInt(AnimatorParameterID parameterId, std::int32_t value) noexcept;
        bool setBool(AnimatorParameterID parameterId, bool value) noexcept;
        bool setTrigger(AnimatorParameterID parameterId) noexcept;

        /**
         * @brief Trigger parameterを解除する。型不一致ではfalse。
         * @param parameterId 解除するparameterのID。
         */
        bool resetTrigger(AnimatorParameterID parameterId) noexcept;

        /**
         * @brief Float parameterの現在値を取得する。
         * @param parameterId 取得するparameterのID。
         * @param value 取得した値を格納する変数への参照。
         * @return 取得に成功した場合はtrue、それ以外はfalse。
         */
        bool getFloat(AnimatorParameterID parameterId, float& value) const noexcept;
        bool getInt(AnimatorParameterID parameterId, std::int32_t& value) const noexcept;
        bool getBool(AnimatorParameterID parameterId, bool& value) const noexcept;

        /**
         * @brief 現在stateのstable IDを返す。
         */
        AnimatorStateID getCurrentState() const noexcept;

        /**
         * @brief 遷移進捗を0から1で返す。
         */
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

        /**
         * @brief 再生中のstateのozz runtime評価に必要な情報を保持する。
         */
        struct RuntimeParameter
        {
            AnimatorParameterID id = 0;                                //!< parameterのstable ID。AnimatorControllerAsset内で一意。
            AnimatorParameterType type = AnimatorParameterType::Float; //!< parameterの型。AnimatorControllerAsset内で一意。
            float floatValue = 0.0f;                                   //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
            std::int32_t intValue = 0;                                 //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
            bool boolValue = false;                                    //!< parameterの値。型に応じてfloatValue/intValue/boolValueのいずれかを使用する。
        };

        /**
         * @brief 再生中のstateのozz runtime評価に必要な情報を保持する。
         */
        struct RuntimeState
        {
            AnimatorStateID id = 0;                                        //!< stateのstable ID。AnimatorControllerAsset内で一意。
            std::shared_ptr<const AnimationClipAsset> clip;                //!< 再生中のClip。nullptrは未設定状態。
            std::unique_ptr<ozz::animation::SamplingJob::Context> context; //!< ozz runtimeのSamplingJob用Context。ozz runtimeが必要とする中間バッファを保持する。
            std::vector<ozz::math::SoaTransform> localTransforms;          //!< ozz runtimeのSamplingJob用出力バッファ。ozz runtimeが評価したLocal Transformを保持する。
            float speed = 1.0f;                                            //!< 再生速度。負値は逆再生として扱う。
            float time = 0.0f;                                             //!< 再生中のClipの経過時間(秒)。ClipのWrap Modeに応じてClampまたはWrapされる。
            std::int64_t loopCount = 0;                                    //!< 再生中のClipの符号付きLoop回数。ClipのWrap Modeに応じて増減する。
            AnimationWrapMode wrapMode = AnimationWrapMode::Loop;          //!< 再生中のClipのWrap Mode。ClipのWrap Modeに応じてtimeとloopCountが変化する。
        };

        /**
         * @brief RuntimeTransitionの条件判定に使用する。
         */
        struct RuntimeCondition
        {
            std::size_t parameterIndex = 0;                             //!< parameterのインデックス。m_parameters[parameterIndex]が条件判定に使用するparameter。
            AnimatorConditionMode mode = AnimatorConditionMode::Equals; //!< 条件判定の方法。AnimatorControllerAsset内で一意。
            float floatThreshold = 0.0f;                                //!< 条件判定の閾値。parameterの型に応じてfloatThreshold/intThresholdのいずれかを使用する。
            std::int32_t intThreshold = 0;                              //!< 条件判定の閾値。parameterの型に応じてfloatThreshold/intThresholdのいずれかを使用する。
        };

        /**
         * @brief RuntimeState間の遷移を表す。条件判定とBlendingJobの評価に使用する。
         */
        struct RuntimeTransition
        {
            AnimatorTransitionID id = 0;              //!< transitionのstable ID。AnimatorControllerAsset内で一意。
            std::size_t sourceStateIndex = 0;         //!< 遷移元のstateのインデックス。m_states[sourceStateIndex]が遷移元のstate。
            std::size_t destinationStateIndex = 0;    //!< 遷移先のstateのインデックス。m_states[destinationStateIndex]が遷移先のstate。
            float duration = 0.0f;                    //!< 遷移時間(秒)。0.0f以下は即時遷移として扱う。
            float exitTime = 0.0f;                    //!< 遷移元のstateの正規化時刻。0.0f〜1.0fの範囲で指定する。
            bool hasExitTime = false;                 //!< 遷移元のstateの正規化時刻を使用するかどうか。trueで使用する、falseで使用しない。
            bool anyState = false;                    //!< 遷移元のstateがany stateかどうか。trueでany state、falseで特定のstate。
            std::vector<RuntimeCondition> conditions; //!< 遷移条件のリスト。条件判定に使用する。
        };

        /**
         * @brief 現在の正規化時刻でozz runtimeを評価し、Palette Snapshotを生成する。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        bool evaluate(float ratio);

        /**
         * @brief 現在の正規化時刻でozz runtimeを評価し、Palette Snapshotを生成する。
         */
        bool evaluateCurrentPose();

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         */
        bool sampleState(RuntimeState& state);

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        bool beginTransition(std::size_t destinationStateIndex, float duration) noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        bool conditionsPass(const RuntimeTransition& transition) const noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        void consumeTriggers(const RuntimeTransition& transition) noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        void advanceState(RuntimeState& state, float deltaTime) noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        std::size_t findState(AnimatorStateID stateId) const noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        std::size_t findParameter(AnimatorParameterID parameterId) const noexcept;

        /**
         * @brief 現在のstateをozz runtimeで評価し、Blended Transformを生成する。
         * @param state 評価するstate。m_states[m_currentStateIndex]が現在再生中のstate。
         * @param ratio 正規化時刻。0.0fでClip開始、1.0fでClip終了。
         */
        float normalizedTime(const RuntimeState& state) const noexcept;

        /**
         * @brief 次の空きSnapshotを取得する。Ring Bufferが全て使用中の場合はnullptrを返す。
         */
        SkinningPaletteSnapshot* acquireSnapshot() noexcept;

        std::shared_ptr<const SkeletonAsset> m_skeleton;                         //!< 再生対象SkeletonAsset。nullptrは未設定状態。
        std::shared_ptr<const AnimationClipAsset> m_clip;                        //!< 再生対象AnimationClipAsset。nullptrは未設定状態。
        std::shared_ptr<const AnimatorControllerAsset> m_controller;             //!< 再生対象AnimatorControllerAsset。nullptrは未設定状態。
        std::vector<RuntimeState> m_states;                                      //!< 再生中のstateのozz runtime評価に必要な情報を保持する。
        std::vector<RuntimeParameter> m_parameters;                              //!< 再生中のparameterのozz runtime評価に必要な情報を保持する。
        std::vector<RuntimeTransition> m_transitions;                            //!< 再生中のstate間の遷移を表す。条件判定とBlendingJobの評価に使用する。
        std::vector<ozz::math::SoaTransform> m_blendedTransforms;                //!< ozz runtimeのBlendingJob用出力バッファ。ozz runtimeが評価したBlended Transformを保持する。
        std::size_t m_currentStateIndex = 0;                                     //!< 再生中のstateのインデックス。m_states[m_currentStateIndex]が現在再生中のstate。
        std::size_t m_destinationStateIndex = 0;                                 //!< 遷移先のstateのインデックス。m_states[m_destinationStateIndex]が遷移先のstate。
        float m_transitionTime = 0.0f;                                           //!< 遷移開始からの経過時間(秒)。ClipのWrap Modeに応じてClampまたはWrapされる。
        float m_transitionDuration = 0.0f;                                       //!< 遷移時間(秒)。0.0f以下は即時遷移として扱う。
        bool m_transitioning = false;                                            //!< 遷移中かどうか。trueで遷移中、falseで遷移中ではない。
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
