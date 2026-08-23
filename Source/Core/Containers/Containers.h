#pragma once

// コンテナ機能の集約ヘッダ

#include <span>

#include "Core\Containers\Hash.h"
#include "Core\Containers\Array.h"
#include "Core\Containers\StaticArray.h"
#include "Core\Containers\String.h"
#include "Core\Containers\HashMap.h"
#include "Core\Containers\HashSet.h"
#include "Core\Containers\RingBuffer.h"

namespace Engine
{
    //! 連続したメモリへの参照
    template <class T>
    using Span = std::span<T>;

    //! 文字列への参照
    using StringView = std::string_view;
} // namespace Engine
