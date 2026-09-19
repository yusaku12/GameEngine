#pragma once

#include <type_traits>
#include <cstdint>

/**
 * @brief enum class をビットフラグとして扱うための汎用クラス。
 * @tparam Enum ビットフラグとして使用する enum class。
 *              各列挙子の値は 1, 2, 4, 8, ... のように
 *              2のべき乗（1ビットのみが立った値）である必要があります。
 *
 * 使用例:
 * @code
 * enum class Flag : uint32_t
 * {
 *     None  = 0,
 *     Red   = 1 << 0,
 *     Green = 1 << 1,
 *     Blue  = 1 << 2,
 * };
 * BITFLAGS_ENABLE(Flag); // enum同士の "|" 演算を有効化（任意）
 *
 * BitFlags<Flag> flags(Flag::Red);
 * flags.set(Flag::Green);
 * if (flags.has(Flag::Red)) { ... }
 *
 * // BITFLAGS_ENABLE 済みなら enum 同士を直接 "|" で組み合わせられる
 * BitFlags<Flag> rg = Flag::Red | Flag::Green;
 * @endcode
 */
template <typename Enum>
class BitFlags
{
    static_assert(std::is_enum_v<Enum>, "BitFlags requires an enum type.");

public:

    using UnderlyingType = std::underlying_type_t<Enum>;

    /**
     * @brief 空のBitFlagsを生成します。
     */
    constexpr BitFlags() noexcept
        : m_flags(0)
    {
    }

    /**
     * @brief 指定したフラグを設定して生成します。
     * @param flag 設定するフラグ。
     */
    constexpr BitFlags(Enum flag) noexcept
        : m_flags(toUnderlying(flag))
    {
    }

    /**
     * @brief 指定した値からBitFlagsを生成します。
     * @param value 内部ビット値。
     */
    explicit constexpr BitFlags(UnderlyingType value) noexcept
        : m_flags(value)
    {
    }

    /**
     * @brief フラグを設定します。
     * @param flag 設定するフラグ。
     */
    constexpr void set(Enum flag) noexcept
    {
        m_flags |= toUnderlying(flag);
    }

    /**
     * @brief フラグを解除します。
     * @param flag 解除するフラグ。
     */
    constexpr void reset(Enum flag) noexcept
    {
        m_flags &= static_cast<UnderlyingType>(~toUnderlying(flag));
    }

    /**
     * @brief フラグのON/OFFを反転します。
     * @param flag 反転するフラグ。
     */
    constexpr void toggle(Enum flag) noexcept
    {
        m_flags ^= toUnderlying(flag);
    }

    /**
     * @brief すべてのフラグを解除します。
     */
    constexpr void clear() noexcept
    {
        m_flags = 0;
    }

    /**
     * @brief 指定したフラグがすべて設定されているか確認します。
     * @param flag 確認するフラグ（複数ビットの組み合わせも可）。
     * @return すべて設定されていればtrue。
     */
    constexpr bool has(Enum flag) const noexcept
    {
        const UnderlyingType value = toUnderlying(flag);
        return (m_flags & value) == value;
    }

    /**
     * @brief 指定したフラグのいずれかが設定されているか確認します。
     * @param flag 確認するフラグ。
     * @return 1つでも設定されていればtrue。
     */
    constexpr bool hasAny(Enum flag) const noexcept
    {
        return (m_flags & toUnderlying(flag)) != 0;
    }

    /**
     * @brief フラグが1つ以上設定されているか確認します。
     * @return フラグが設定されていればtrue。
     */
    constexpr bool any() const noexcept
    {
        return m_flags != 0;
    }

    /**
     * @brief フラグが1つも設定されていないか確認します。
     * @return 何も設定されていなければtrue。
     */
    constexpr bool none() const noexcept
    {
        return m_flags == 0;
    }

    /**
     * @brief 内部のビット値を取得します。
     * @return 内部ビット値。
     */
    constexpr UnderlyingType value() const noexcept
    {
        return m_flags;
    }

    /**
     * @brief 内部のビット値を設定します。
     * @param value 設定するビット値。
     */
    constexpr void assign(UnderlyingType value) noexcept
    {
        m_flags = value;
    }

    /**
     * @brief 指定したフラグを追加します。
     */
    constexpr BitFlags& operator|=(Enum flag) noexcept
    {
        set(flag);
        return *this;
    }

