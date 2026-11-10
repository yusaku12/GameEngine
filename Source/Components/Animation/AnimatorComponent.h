#pragma once

#include <span>
#include <optional>
#include <string>
#include <string_view>

#include "Animation\Runtime\AnimatorInstance.h"
#include "Assets\Animation\AnimationTypes.h"
#include "Core\GameObject\Component.h"

namespace Engine
{
    /**
     * @brief GameObjectごとのClip再生とClip別設定、Root Motionを管理するComponent。
     * @thread_safety Lifecycle準備はGame update threadで行い、Animation評価はJobSystem workerで行う。
     * 同一Componentへ評価中にアクセスしてはならない。Render threadにはimmutable Snapshotを渡す。
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
         * @brief 同一GameObjectのModelが参照する最初のClipを設定する。
         * @return 設定に成功した場合はtrue、失敗した場合はfalse
         */
        bool useModelDefaultAnimation();

        /**
         * @brief 外部ファイルからアニメーションを読み込み、最初のClipを再生対象にする。
         * @param path アニメーションを含むFBX等のファイルパス
         * @return 読み込みと設定に成功した場合はtrue
         */
        bool importAnimation(const std::filesystem::path& path);

        /**
         * @brief 指定正規化時刻から再生する。
         * @param normalizedTime 再生を開始する正規化時刻
         * @return 再生に成功した場合はtrue、失敗した場合はfalse
         */
        bool play(float normalizedTime = 0.0f);

        /**
         * @brief 現在位置と往復方向を維持して再開する。Once完了時は再生速度の向きに応じて先頭/末尾から再開する。
         * @return 再開成功時はtrue。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool resume();

        /**
         * @brief 姿勢を即時シークする。再生状態を維持し、Root Motion移動は発生させない。
         * @param normalizedTime 0から1へ制限する正規化時刻。非有限値は拒否する。
         * @return 評価成功時はtrue。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool seek(float normalizedTime);

        /**
         * @brief 元Clipの等間隔サンプル1つ分を進める/戻す。間隔不明時は1/30秒を使用する。
         * @param forward trueで進める、falseで戻す。速度とWrap Modeに依存せず端で止まる。
         * @return 姿勢更新成功時はtrue。一時停止し、Root Motion移動は発生させない。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool stepFrame(bool forward);

        /** @brief 先頭姿勢へ戻して停止する。失敗時はfalse。評価との並行呼び出しは不可。 */
        bool stop();

        /** @brief 再生/一時停止状態を維持して先頭姿勢へ戻す。失敗時はfalse。評価との並行呼び出しは不可。 */
        bool rewind() { return seek(0.0f); }

        /**
         * @brief 選択中ClipのComponent固有Wrap Modeを設定する。
         * @param mode Once / Loop / PingPong。
         * @return 設定成功時はtrue。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool setWrapMode(AnimationWrapMode mode);

        /** @brief 選択中ClipのWrap Modeを取得する。未選択時はLoop。 */
        AnimationWrapMode getWrapMode() const noexcept;

        /**
         * @brief ゲーム開始時のClipを指定する。無効Handleは選択中Clipを使用する設定へ戻す。
         * @param clip 同じSkeletonに属するClip。
         * @return 設定成功時はtrue。現在のプレビュー再生は変更しない。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool setInitialAnimation(AnimationClipHandle clip);

        /** @brief 初期Clip GUIDを取得する。無効GUIDは選択中Clipを使用する。 */
        const AssetGUID& getInitialAnimationGuid() const noexcept { return m_initialClipGuid; }

        /** @brief ゲーム開始時の自動再生を設定する。現在のプレビューは変更しない。 */
        void setAutoPlay(bool enabled) noexcept { m_autoPlay = enabled; }

        /** @brief ゲーム開始時に自動再生するかを取得する。 */
        bool getAutoPlay() const noexcept { return m_autoPlay; }

        /**
         * @brief 現在姿勢を維持して一時停止する。
         */
        void pause() noexcept
        {
            m_instance.pause();
            if (m_restorePending) m_pendingPlaying = false;
        }

        /**
         * @brief 選択中Clipの再生速度を設定する。非有限値またはClip未選択はログ付きで拒否する。
         * @param speed 再生速度
         */
        void setSpeed(float speed);

        /**
         * @brief 再生速度を取得する。
         * @return 再生速度
         */
        float getSpeed() const noexcept { return m_instance.getSpeed(); }

        /**
         * @brief 選択中ClipのComponent内表示名を変更する。空白のみの名前は拒否する。
         * @param name Component内表示名。
         * @return 変更できた場合はtrue。共有Clip Asset名やファイル名は変更しない。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool setAnimationName(std::string_view name);

        /** @brief 選択中ClipのComponent内表示名を取得する。未選択時は空。 */
        std::string_view getAnimationName() const noexcept;

        /**
         * @brief 再生中かを取得する。
         * @return 再生中であればtrue、そうでなければfalse
         */
        bool isPlaying() const noexcept { return m_restorePending ? m_pendingPlaying : m_instance.isPlaying(); }

        /**
         * @brief Wrap Mode適用後の正規化時刻を取得する。
         * @return 正規化時刻
         */
        float getNormalizedTime() const noexcept
        {
            return m_restorePending ? m_pendingNormalizedTime : m_instance.getNormalizedTime();
        }

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
         * @brief 永続化対象Skeleton GUIDを取得する。
         */
        const AssetGUID& getSkeletonGuid() const noexcept { return m_skeletonGuid; }

