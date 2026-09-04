#pragma once

#include "Shader.h"
#include "ShaderCompiler.h"
#include "ShaderFileWatcher.h"

namespace Engine
{
    /**
     * @brief ShaderManager の動作モード
     */
    enum class ShaderMode
    {
        Runtime,     //!< ランタイムモード。.cso ロードのみを行い、HLSL コンパイルや Hot Reload は無効
        Editor,      //!< エディタモード。.cso 使用、HLSL コンパイルおよび Hot Reload 有効
        Development  //!< 開発モード。.cso 使用、Debug コンパイルおよび Hot Reload 有効
    };

    /**
     * @brief 個々の Shader の動作・ロード状態を表す列挙型
     */
    enum class ShaderStatus
    {
        Unloaded,      //!< ロードされていない状態
        Loaded,        //!< コンパイル済み .cso が正常にロードされている状態
        Compiling,     //!< バックグラウンドで DXC 再コンパイル実行中の状態
        CompileFailed, //!< コンパイル失敗状態 (前回の有効な Shader を維持)
        Reloading,     //!< CSO 再ロード処理中の状態
        ReloadFailed   //!< CSO リロード失敗状態 (前回の有効な Shader を維持)
    };

    //! Shader 登録識別子ハンドルの型定義
    using ShaderID = std::uint32_t;

    /**
     * @brief GUI やインスペクターで表示するための Shader 詳細情報構造体
     */
    struct ShaderDetails
    {
        ShaderID id = 0;                                                   //!< Shader 登録 ID
        ShaderCompileDesc compileDesc;                                     //!< コンパイル設定
        std::vector<std::filesystem::path> dependencies;                   //!< インクルード依存関係 (.hlsli) 一覧
        ShaderStatus status = ShaderStatus::Unloaded;                      //!< 現在のステータス
        bool hasValidBytecode = false;                                     //!< 有効な Bytecode を保持しているか
    };

    /**
     * @brief CSO ロード、DXC 非同期再ビルド、HLSL / HLSli 依存関係追跡、Hot Reload 通知を統括管理するクラス
     * @details Render Thread を停止させずにバックグラウンドスレッドで DXC コンパイルを行い、コンパイル成功時のみメインスレッドで安全に Shader オブジェクトを置き換える。
     * @thread_safety Thread-safe. メインスレッド・Render Thread・GUI スレッドからの並行呼び出しに対応。
     */
    class ShaderManager
    {
    public:

        /**
         * @brief Hot Reload 等で Shader が更新・再生成されたときに呼び出される通知コールバック
         */
        using ShaderChangedCallback = std::function<void(ShaderID)>;

        ShaderManager() = default;

        /**
         * @brief デストラクタ。スレッドおよびリソースを安全にシャットダウンする。
         */
        ~ShaderManager();

        GE_DISABLE_COPY_AND_MOVE(ShaderManager);

        /**
         * @brief ShaderManager を初期化し、必要に応じてバックグラウンドコンパイル worker および FileWatcher を起動する
         * @param mode 動作モード (Runtime, Editor, Development)
         * @param shaderRoot HLSL 監視対象のルートディレクトリ (例: "Assets/Shaders")
         * @param callback Shader 更新時に呼び出される通知コールバック
         * @return 初期化に成功した場合は true
         */
        bool initialize(ShaderMode mode, const std::filesystem::path& shaderRoot, ShaderChangedCallback callback = {});

        /**
         * @brief すべてのバックグラウンド処理を停止し、登録データを解放する
         */
        void shutdown();

        /**
         * @brief 管理対象の Shader を登録する
         * @param compileDesc 該当 Shader のコンパイル設定
         * @return 割り当てられた ShaderID (1 以上)
         */
        ShaderID registerShader(const ShaderCompileDesc& compileDesc);

        /**
         * @brief 登録済みのすべての Shader の .cso ファイルをディスクからロードする
         * @return すべての Shader のロードに成功した場合は true
         */
        bool loadAll();

        /**
         * @brief メインスレッド / Render Thread のフレーム更新時に呼び出し、Hot Reload イベントの検知と成果物の反映を行う
         */
        void processHotReload();