    /**
     * @brief 指定したBitFlagsの内容を追加します。
     */
    constexpr BitFlags& operator|=(BitFlags other) noexcept
    {
        m_flags |= other.m_flags;
        return *this;
    }

    /**
     * @brief 指定したフラグとの論理積を取ります。
     */
    constexpr BitFlags& operator&=(Enum flag) noexcept
    {
        m_flags &= toUnderlying(flag);
        return *this;
    }

    /**
     * @brief 指定したBitFlagsとの論理積を取ります。
     */
    constexpr BitFlags& operator&=(BitFlags other) noexcept
    {
        m_flags &= other.m_flags;
        return *this;
    }

    /**
     * @brief 指定したフラグを反転します。
     */
    constexpr BitFlags& operator^=(Enum flag) noexcept
    {
        toggle(flag);
        return *this;
    }

    /**
     * @brief 指定したBitFlagsとの排他的論理和を取ります。
     */
    constexpr BitFlags& operator^=(BitFlags other) noexcept
    {
        m_flags ^= other.m_flags;
        return *this;
    }

    /**
     * @brief 全ビットを反転した新しいBitFlagsを返します。
     */
    constexpr BitFlags operator~() const noexcept
    {
        return BitFlags(static_cast<UnderlyingType>(~m_flags));
    }

    friend constexpr BitFlags operator|(BitFlags lhs, BitFlags rhs) noexcept { return lhs |= rhs; }
    friend constexpr BitFlags operator|(BitFlags lhs, Enum rhs) noexcept { return lhs |= rhs; }
    friend constexpr BitFlags operator&(BitFlags lhs, BitFlags rhs) noexcept { return lhs &= rhs; }
    friend constexpr BitFlags operator&(BitFlags lhs, Enum rhs) noexcept { return lhs &= rhs; }
    friend constexpr BitFlags operator^(BitFlags lhs, BitFlags rhs) noexcept { return lhs ^= rhs; }
    friend constexpr BitFlags operator^(BitFlags lhs, Enum rhs) noexcept { return lhs ^= rhs; }

    friend constexpr bool operator==(BitFlags lhs, BitFlags rhs) noexcept { return lhs.m_flags == rhs.m_flags; }
    friend constexpr bool operator!=(BitFlags lhs, BitFlags rhs) noexcept { return !(lhs == rhs); }

    /**
     * @brief boolへの明示的な変換（フラグが1つでも立っていればtrue）。
     */
    explicit constexpr operator bool() const noexcept
    {
        return any();
    }

    /**
     * @brief enum値を基底型へ変換します。
     */
    static constexpr UnderlyingType toUnderlying(Enum flag) noexcept
    {
        return static_cast<UnderlyingType>(flag);
    }

private:

    UnderlyingType m_flags; //!< 内部のビット値
};

namespace bitflags_detail
{
    // BITFLAGS_ENABLE(Enum) を呼んだ enum 型に対してのみ true になるトレイト。
    // これにより「意図せず他の enum 同士に operator| が効いてしまう」事故を防ぎます。
    template <typename Enum>
    struct IsBitFlagEnum : std::false_type
    {
    };
}

/**
 * @brief 指定した enum class に対して、Enum同士の "|", "&", "^" 演算子で
 *        直接 BitFlags<Enum> を組み立てられるようにします。
 *        グローバルスコープ、enum定義の直後に1回だけ記述してください。
 */
#define BITFLAGS_ENABLE(EnumType)                                  \
    namespace bitflags_detail                                      \
    {                                                              \
        template <>                                                \
        struct IsBitFlagEnum<EnumType> : std::true_type            \
        {                                                          \
        };                                                         \
    }

template <typename Enum, typename = std::enable_if_t<bitflags_detail::IsBitFlagEnum<Enum>::value>>
constexpr BitFlags<Enum> operator|(Enum lhs, Enum rhs) noexcept
{
    return BitFlags<Enum>(lhs) | rhs;
}

template <typename Enum, typename = std::enable_if_t<bitflags_detail::IsBitFlagEnum<Enum>::value>>
constexpr BitFlags<Enum> operator&(Enum lhs, Enum rhs) noexcept
{
    return BitFlags<Enum>(lhs) & rhs;
}

template <typename Enum, typename = std::enable_if_t<bitflags_detail::IsBitFlagEnum<Enum>::value>>
constexpr BitFlags<Enum> operator^(Enum lhs, Enum rhs) noexcept
{
    return BitFlags<Enum>(lhs) ^ rhs;
}
