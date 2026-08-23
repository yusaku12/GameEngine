#pragma once

#include <cstddef>
#include <cstdint>

namespace Engine
{

/**
 * @brief 2の累乗かどうかを判定する
 * @param value 判定する値
 * @return bool 2の累乗ならtrue
 */
inline constexpr bool memoryIsPowerOfTwo(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/**
 * @brief 値を指定アライメントへ切り上げる
 * @param value 対象の値
 * @param alignment アライメント（2の累乗であること）
 * @return size_t 切り上げ後の値
 */
inline constexpr size_t memoryAlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief ポインタを指定アライメントへ切り上げる
 * @param pointer 対象のポインタ
 * @param alignment アライメント（2の累乗であること）
 * @return void* 切り上げ後のポインタ
 */
inline void* memoryAlignPointer(void* pointer, size_t alignment)
{
    const size_t address = static_cast<size_t>(reinterpret_cast<uintptr_t>(pointer));
    return reinterpret_cast<void*>(static_cast<uintptr_t>(memoryAlignUp(address, alignment)));
}

/**
 * @brief アライメントを満たすために必要な調整量を求める
 * @param pointer 対象のポインタ
 * @param alignment アライメント（2の累乗であること）
 * @return size_t 調整量（バイト）
 */
inline size_t memoryAlignAdjustment(const void* pointer, size_t alignment)
{
    const size_t address = static_cast<size_t>(reinterpret_cast<uintptr_t>(pointer));
    return memoryAlignUp(address, alignment) - address;
}

/**
 * @brief ヘッダを手前に置いたうえでアライメントを満たす調整量を求める
 * @param pointer ブロック先頭のポインタ
 * @param alignment アライメント（2の累乗であること）
 * @param headerSize ユーザ領域の直前に必要なヘッダのサイズ
 * @return size_t ブロック先頭からユーザ領域までの調整量（バイト）
 */
inline size_t memoryAlignAdjustmentWithHeader(const void* pointer, size_t alignment, size_t headerSize)
{
    const size_t address = static_cast<size_t>(reinterpret_cast<uintptr_t>(pointer));
    return memoryAlignUp(address + headerSize, alignment) - address;
}

/**
 * @brief ポインタをバイト単位で進める
 * @param pointer 基準のポインタ
 * @param offset 進めるバイト数
 * @return void* 進めた後のポインタ
 */
inline void* memoryOffsetPointer(void* pointer, size_t offset)
{
    return static_cast<unsigned char*>(pointer) + offset;
}

/**
 * @brief 2つのポインタの差をバイト単位で求める
 * @param high 大きい方のポインタ
 * @param low 小さい方のポインタ
 * @return size_t 差（バイト）
 */
inline size_t memoryPointerDistance(const void* high, const void* low)
{
    return static_cast<size_t>(static_cast<const unsigned char*>(high) - static_cast<const unsigned char*>(low));
}

/**
 * @brief 型に対して使用すべきアライメントを求める
 * @return size_t アライメント（バイト）
 */
template <class T>
inline constexpr size_t memoryAlignmentOf()
{
    return alignof(T) < alignof(std::max_align_t) ? alignof(std::max_align_t) : alignof(T);
}

/**
 * @brief 固定長バッファへ名前をコピーする
 * @param destination コピー先
 * @param capacity コピー先の容量（終端文字を含む）
 * @param source コピー元
 */
inline void memoryCopyName(char* destination, size_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0)
        return;

    size_t index = 0;
    if (source != nullptr)
    {
        while (index + 1 < capacity && source[index] != '\0')
        {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

} // namespace Engine
