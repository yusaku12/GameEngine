#pragma once

#include <DirectXTex.h>
#include <filesystem>
#include <string_view>

namespace Engine
{
    /**
     * @brief GPU完了後に読み戻したScene HDRと表示用sRGB画像。
     * @details HDRはRGBA16F、displayはsRGB値を格納したRGBA8 UNORM。Forward時はhdrが空。
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    struct DX12FrameCapture
    {
        DirectX::ScratchImage hdr;     //!< Linear RGBA16F scene color.
        DirectX::ScratchImage display; //!< sRGB-encoded RGBA8 display color.

        /**
         * @brief HDRをDDS、表示画像をPNGとして保存する。
         * @param directory 既存の出力ディレクトリ。
         * @param name 区切り文字を含まないファイル名。
         * @pre 呼び出しスレッドのCOM Apartmentが初期化済みであること。
         * @pre 両画像を保持するDeferred Captureであること。Forwardの表示画像だけは保存しない。
         * @return 両画像の保存に成功した場合はtrue。既存ファイルは上書きしない。
         */
        bool save(const std::filesystem::path& directory, std::wstring_view name) const;
    };
}
