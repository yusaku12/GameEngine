#pragma once

#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief CameraのProjection方式。
     */
    enum class CameraProjectionMode
    {
        Perspective,
        Orthographic
    };

    /**
     * @brief CameraのClear方式。
     * @details SkyboxはDeferred時にSky Atmosphereがあれば描画し、なければ背景色を使う。
     */
    enum class CameraClearMode
    {
        Skybox,
        SolidColor,
        DepthOnly,
        Nothing
    };

    /**
     * @brief 描画先に対する正規化Viewport。
     */
    struct CameraViewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };
} // namespace Engine
