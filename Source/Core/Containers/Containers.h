#pragma once

// コンテナ機能の集約ヘッダ

#include <span>

#include "Core\Containers\RingBuffer.h"
#include "Core\Containers\String.h"

namespace Engine
{
    //! 連続したメモリへの参照
    template <class T>
    using Span = std::span<T>;

    //! 文字列への参照
    using StringView = std::string_view;
} // namespace Engine
