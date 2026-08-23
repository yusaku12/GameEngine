#pragma once

#include "Core\Math\MathFunction.h"

namespace Engine
{
    /**
     * @brief イージングの種類
     */
    enum class EaseType : uint8_t
    {
        LINEAR,        //!< 線形
        QUAD_IN,       //!< 2次（加速）
        QUAD_OUT,      //!< 2次（減速）
        QUAD_IN_OUT,   //!< 2次（加速して減速）
        CUBIC_IN,      //!< 3次（加速）
        CUBIC_OUT,     //!< 3次（減速）
        CUBIC_IN_OUT,  //!< 3次（加速して減速）
        QUART_IN,      //!< 4次（加速）
        QUART_OUT,     //!< 4次（減速）
        QUART_IN_OUT,  //!< 4次（加速して減速）
        SINE_IN,       //!< 正弦（加速）
        SINE_OUT,      //!< 正弦（減速）
        SINE_IN_OUT,   //!< 正弦（加速して減速）
        EXPO_IN,       //!< 指数（加速）
        EXPO_OUT,      //!< 指数（減速）
        EXPO_IN_OUT,   //!< 指数（加速して減速）
        CIRC_IN,       //!< 円（加速）
        CIRC_OUT,      //!< 円（減速）
        CIRC_IN_OUT,   //!< 円（加速して減速）
        BACK_IN,       //!< 行き過ぎてから戻る（開始時）
        BACK_OUT,      //!< 行き過ぎてから戻る（終了時）
        ELASTIC_IN,    //!< 弾性（開始時）
        ELASTIC_OUT,   //!< 弾性（終了時）
        BOUNCE_IN,     //!< 跳ね返り（開始時）
        BOUNCE_OUT,    //!< 跳ね返り（終了時）
        COUNT,         //!< 種類の総数
    };

    inline constexpr float easeLinear(float t) { return t; }

    inline constexpr float easeQuadIn(float t) { return t * t; }
    inline constexpr float easeQuadOut(float t) { return t * (2.0f - t); }
    inline constexpr float easeQuadInOut(float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }

    inline constexpr float easeCubicIn(float t) { return t * t * t; }
    inline constexpr float easeCubicOut(float t) { const float f = t - 1.0f; return f * f * f + 1.0f; }
    inline constexpr float easeCubicInOut(float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f + 4.0f * square(t - 1.0f) * (t - 1.0f); }

    inline constexpr float easeQuartIn(float t) { return t * t * t * t; }
    inline constexpr float easeQuartOut(float t) { const float f = t - 1.0f; return 1.0f - f * f * f * f; }
    inline constexpr float easeQuartInOut(float t) { const float f = t - 1.0f; return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * f * f * f * f; }

    inline float easeSineIn(float t) { return 1.0f - std::cos(t * HALF_PI); }
    inline float easeSineOut(float t) { return std::sin(t * HALF_PI); }
    inline float easeSineInOut(float t) { return -0.5f * (std::cos(PI * t) - 1.0f); }

    inline float easeExpoIn(float t) { return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
    inline float easeExpoOut(float t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    inline float easeExpoInOut(float t)
    {
        if (t <= 0.0f)
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;

        return t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
    }

    inline float easeCircIn(float t) { return 1.0f - std::sqrt(maximum(0.0f, 1.0f - t * t)); }
    inline float easeCircOut(float t) { return std::sqrt(maximum(0.0f, 1.0f - square(t - 1.0f))); }
    inline float easeCircInOut(float t)
    {
        return t < 0.5f
            ? 0.5f * (1.0f - std::sqrt(maximum(0.0f, 1.0f - 4.0f * t * t)))
            : 0.5f * (std::sqrt(maximum(0.0f, 1.0f - square(-2.0f * t + 2.0f))) + 1.0f);
    }

    inline constexpr float easeBackIn(float t)
    {
        constexpr float overshoot = 1.70158f;
        return t * t * ((overshoot + 1.0f) * t - overshoot);
    }

    inline constexpr float easeBackOut(float t)
    {
        constexpr float overshoot = 1.70158f;
        const float f = t - 1.0f;
        return f * f * ((overshoot + 1.0f) * f + overshoot) + 1.0f;
    }

    inline float easeElasticOut(float t)
    {
        if (t <= 0.0f)
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;

        constexpr float period = 0.3f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t - period * 0.25f) * TWO_PI / period) + 1.0f;
    }

