#pragma once

namespace Engine::Serialization
{
    /*
    * @brief　FlatBufferReaderクラスは、FlatBuffers形式のデータを読み込むためのクラスです。
    */
    class FlatBufferReader
    {
    public:

        /*
        * @brief 指定されたパスからFlatBuffers形式のデータを読み込む。
        * @param path 読み込み元のパス。
        * @return 読み込みが成功した場合はtrue、失敗した場合はfalseを返す。
        */
        bool open(const std::filesystem::path& path);

        /*
        * @brief FlatBuffers形式のデータが有効かどうかを検証する。
        * @return データが有効な場合はtrue、無効な場合はfalseを返す。
        */
        bool validate() const;

        /*
        * @brief 指定された識別子を持つかどうかを確認する。
        * @param identifier 確認する識別子。
        * @return 識別子が一致する場合はtrue、そうでない場合はfalseを返す。
        */
        bool hasIdentifier(const char* identifier) const;

        /*
        * @brief データへのポインタを取得する。
        * @return データへのポインタ。
        */
        const std::uint8_t* data() const { return m_data.data(); }

        /*
        * @brief データのサイズを取得する。
        * @return データのサイズ。
        */
        std::size_t size() const { return m_data.size(); }

    private:

        std::vector<std::uint8_t> m_data; //!< FlatBuffers形式のデータを格納するバッファ。
    };
}