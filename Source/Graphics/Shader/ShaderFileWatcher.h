#pragma once

namespace Engine
{
    /**
     * @brief Windows ReadDirectoryChangesW API を使用して HLSL/HLSli ファイルの変更を常時監視するクラス
     * @details 非同期バックグラウンドスレッドでファイルシステムイベントをキャッチし、デバウンス処理後に変更ファイル一覧を収集する。
     * @thread_safety Thread-safe. 変更一覧の取得 (consumeChanges) や開始/停止はマルチスレッドから呼び出し可能。
     */
    class ShaderFileWatcher
    {
    public:

        ShaderFileWatcher() = default;

        /**
         * @brief デストラクタ。監視スレッドを安全に停止・終了する。
         */
        ~ShaderFileWatcher();

        /**
         * @brief 指定したディレクトリ下の Shader ファイル監視を開始する
         * @param directory 監視対象のルートディレクトリ (例: "Assets/Shaders")
         * @param debounce 変更検知後の重複抑制デバウンス時間 (ミリ秒)
         * @return 監視の開始に成功した場合は true
         */
        bool start(const std::filesystem::path& directory, std::chrono::milliseconds debounce = std::chrono::milliseconds(200));

        /**
         * @brief 監視を停止し、バックグラウンドスレッドを終了する
         */
        void stop();

        /**
         * @brief 検出された変更ファイル一覧を取得し、内部キューをクリアする
         * @return 変更された Shader ファイル (.hlsl, .hlsli) のパス一覧
         */
        std::vector<std::filesystem::path> consumeChanges();

        /**
         * @brief 現在ファイル監視が実行中かどうかを取得する
         * @return 実行中の場合は true
         */
        bool isWatching() const noexcept { return m_running; }

    private:

        /**
         * @brief バックグラウンド監視スレッドのメインループ関数
         */
        void watch();

        std::filesystem::path m_directory;            //!< 監視対象ディレクトリの絶対パス
        std::chrono::milliseconds m_debounce{ 200 };  //!< デバウンス待機時間
        std::atomic_bool m_running = false;           //!< 監視スレッド実行フラグ
        std::thread m_thread;                         //!< 監視ワーカースレッド
        std::mutex m_mutex;                           //!< 変更リスト保護用ミューテックス
        std::vector<std::filesystem::path> m_changes; //!< 蓄積された変更ファイル一覧
        void* m_directoryHandle = nullptr;            //!< Windows Directory Handle (HANDLE)
    };
} // namespace Engine