        /**
         * @brief 永続化対象Clip GUIDを取得する。
         */
        const AssetGUID& getClipGuid() const noexcept { return m_clipGuid; }

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        bool copyConfigurationTo(Component& destination) const override;

        /**
         * @brief Render threadへ提出可能なimmutable Palette Snapshotを取得する。
         * @return immutable Palette Snapshot
         */
        std::shared_ptr<const SkinningPaletteSnapshot> getSkinningSnapshot() const noexcept { return m_instance.getSnapshot(); }

        /**
         * @brief 選択中ClipのXYZ移動と回転を姿勢から抽出し、Play中にGameObjectへ適用する。
         * @details 有効なCharacterControllerがあれば衝突付き移動を行う。
         * Dynamic Rigidbodyとの併用は不可。編集時は姿勢からの除去のみ行う。
         * @param apply Root Motionを適用するかどうか
         */
        void setApplyRootMotion(bool apply);

        /**
         * @brief Root Motion適用設定を取得する。
         * @return Root Motionを適用するかどうか
         */
        bool getApplyRootMotion() const noexcept { return m_applyRootMotion; }

        /** @brief 抽出ボーンを指定する。空の名前は先頭ルート。既知のSkeletonに存在しない名前は拒否する。 */
        bool setRootMotionJoint(std::string_view jointName);

        /** @brief 保存対象の抽出ボーン名。空なら先頭ルートを使用する。 */
        const std::string& getRootMotionJoint() const noexcept { return m_rootMotionJoint; }

        /**
         * @brief 選択中Clipの抽出軸を設定する。OFFの成分は姿勢に残し、共有Assetは変更しない。
         * @param axes モデルローカル移動X/Y/Zと回転のON/OFF。
         * @return Clip選択済みならtrue。
         * @thread_safety 評価と並行して呼び出してはならない。
         */
        bool setRootMotionAxes(RootMotionAxes axes);

        /** @brief 選択中Clipの抽出軸を取得する。未選択時は全成分ON。 */
        RootMotionAxes getRootMotionAxes() const noexcept;

    protected:

        void onAwake() override;
        void onImGui() override;

    private:

        friend class GameObjectManager;

        void preparePendingUpdate();
        void clearAnimationClip() noexcept;

        struct ClipConfiguration
        {
            AssetGUID guid{};
            std::filesystem::path path;
            std::string name;
            float speed = 1.0f;
            bool applyRootMotion = false;
            std::optional<AnimationWrapMode> wrapMode;
            RootMotionAxes rootMotionAxes;
        };

        ClipConfiguration* findClipConfiguration(const AssetGUID& guid) noexcept;
        const ClipConfiguration* findClipConfiguration(const AssetGUID& guid) const noexcept;
        void registerClip(AnimationClipHandle clip);
        void refreshModelClips();
        void applyClipConfiguration() noexcept;
        bool prepareStartup();

        /**
         * @brief SkeletonとAnimation Clipの互換性を検証し、ozz runtimeに設定する。
         * @return 成功した場合はtrue、失敗した場合はfalse
         */
        bool bindAssets();

        /**
         * @brief 通常更新で準備したAnimationを評価する。
         * @details GameObjectManagerが全Componentの通常更新を終えた後に呼び出す。
         */
        void evaluatePendingUpdate(float deltaTime);
        void applyPendingRootMotion(float deltaTime);

        AnimatorInstance m_instance;                                    //!< AnimatorInstanceはozz runtimeをラップし、Animation Clipの再生と評価を管理する。
        SkeletonHandle m_skeleton;                                      //!< Skeleton Handleは再生対象Skeleton Assetを識別するためのハンドル。
        AnimationClipHandle m_clip;                                     //!< Animation Clip Handleは再生対象Animation Clip Assetを識別するためのハンドル。
        AssetGUID m_skeletonGuid;                                       //!< 永続化対象SkeletonのGUID。
        AssetGUID m_clipGuid;                                           //!< 永続化対象Skeleton/ClipのGUID。
        std::filesystem::path m_skeletonPath;                           //!< Skeletonの永続化Path。
        std::filesystem::path m_clipPath;                               //!< Animation Clipの永続化Path。
        std::vector<ClipConfiguration> m_clipConfigurations;
        AssetGUID m_initialClipGuid;
        bool m_autoPlay = true;
        bool m_startupPending = false;
        float m_pendingNormalizedTime = 0.0f;                           //!< 再生中のAnimator Instanceに適用するために保持される再生開始時の正規化時刻。
        bool m_pendingPlaying = true;                                   //!< 再生中のAnimator Instanceに適用するために保持される再生状態。
        bool m_restorePending = false;                                  //!< 再生中のAnimator Instanceに適用するために保持される再生状態を復元する必要があるかどうかを示すフラグ。
        bool m_bindingDirty = false;                                    //!< SkeletonとAnimation Clipの互換性を検証する必要があるかどうかを示すフラグ。
        bool m_evaluationErrorLogged = false;                           //!< Animation Clipの評価に失敗した場合にエラーログを出力したかどうかを示すフラグ。
        bool m_evaluationPending = false;                               //!< 通常更新でAnimation評価の準備が完了したかどうかを示すフラグ。
        bool m_applyRootMotion = false;                                 //!< Root Motionを適用するかどうかを示すフラグ。
        std::string m_rootMotionJoint;
        bool m_rootMotionPending = false;
        bool m_rootMotionErrorLogged = false;
        std::vector<AnimationClipHandle> m_availableClips;              //!< 再生可能なAnimation Clip Handleのリスト。
    };
}
