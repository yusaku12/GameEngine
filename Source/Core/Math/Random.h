#pragma once

#include <cstdint>

#include "Core\Math\MathTypes.h"

namespace Engine
{

/**
 * @brief 疑似乱数生成器
 * PCG32を用いており、同じシードからは常に同じ系列を生成する
 */
class Random
{
public:

    Random();

    /**
     * @brief シードを指定して構築する
     * @param seed シード値
     */
    explicit Random(uint64_t seed);

    /**
     * @brief 全体で共有する乱数生成器を取得する
     * @return Random& 乱数生成器
     */
    static Random& global();

    /**
     * @brief シードを設定する
     * @param seed シード値
     */
    void setSeed(uint64_t seed);

    /**
     * @brief 32bitの乱数を取得する
     * @return uint32_t 乱数
     */
    uint32_t nextUInt();

    /**
     * @brief 64bitの乱数を取得する
     * @return uint64_t 乱数
     */
    uint64_t nextUInt64();

    /**
     * @brief 0以上1未満の乱数を取得する
     * @return float 乱数
     */
    float nextFloat();

    /**
     * @brief 指定範囲の整数を取得する
     * @param low 下限（含む）
     * @param high 上限（含む）
     * @return int32_t 乱数
     */
    int32_t range(int32_t low, int32_t high);

    /**
     * @brief 指定範囲の実数を取得する
     * @param low 下限（含む）
     * @param high 上限（含まない）
     * @return float 乱数
     */
    float range(float low, float high);

    /**
     * @brief 真偽値を取得する
     * @param probability trueになる確率
     * @return bool 乱数
     */
    bool nextBool(float probability = 0.5f);

    /**
     * @brief 単位円の内部の点を取得する
     * @return Vector2 点
     */
    Vector2 insideUnitCircle();

    /**
     * @brief 単位球の内部の点を取得する
     * @return Vector3 点
     */
    Vector3 insideUnitSphere();

    /**
     * @brief 単位球面上の点を取得する
     * @return Vector3 点
     */
    Vector3 onUnitSphere();

    /**
     * @brief 一様な回転を取得する
     * @return Quaternion 回転
     */
    Quaternion rotation();

    /**
     * @brief ランダムな色を取得する（アルファは1固定）
     * @return Color 色
     */
    Color color();

private:

    uint64_t m_state = 0;     //!< 内部状態
    uint64_t m_increment = 0; //!< 系列を決める増分（奇数）
};

} // namespace Engine