        /**
         * @brief 指定した ShaderID を持つ Shader を手動で再コンパイルキューへ追加する
         * @param id 再コンパイル対象の ShaderID
         */
        void recompileShader(ShaderID id);

        /**
         * @brief 登録されているすべての Shader を手動で再コンパイルキューへ追加する
         */
        void recompileAll();

        /**
         * @brief 登録されているすべての Shader の .cso ファイルをディスクから再ロードする
         * @return すべて成功した場合は true
         */
        bool reloadAll();

        /**
         * @brief 保持しているコンパイル済み Shader オブジェクト参照を取得する
         * @param id 取得対象の ShaderID
         * @return 非所有の DX12Shader 共有ポインタ。存在しない場合は nullptr
         */
        std::shared_ptr<const DX12Shader> get(ShaderID id) const;

        /**
         * @brief 指定した Shader の現在のステータスを取得する
         * @param id 対象の ShaderID
         * @return ShaderStatus
         */
        ShaderStatus getStatus(ShaderID id) const;

        /**
         * @brief GUI 表示用の Shader 詳細情報を取得する
         * @param id 対象の ShaderID
         * @return ShaderDetails 構造体
         */
        ShaderDetails getShaderDetails(ShaderID id) const;

        /**
         * @brief 現在登録されているすべての ShaderID 一覧を取得する
         * @return ShaderID のベクター
         */
        std::vector<ShaderID> getAllShaderIDs() const;

        /**
         * @brief 現在の動作モードを取得する
         * @return ShaderMode
         */
        ShaderMode getMode() const noexcept { return m_mode; }

    private:

        /**
         * @brief ShaderManager 内部で保持する Shader エントリ情報
         */
        struct Entry
        {
            ShaderCompileDesc compileDesc;                   //!< コンパイル設定
            std::vector<std::filesystem::path> dependencies; //!< 解析済みインクルード依存パス一覧
            std::shared_ptr<DX12Shader> shader;              //!< 保持中のコンパイル済み Shader
            ShaderStatus status = ShaderStatus::Unloaded;    //!< 現在のステータス
        };

        /** @brief 非同期コンパイルリクエスト */
        struct CompileRequest { ShaderID id; ShaderCompileDesc desc; };

        /** @brief 非同期コンパイル結果 */
        struct CompileResult { ShaderID id; ShaderCompileResult result; };

        /**
         * @brief バックグラウンドコンパイルワーカースレッドのメインループ
         */
        void compileWorker();

        /**
         * @brief 指定された ShaderID のコンパイルリクエストをキューへ追加する (要ロック)
         * @param id 対象の ShaderID
         */
        void enqueueCompile(ShaderID id);

        /**
         * @brief HLSL ソースファイルの #include ディレクティブを再帰解析し、依存ファイル一覧を収集する
         * @param sourcePath 解析対象の HLSL ファイルパス
         * @return 依存パス一覧のベクター
         */
        static std::vector<std::filesystem::path> collectDependencies(const std::filesystem::path& sourcePath);

        ShaderMode m_mode = ShaderMode::Runtime;                              //!< 動作モード
        std::filesystem::path m_shaderRoot;                                   //!< Shader ルートディレクトリ絶対パス
        ShaderCompiler m_compiler;                                            //!< DXC コンパイラ呼び出しオブジェクト
        ShaderFileWatcher m_watcher;                                          //!< ファイル変更監視オブジェクト
        ShaderChangedCallback m_callback;                                     //!< 更新通知コールバック
        mutable std::mutex m_mutex;                                           //!< 内部データ構造保護用ミューテックス
        std::unordered_map<ShaderID, Entry> m_entries;                        //!< ShaderID 検索マップ
        std::queue<CompileRequest> m_requests;                                //!< 非同期コンパイル要求キュー
        std::queue<CompileResult> m_results;                                  //!< 完了済みコンパイル結果キュー
        std::condition_variable m_condition;                                  //!< ワーカースレッド通知条件変数
        std::atomic_bool m_running = false;                                   //!< ワーカースレッド実行フラグ
        std::thread m_worker;                                                 //!< バックグラウンドコンパイルワーカースレッド
        ShaderID m_nextId = 1;                                                //!< 次に割り当てる ShaderID
    };
} // namespace Engine