    inline float easeElasticIn(float t)
    {
        return 1.0f - easeElasticOut(1.0f - t);
    }

    inline constexpr float easeBounceOut(float t)
    {
        constexpr float scale = 7.5625f;
        constexpr float divider = 2.75f;

        if (t < 1.0f / divider)
            return scale * t * t;

        if (t < 2.0f / divider)
        {
            const float f = t - 1.5f / divider;
            return scale * f * f + 0.75f;
        }

        if (t < 2.5f / divider)
        {
            const float f = t - 2.25f / divider;
            return scale * f * f + 0.9375f;
        }

        const float f = t - 2.625f / divider;
        return scale * f * f + 0.984375f;
    }

    inline constexpr float easeBounceIn(float t)
    {
        return 1.0f - easeBounceOut(1.0f - t);
    }

    /**
     * @brief 指定した種類のイージングを評価する
     * @param type イージングの種類
     * @param alpha 0から1の補間係数
     * @return float 補間後の係数
     */
    inline float ease(EaseType type, float alpha)
    {
        const float t = saturate(alpha);

        switch (type)
        {
        case EaseType::LINEAR:       return easeLinear(t);
        case EaseType::QUAD_IN:      return easeQuadIn(t);
        case EaseType::QUAD_OUT:     return easeQuadOut(t);
        case EaseType::QUAD_IN_OUT:  return easeQuadInOut(t);
        case EaseType::CUBIC_IN:     return easeCubicIn(t);
        case EaseType::CUBIC_OUT:    return easeCubicOut(t);
        case EaseType::CUBIC_IN_OUT: return easeCubicInOut(t);
        case EaseType::QUART_IN:     return easeQuartIn(t);
        case EaseType::QUART_OUT:    return easeQuartOut(t);
        case EaseType::QUART_IN_OUT: return easeQuartInOut(t);
        case EaseType::SINE_IN:      return easeSineIn(t);
        case EaseType::SINE_OUT:     return easeSineOut(t);
        case EaseType::SINE_IN_OUT:  return easeSineInOut(t);
        case EaseType::EXPO_IN:      return easeExpoIn(t);
        case EaseType::EXPO_OUT:     return easeExpoOut(t);
        case EaseType::EXPO_IN_OUT:  return easeExpoInOut(t);
        case EaseType::CIRC_IN:      return easeCircIn(t);
        case EaseType::CIRC_OUT:     return easeCircOut(t);
        case EaseType::CIRC_IN_OUT:  return easeCircInOut(t);
        case EaseType::BACK_IN:      return easeBackIn(t);
        case EaseType::BACK_OUT:     return easeBackOut(t);
        case EaseType::ELASTIC_IN:   return easeElasticIn(t);
        case EaseType::ELASTIC_OUT:  return easeElasticOut(t);
        case EaseType::BOUNCE_IN:    return easeBounceIn(t);
        case EaseType::BOUNCE_OUT:   return easeBounceOut(t);
        default:                     return t;
        }
    }

    /**
     * @brief イージングを適用して補間する
     * @param from 開始値
     * @param to 終了値
     * @param alpha 0から1の補間係数
     * @param type イージングの種類
     * @return float 補間結果
     */
    inline float easeLerp(float from, float to, float alpha, EaseType type)
    {
        return lerp(from, to, ease(type, alpha));
    }
} // namespace Engine
