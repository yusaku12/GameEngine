#pragma once

#include "Core\Math\SimpleMath.h"

namespace Engine
{
    //! 2次元ベクトル
    using Vector2 = DirectX::SimpleMath::Vector2;

    //! 3次元ベクトル
    using Vector3 = DirectX::SimpleMath::Vector3;

    //! 4次元ベクトル
    using Vector4 = DirectX::SimpleMath::Vector4;

    //! 4x4行列（行優先）
    using Matrix = DirectX::SimpleMath::Matrix;

    //! クォータニオン
    using Quaternion = DirectX::SimpleMath::Quaternion;

    //! 平面
    using Plane = DirectX::SimpleMath::Plane;

    //! 半直線
    using Ray = DirectX::SimpleMath::Ray;

    //! RGBAカラー
    using Color = DirectX::SimpleMath::Color;

    //! 矩形
    using Rect = DirectX::SimpleMath::Rectangle;

    //! ビューポート
    using Viewport = DirectX::SimpleMath::Viewport;
} // namespace Engine
