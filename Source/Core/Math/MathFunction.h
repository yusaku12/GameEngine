#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Core\CoreDefines.h"

namespace Engine
{

static constexpr float PI = 3.14159265358979323846f;         //!< 円周率
static constexpr float TWO_PI = PI * 2.0f;                   //!< 円周率の2倍
static constexpr float HALF_PI = PI * 0.5f;                  //!< 円周率の半分
static constexpr float DEG_TO_RAD = PI / 180.0f;             //!< 度からラジアンへの変換係数
static constexpr float RAD_TO_DEG = 180.0f / PI;             //!< ラジアンから度への変換係数
static constexpr float EPSILON = 1.0e-6f;                    //!< 浮動小数点の比較に使う許容誤差

/**
 * @brief 小さい方の値を返す
 */
template <class T>
inline constexpr T minimum(T left, T right)
{
    return left < right ? left : right;
}

/**
 * @brief 大きい方の値を返す
 */
template <class T>
inline constexpr T maximum(T left, T right)
{
    return left > right ? left : right;
}

/**
 * @brief 値を範囲内へ収める
 * @param value 対象の値
 * @param low 下限
 * @param high 上限
 * @return T 範囲内へ収めた値
 */
template <class T>
inline constexpr T clamp(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

/**
 * @brief 値を0から1の範囲へ収める
 */
inline constexpr float saturate(float value)
{
    return clamp(value, 0.0f, 1.0f);
}

/**
 * @brief 線形補間する
 * @param from 開始値
 * @param to 終了値
 * @param alpha 補間係数
 * @return float 補間結果
 */
inline constexpr float lerp(float from, float to, float alpha)
{
    return from + (to - from) * alpha;
}

/**
 * @brief 補間係数を0から1へ収めたうえで線形補間する
 */
inline constexpr float lerpClamped(float from, float to, float alpha)
{
    return lerp(from, to, saturate(alpha));
}

/**
 * @brief 値が範囲のどこに位置するかを0から1で求める
 * @param from 範囲の開始値
 * @param to 範囲の終了値
 * @param value 対象の値
 * @return float 位置を表す係数
 */
inline constexpr float inverseLerp(float from, float to, float value)
{
    return to - from == 0.0f ? 0.0f : saturate((value - from) / (to - from));
}

/**
 * @brief 両端が滑らかになるよう補間する
 */
inline constexpr float smoothStep(float from, float to, float alpha)
{
    const float t = saturate(alpha);
    return lerp(from, to, t * t * (3.0f - 2.0f * t));
}

/**
 * @brief 度をラジアンへ変換する
 */
inline constexpr float toRadians(float degrees)
{
    return degrees * DEG_TO_RAD;
}

/**
 * @brief ラジアンを度へ変換する
 */
inline constexpr float toDegrees(float radians)
{
    return radians * RAD_TO_DEG;
}

/**
 * @brief 2つの値がほぼ等しいかを判定する
 * @param left 比較する値
 * @param right 比較する値
 * @param tolerance 許容誤差
 * @return bool ほぼ等しければtrue
 */
inline bool isNearlyEqual(float left, float right, float tolerance = EPSILON)
{
    return std::fabs(left - right) <= tolerance;
}

/**
 * @brief 値がほぼ0かを判定する
 */
inline bool isNearlyZero(float value, float tolerance = EPSILON)
{
    return std::fabs(value) <= tolerance;
}

/**
 * @brief 符号を求める
 * @return int 正なら1、負なら-1、0なら0
 */
template <class T>
inline constexpr int sign(T value)
{
    return value > T(0) ? 1 : (value < T(0) ? -1 : 0);
}

/**
 * @brief 値の2乗を求める
 */
template <class T>
inline constexpr T square(T value)
{
    return value * value;
}

/**
 * @brief 角度を-πからπの範囲へ収める
 * @param radians 角度（ラジアン）
 * @return float 収めた角度
 */
inline float wrapAngle(float radians)
{
    radians = std::fmod(radians + PI, TWO_PI);
    if (radians < 0.0f)
        radians += TWO_PI;

    return radians - PI;
}

/**
 * @brief 角度を最短経路で補間する
 * @param from 開始角度（ラジアン）
 * @param to 終了角度（ラジアン）
 * @param alpha 補間係数
 * @return float 補間結果（ラジアン）
 */
inline float lerpAngle(float from, float to, float alpha)
{
    return from + wrapAngle(to - from) * saturate(alpha);
}

/**
 * @brief 目標値へ一定量ずつ近づける
 * @param current 現在値
 * @param target 目標値
 * @param maxDelta 1回で変化できる最大量
 * @return float 近づけた後の値
 */
inline float moveTowards(float current, float target, float maxDelta)
{
    const float difference = target - current;
    if (std::fabs(difference) <= maxDelta)
        return target;

    return current + static_cast<float>(sign(difference)) * maxDelta;
}

/**
 * @brief 値を0から範囲の間で繰り返す
 */
inline float repeat(float value, float length)
{
    return clamp(value - std::floor(value / length) * length, 0.0f, length);
}

/**
 * @brief 値を0と範囲の間で往復させる
 */
inline float pingPong(float value, float length)
{
    const float wrapped = repeat(value, length * 2.0f);
    return length - std::fabs(wrapped - length);
}

/**
 * @brief 2の累乗かどうかを判定する
 */
inline constexpr bool isPowerOfTwo(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/**
 * @brief 指定値以上で最小の2の累乗を求める
 */
inline constexpr size_t nextPowerOfTwo(size_t value)
{
    if (value == 0)
        return 1;

    size_t result = 1;
    while (result < value)
        result <<= 1;

    return result;
}

} // namespace Engine
