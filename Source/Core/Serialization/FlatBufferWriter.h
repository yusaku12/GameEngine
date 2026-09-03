#pragma once

namespace Engine::Serialization
{
    /**
     * @brief FlatBuffersバイナリを書き込むクラス。
     */
    class FlatBufferWriter
    {
    public:

        /**
         * @brief 指定されたパスにバイナリを書き込む。
         * @param path 書き込み先のパス。
         * @param data 書き込むバイナリデータ。
         * @return 書き込みが成功した場合はtrue、失敗した場合はfalseを返す。
         */
        bool save(const std::filesystem::path& path, std::span<const std::uint8_t> data) const;

        /**
         * @brief 指定されたパスにバイナリを書き込む（アトミック操作）。
         * @param path 書き込み先のパス。
         * @param data 書き込むバイナリデータ。
         * @return 書き込みが成功した場合はtrue、失敗した場合はfalseを返す。
         */
        bool saveAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) const;
    };
}