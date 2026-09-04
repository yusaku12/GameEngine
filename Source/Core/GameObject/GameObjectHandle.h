#pragma once

#include <cstdint>

namespace Engine
{
    /**
     * @brief GameObjectの安全な識別子。
     *
     * indexはオブジェクトのインデックス、generationは再利用時に
     * 破棄済みハンドルとの誤アクセスを防ぐための世代番号である。
     */
    struct GameObjectHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return generation != 0;
        }

        bool operator==(const GameObjectHandle& other) const noexcept = default;
    };
} // namespace Engine
