#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    //! FNV-1aのオフセット基底
    static constexpr uint64_t HASH_OFFSET_BASIS = static_cast<uint64_t>(14695981039346656037);

    //! FNV-1aの素数
    static constexpr uint64_t HASH_PRIME = static_cast<uint64_t>(1099511628211);

    /**
     * @brief バイト列のハッシュ値を求める（FNV-1a）
     * @param data 対象のデータ
     * @param size データのサイズ（バイト）
     * @return uint64_t ハッシュ値
     */
    inline uint64_t hashBytes(const void* data, size_t size)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        uint64_t hash = HASH_OFFSET_BASIS;

        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(bytes[index]);
            hash *= HASH_PRIME;
        }

        return hash;
    }

    /**
     * @brief 文字列のハッシュ値を求める（コンパイル時に評価できる）
     * @param text 対象の文字列
     * @param size 文字数
     * @return uint64_t ハッシュ値
     */
    inline constexpr uint64_t hashString(const char* text, size_t size)
    {
        uint64_t hash = HASH_OFFSET_BASIS;

        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(text[index]));
            hash *= HASH_PRIME;
        }

        return hash;
    }

    /**
     * @brief 文字列のハッシュ値を求める（コンパイル時に評価できる）
     * @param text 対象の文字列（null終端）
     * @return uint64_t ハッシュ値
     */
    inline constexpr uint64_t hashString(const char* text)
    {
        uint64_t hash = HASH_OFFSET_BASIS;

        while (*text != '\0')
        {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*text));
            hash *= HASH_PRIME;
            ++text;
        }

        return hash;
    }

    /**
     * @brief 整数のハッシュ値を求める（splitmix64の最終化）
     * @param value 対象の値
     * @return uint64_t ハッシュ値
     */
    inline constexpr uint64_t hashInteger(uint64_t value)
    {
        value ^= value >> 30;
        value *= static_cast<uint64_t>(0xBF58476D1CE4E5B9);
        value ^= value >> 27;
        value *= static_cast<uint64_t>(0x94D049BB133111EB);
        value ^= value >> 31;
        return value;
    }

    /**
     * @brief 2つのハッシュ値を合成する
     * @param seed 合成先のハッシュ値
     * @param value 合成するハッシュ値
     * @return uint64_t 合成後のハッシュ値
     */
    inline constexpr uint64_t hashCombine(uint64_t seed, uint64_t value)
    {
        return seed ^ (value + static_cast<uint64_t>(0x9E3779B97F4A7C15) + (seed << 6) + (seed >> 2));
    }

    /**
     * @brief ハッシュ関数オブジェクト
     * 独自の型で連想コンテナを使う場合はこのテンプレートを特殊化する
     */
    template <class T>
    struct Hash
    {
        uint64_t operator()(const T& value) const
        {
            if constexpr (std::is_enum_v<T>)
            {
                return hashInteger(static_cast<uint64_t>(value));
            }
            else if constexpr (std::is_integral_v<T>)
            {
                return hashInteger(static_cast<uint64_t>(value));
            }
            else if constexpr (std::is_pointer_v<T>)
            {
                return hashInteger(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value)));
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // -0.0と+0.0を同じハッシュ値にする
                const T normalized = value == static_cast<T>(0) ? static_cast<T>(0) : value;
                return hashBytes(&normalized, sizeof(normalized));
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<T>, "この型ではHash<T>の特殊化が必要です");
                return hashBytes(&value, sizeof(value));
            }
        }
    };

    template <>
    struct Hash<std::string_view>
    {
        uint64_t operator()(std::string_view value) const { return hashString(value.data(), value.size()); }
    };

    template <>
    struct Hash<const char*>
    {
        uint64_t operator()(const char* value) const { return value != nullptr ? hashString(value) : 0; }
    };
} // namespace Engine
