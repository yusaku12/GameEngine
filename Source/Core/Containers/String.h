#pragma once

#include <cstring>
#include <string>
#include <string_view>

#include "Core\Containers\Hash.h"
#include "Core\Memory\MemoryApi.h"

namespace Engine
{

/**
 * @brief 文字列
 * 短い文字列は内部バッファへ格納し、長い場合のみエンジンのヒープから確保する
 */
class String
{
public:

    //! 内部バッファへ収まる最大文字数（終端文字を除く）
    static constexpr size_t INLINE_CAPACITY = 31;

    //! 見つからなかったことを表す位置
    static constexpr size_t NPOS = ~static_cast<size_t>(0);

    String();
    String(const char* text);
    String(const char* text, size_t size);
    String(std::string_view text);
    String(const String& other);
    String(String&& other) noexcept;
    ~String();

    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    String& operator=(const char* text);

    /**
     * @brief 書式指定で文字列を生成する
     * @param format フォーマット文字列
     * @param args フォーマット引数
     * @return String 生成した文字列
     */
    template <class... Args>
    static String format(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        const std::string result = spdlog::fmt_lib::format(format, std::forward<Args>(args)...);
        return String(result.data(), result.size());
    }

    /**
     * @brief ワイド文字列から生成する（UTF-8へ変換する）
     * @param text 変換元の文字列
     * @return String 生成した文字列
     */
    static String fromWide(const wchar_t* text);

    /**
     * @brief ワイド文字列へ変換する（UTF-8から変換する）
     * @return std::wstring 変換後の文字列
     */
    std::wstring toWide() const;

    /**
     * @brief 容量を確保する
     * @param capacity 確保する文字数（終端文字を除く）
     */
    void reserve(size_t capacity);

    /**
     * @brief 文字数を変更する
     * @param size 新しい文字数
     * @param fill 増えた分を埋める文字
     */
    void resize(size_t size, char fill = ' ');

    /**
     * @brief 内容を空にする（容量は維持する）
     */
    void clear();

    /**
     * @brief 文字列を末尾へ追加する
     * @param text 追加する文字列
     * @param size 追加する文字数
     */
    void append(const char* text, size_t size);

    void append(std::string_view text) { append(text.data(), text.size()); }
    void append(const String& text) { append(text.c_str(), text.size()); }
    void append(char character) { append(&character, 1); }

    String& operator+=(const char* text) { append(text, textLength(text)); return *this; }
    String& operator+=(std::string_view text) { append(text); return *this; }
    String& operator+=(const String& text) { append(text); return *this; }
    String& operator+=(char character) { append(character); return *this; }

    /**
     * @brief 部分文字列を取り出す
     * @param offset 開始位置
     * @param count 文字数
     * @return String 取り出した文字列
     */
    String substring(size_t offset, size_t count = NPOS) const;

    /**
     * @brief 文字列を検索する
     * @param text 探す文字列
     * @param offset 検索の開始位置
     * @return size_t 見つかった位置。見つからなければNPOS
     */
    size_t find(std::string_view text, size_t offset = 0) const;

    /**
     * @brief 文字を検索する
     * @param character 探す文字
     * @param offset 検索の開始位置
     * @return size_t 見つかった位置。見つからなければNPOS
     */
    size_t find(char character, size_t offset = 0) const;

    bool startsWith(std::string_view text) const;
    bool endsWith(std::string_view text) const;
    bool contains(std::string_view text) const { return find(text) != NPOS; }

    /**
     * @brief 前後の空白を取り除いた文字列を返す
     * @return String 取り除いた文字列
     */
    String trimmed() const;

    /**
     * @brief 英字を大文字に変換した文字列を返す
     * @return String 変換後の文字列
     */
    String toUpper() const;

    /**
     * @brief 英字を小文字に変換した文字列を返す
     * @return String 変換後の文字列
     */
    String toLower() const;

    const char* c_str() const { return m_data; }
    char* data() { return m_data; }
    const char* data() const { return m_data; }

    size_t size() const { return m_size; }
    size_t length() const { return m_size; }
    size_t capacity() const { return m_capacity; }
    bool isEmpty() const { return m_size == 0; }

    char& operator[](size_t index) { GE_ASSERT(index < m_size); return m_data[index]; }
    const char& operator[](size_t index) const { GE_ASSERT(index < m_size); return m_data[index]; }

    std::string_view view() const { return std::string_view(m_data, m_size); }
    operator std::string_view() const { return view(); }

    char* begin() { return m_data; }
    char* end() { return m_data + m_size; }
    const char* begin() const { return m_data; }
    const char* end() const { return m_data + m_size; }

    bool operator==(const String& other) const { return view() == other.view(); }
    bool operator!=(const String& other) const { return !(*this == other); }
    bool operator<(const String& other) const { return view() < other.view(); }
    bool operator==(std::string_view other) const { return view() == other; }
    bool operator!=(std::string_view other) const { return view() != other; }
    bool operator==(const char* other) const { return view() == std::string_view(other != nullptr ? other : ""); }
    bool operator!=(const char* other) const { return !(*this == other); }

private:

    /**
     * @brief null終端文字列の長さを求める
     * @param text 対象の文字列
     * @return size_t 文字数
     */
    static size_t textLength(const char* text) { return text != nullptr ? std::strlen(text) : 0; }

    /**
     * @brief 内部バッファを使用しているかを取得する
     * @return bool 内部バッファならtrue
     */
    bool isInline() const { return m_data == m_inline; }

    void assign(const char* text, size_t size);
    void releaseStorage();

    char*  m_data;                        //!< 文字列の格納先（内部バッファかヒープを指す）
    size_t m_size;                        //!< 文字数
    size_t m_capacity;                    //!< 確保済みの容量（終端文字を除く）
    char   m_inline[INLINE_CAPACITY + 1]; //!< 短い文字列用の内部バッファ
};

inline String operator+(const String& left, std::string_view right)
{
    String result(left);
    result.append(right);
    return result;
}

inline String operator+(const String& left, const char* right)
{
    String result(left);
    result += right;
    return result;
}

template <>
struct Hash<String>
{
    uint64_t operator()(const String& value) const { return hashString(value.data(), value.size()); }
};

} // namespace Engine
