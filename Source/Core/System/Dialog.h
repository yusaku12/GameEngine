#pragma once

struct IFileDialog;

namespace Engine
{
    /**
     * @brief ネイティブダイアログの終了結果。
     */
    enum class DialogResult
    {
        Ok,     //!< ユーザーが操作を確定した。
        Cancel, //!< ユーザーが操作をキャンセルした。
        Error   //!< ダイアログの生成または結果取得に失敗した。
    };

    /**
     * @brief ファイルダイアログに表示するファイル種別。
     */
    struct FileDialogFilter
    {
        const wchar_t* name = nullptr;    //!< ユーザーへ表示する種別名。呼び出し中は有効であること。
        const wchar_t* pattern = nullptr; //!< ワイルドカード形式の検索パターン。例: L"*.scene"。
    };

    /**
     * @brief Windows標準ファイルダイアログを提供するユーティリティクラス。
     * @details 各関数は呼び出し中のみCOMを初期化し、取得したCOMリソースをRAIIで解放する。
     * @thread_safety 呼び出し元のUIスレッドで使用すること。
     */
    class Dialog final
    {
    public:

        /**
         * @brief Windows標準の「ファイルを開く」ダイアログを表示する。
         * @param outPaths 選択されたファイルパスの格納先。呼び出し時に内容を消去する。
         * @param title ダイアログのタイトル。空の場合はシステム既定値を使用する。
         * @param initialPath 初期表示するフォルダーまたはファイルパス。
         * @param filters 選択可能なファイル種別。空の場合はすべてのファイルを表示する。
         * @param multiSelect 複数ファイルの選択を許可する場合はtrue。
         * @param ownerWindow ダイアログを所有するウィンドウ。省略時は所有者なし。
         * @return 確定、キャンセル、またはエラーを表すDialogResult。
         */
        static DialogResult openFile(
            std::vector<std::filesystem::path>& outPaths,
            std::wstring_view title = {},
            const std::filesystem::path& initialPath = {},
            std::span<const FileDialogFilter> filters = {},
            bool multiSelect = false,
            HWND ownerWindow = nullptr);

        /**
         * @brief Windows標準の「名前を付けて保存」ダイアログを表示する。
         * @param outPath 選択された保存先の格納先。呼び出し時に内容を消去する。
         * @param title ダイアログのタイトル。空の場合はシステム既定値を使用する。
         * @param initialPath 初期表示するフォルダーまたはファイルパス。
         * @param defaultExtension 既定の拡張子。先頭のピリオドは省略できる。
         * @param filters 選択可能なファイル種別。空の場合はすべてのファイルを表示する。
         * @param ownerWindow ダイアログを所有するウィンドウ。省略時は所有者なし。
         * @return 確定、キャンセル、またはエラーを表すDialogResult。
         */
        static DialogResult saveFile(
            std::filesystem::path& outPath,
            std::wstring_view title = {},
            const std::filesystem::path& initialPath = {},
            std::wstring_view defaultExtension = {},
            std::span<const FileDialogFilter> filters = {},
            HWND ownerWindow = nullptr);

        Dialog() = delete;

    private:

        /**
         * @brief ダイアログへ共通のタイトル、初期パス、ファイル種別を設定する
         * @param dialog 設定対象のダイアログ。
         * @param title ダイアログのタイトル。空の場合はシステム既定値を使用する。
         * @param initialPath 初期表示するフォルダーまたはファイルパス。
         * @param filters 選択可能なファイル種別。空の場合はすべてのファイルを表示する。
         */
        static HRESULT configure(
            IFileDialog& dialog,
            std::wstring_view title,
            const std::filesystem::path& initialPath,
            std::span<const FileDialogFilter> filters);
    };
} // namespace Engine