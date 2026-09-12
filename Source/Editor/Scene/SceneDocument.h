#pragma once

namespace Engine
{
    class Scene;
}

namespace Engine::Editor
{
    /**
     * @brief Editorで編集中のSceneファイルと保存状態を管理するクラス。
     * @thread_safety Main thread only.
     */
    class SceneDocument
    {
    public:

        /**
         * @brief 現在の保存先へSceneを保存する。
         * @param scene 保存するScene。
         * @return 保存に成功した場合はtrue。保存先が未設定の場合はfalse。
         */
        bool save(const Scene& scene);

        /**
         * @brief 保存先を指定してSceneを保存する。
         * @param scene 保存するScene。
         * @param path 保存先。拡張子がない場合は.sceneを補完する。
         * @return 保存に成功した場合はtrue。
         */
        bool saveAs(const Scene& scene, const std::filesystem::path& path);

        /**
         * @brief ファイルからSceneを読み込む。
         * @param scene 読み込み先のScene。
         * @param path 読み込むSceneファイルのパス。
         * @return 読み込みに成功した場合はtrue。
         */
        bool load(Scene& scene, const std::filesystem::path& path);

        /**
         * @brief 新規Scene用に保存先と操作結果をリセットする。
         */
        void reset() noexcept;

        /**
         * @brief 保存先が設定済みかどうかを取得する。
         */
        bool hasPath() const noexcept { return !m_path.empty(); }

        /**
         * @brief 現在の保存先を取得する。
         */
        const std::filesystem::path& path() const noexcept { return m_path; }

        /**
         * @brief 直近のファイル操作結果を表示する文字列を取得する。
         */
        const std::string& status() const noexcept { return m_status; }

    private:

        std::filesystem::path m_path; //!< 現在のSceneの保存先。
        std::string m_status;         //!< 直近のファイル操作結果。
    };
} // namespace Engine::Editor