#pragma once

#include <filesystem>

namespace Engine
{
    class DX12Renderer;

    /**
     * @brief 外部アセット不要の固定シーンでHDR・表示変換・描画順・再現性を検証する。
     * @param renderer 初期化済みRuntime Renderer。検証後は終了すること。
     * @param directory 新規または空の出力ディレクトリ。既存の基準画像は上書きしない。
     * @return 全検証と画像・結果の保存に成功した場合はtrue。
     * @thread_safety Render thread only. Scene update and rendering must not run concurrently.
     */
    bool runRenderingValidation(DX12Renderer& renderer, const std::filesystem::path& directory);
}
