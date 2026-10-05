#include "Pch.h"
#include "Core\Math\MathFunction.h"
#include "Core\Math\Random.h"

namespace Engine
{
    //! PCG32の乗数
    static constexpr uint64_t RANDOM_MULTIPLIER = static_cast<uint64_t>(6364136223846793005);

    //! 既定の系列を決める増分
    static constexpr uint64_t RANDOM_DEFAULT_INCREMENT = static_cast<uint64_t>(1442695040888963407);

    Random::Random()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
        setSeed(static_cast<uint64_t>(now.count()));
    }

    Random::Random(uint64_t seed)
    {
        setSeed(seed);
    }

    Random& Random::global()
    {
        static Random instance;
        return instance;
    }

    void Random::setSeed(uint64_t seed)
    {
        m_state = 0;
        m_increment = (RANDOM_DEFAULT_INCREMENT << 1) | 1;

        nextUInt();
        m_state += seed;
        nextUInt();
    }

    uint32_t Random::nextUInt()
    {
        const uint64_t previous = m_state;
        m_state = previous * RANDOM_MULTIPLIER + m_increment;

        const uint32_t xorShifted = static_cast<uint32_t>(((previous >> 18) ^ previous) >> 27);
        const uint32_t rotation = static_cast<uint32_t>(previous >> 59);

        return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
    }

    uint64_t Random::nextUInt64()
    {
        const uint64_t high = static_cast<uint64_t>(nextUInt());
        return (high << 32) | static_cast<uint64_t>(nextUInt());
    }

    float Random::nextFloat()
    {
        // 24bitの精度でfloatの[0,1)へ落とし込む
        return static_cast<float>(nextUInt() >> 8) * (1.0f / 16777216.0f);
    }

    int32_t Random::range(int32_t low, int32_t high)
    {
        if (low >= high)
            return low;

        const std::uint64_t span = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(high) - static_cast<std::int64_t>(low)) + 1u;
        constexpr std::uint64_t UINT32_RANGE = std::uint64_t{ 1 } << 32;
        const std::uint64_t rejectionThreshold = UINT32_RANGE % span;
        std::uint32_t value = nextUInt();
        while (static_cast<std::uint64_t>(value) < rejectionThreshold)
            value = nextUInt();

        const std::int64_t result = static_cast<std::int64_t>(low)
            + static_cast<std::int64_t>(static_cast<std::uint64_t>(value) % span);
        return static_cast<std::int32_t>(result);
    }

    float Random::range(float low, float high)
    {
        if (!std::isfinite(low) || !std::isfinite(high))
        {
            LOG_WARNING("[Random] Invalid floating-point range [{}, {}); returning a finite bound.", low, high);
            if (std::isfinite(low))
                return low;
            if (std::isfinite(high))
                return high;
            return 0.0f;
        }
        if (low >= high)
            return low;

        const double value = static_cast<double>(low)
            + (static_cast<double>(high) - static_cast<double>(low)) * static_cast<double>(nextFloat());
        const float result = static_cast<float>(value);
        return result < high ? result : std::nextafter(high, low);
    }

    bool Random::nextBool(float probability)
    {
        if (!std::isfinite(probability) || probability < 0.0f || probability > 1.0f)
        {
            LOG_WARNING("[Random] Invalid probability {}; returning false.", probability);
            return false;
        }
        return nextFloat() < probability;
    }

    Vector2 Random::insideUnitCircle()
    {
        const float angle = range(0.0f, TWO_PI);
        const float radius = std::sqrt(nextFloat());

        return Vector2(std::cos(angle) * radius, std::sin(angle) * radius);
    }

    Vector3 Random::onUnitSphere()
    {
        const float z = range(-1.0f, 1.0f);
        const float angle = range(0.0f, TWO_PI);
        const float radius = std::sqrt(maximum(0.0f, 1.0f - z * z));

        return Vector3(std::cos(angle) * radius, std::sin(angle) * radius, z);
    }

    Vector3 Random::insideUnitSphere()
    {
        return onUnitSphere() * std::cbrt(nextFloat());
    }

    Quaternion Random::rotation()
    {
        // Shoemakeの一様なクォータニオン生成法
        const float u1 = nextFloat();
        const float u2 = range(0.0f, TWO_PI);
        const float u3 = range(0.0f, TWO_PI);

        const float sqrt1 = std::sqrt(1.0f - u1);
        const float sqrt2 = std::sqrt(u1);

        return Quaternion(sqrt1 * std::sin(u2), sqrt1 * std::cos(u2), sqrt2 * std::sin(u3), sqrt2 * std::cos(u3));
    }

    Color Random::color()
    {
        return Color(nextFloat(), nextFloat(), nextFloat(), 1.0f);
    }
} // namespace Engine