#include "Pch.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    namespace
    {
        bool isFinite(const Matrix& matrix) noexcept
        {
            const std::array<float, 16> elements{
                matrix._11, matrix._12, matrix._13, matrix._14,
                matrix._21, matrix._22, matrix._23, matrix._24,
                matrix._31, matrix._32, matrix._33, matrix._34,
                matrix._41, matrix._42, matrix._43, matrix._44
            };
            return std::ranges::all_of(elements, [](const float value) { return std::isfinite(value); });
        }

        bool isRepresentedBy(const Matrix& candidate, const Matrix& source) noexcept
        {
            const std::array<float, 16> candidateElements{
                candidate._11, candidate._12, candidate._13, candidate._14,
                candidate._21, candidate._22, candidate._23, candidate._24,
                candidate._31, candidate._32, candidate._33, candidate._34,
                candidate._41, candidate._42, candidate._43, candidate._44
            };
            const std::array<float, 16> sourceElements{
                source._11, source._12, source._13, source._14,
                source._21, source._22, source._23, source._24,
                source._31, source._32, source._33, source._34,
                source._41, source._42, source._43, source._44
            };

            for (std::size_t index = 0; index < candidateElements.size(); ++index)
            {
                const float candidateValue = candidateElements[index];
                const float sourceValue = sourceElements[index];
                if (!std::isfinite(candidateValue) || !std::isfinite(sourceValue))
                    return false;

                const float scale = std::max({ 1.0f, std::abs(candidateValue), std::abs(sourceValue) });
                if (std::abs(candidateValue - sourceValue) > 1.0e-4f * scale)
                    return false;
            }
            return true;
        }
    }

    Transform::Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
        : m_position(position)
        , m_rotation(rotation)
        , m_scale(scale)
    {
    }

    Transform Transform::fromMatrix(const Matrix& matrix)
    {
        Transform result;
        Matrix source = matrix;
        const bool decomposed = source.Decompose(result.m_scale, result.m_rotation, result.m_position);
        const float rotationLengthSquared = result.m_rotation.LengthSquared();
        const bool validDecomposition = decomposed
            && std::isfinite(result.m_scale.x) && std::isfinite(result.m_scale.y) && std::isfinite(result.m_scale.z)
            && std::isfinite(result.m_rotation.x) && std::isfinite(result.m_rotation.y)
            && std::isfinite(result.m_rotation.z) && std::isfinite(result.m_rotation.w)
            && std::isfinite(rotationLengthSquared) && rotationLengthSquared > EPSILON
            && std::isfinite(result.m_position.x) && std::isfinite(result.m_position.y)
            && std::isfinite(result.m_position.z);

        if (!validDecomposition)
        {
            LOG_WARNING("[Transform] Matrix decomposition failed or produced invalid values; using a safe fallback.");
            const Vector3 translation = source.Translation();
            result.m_position = std::isfinite(translation.x) && std::isfinite(translation.y)
                && std::isfinite(translation.z) ? translation : Vector3::Zero;
            result.m_rotation = Quaternion::Identity;
            result.m_scale = Vector3::One;
        }

        return result;
    }

    bool Transform::tryFromMatrix(const Matrix& matrix, Transform& result)
    {
        if (!isFinite(matrix))
            return false;

        Transform candidate;
        Matrix source = matrix;
        if (!source.Decompose(candidate.m_scale, candidate.m_rotation, candidate.m_position))
            return false;

        const float rotationLengthSquared = candidate.m_rotation.LengthSquared();
        if (!std::isfinite(candidate.m_scale.x) || !std::isfinite(candidate.m_scale.y)
            || !std::isfinite(candidate.m_scale.z)
            || !std::isfinite(candidate.m_rotation.x) || !std::isfinite(candidate.m_rotation.y)
            || !std::isfinite(candidate.m_rotation.z) || !std::isfinite(candidate.m_rotation.w)
            || !std::isfinite(rotationLengthSquared) || rotationLengthSquared <= EPSILON
            || !std::isfinite(candidate.m_position.x) || !std::isfinite(candidate.m_position.y)
            || !std::isfinite(candidate.m_position.z)
            || !isRepresentedBy(candidate.toMatrix(), matrix))
            return false;

        result = candidate;
        return true;
    }

    Transform Transform::lerp(const Transform& from, const Transform& to, float alpha)
    {
        const float t = std::isfinite(alpha) ? saturate(alpha) : 0.0f;

        return Transform(
            Vector3::Lerp(from.m_position, to.m_position, t),
            Quaternion::Slerp(from.m_rotation, to.m_rotation, t),
            Vector3::Lerp(from.m_scale, to.m_scale, t));
    }

    Matrix Transform::toMatrix() const
    {
        return Matrix::CreateScale(m_scale) * Matrix::CreateFromQuaternion(m_rotation) * Matrix::CreateTranslation(m_position);
    }

    Matrix Transform::toInverseMatrix() const
    {
        const Matrix matrix = toMatrix();
        const float determinant = matrix.Determinant();
        if (!std::isfinite(determinant) || determinant == 0.0f)
        {
            LOG_WARNING("[Transform] Cannot invert singular or invalid transform matrix; returning identity.");
            return Matrix::Identity;
        }

        const Matrix inverse = matrix.Invert();
        const std::array<float, 16> inverseValues{
            inverse._11, inverse._12, inverse._13, inverse._14,
            inverse._21, inverse._22, inverse._23, inverse._24,
            inverse._31, inverse._32, inverse._33, inverse._34,
            inverse._41, inverse._42, inverse._43, inverse._44
        };
        if (!std::ranges::all_of(inverseValues, [](const float value) { return std::isfinite(value); }))
        {
            LOG_WARNING("[Transform] Transform inverse contains invalid values; returning identity.");
            return Matrix::Identity;
        }

        return inverse;
    }

    Transform Transform::combine(const Transform& parent) const
    {
        Transform result;
        result.m_scale = m_scale * parent.m_scale;
        result.m_rotation = Quaternion::Concatenate(m_rotation, parent.m_rotation);
        result.m_position = parent.transformPoint(m_position);

        return result;
    }

    Vector3 Transform::transformPoint(const Vector3& point) const
    {
        return Vector3::Transform(point * m_scale, m_rotation) + m_position;
    }

    Vector3 Transform::transformDirection(const Vector3& direction) const
    {
        return Vector3::Transform(direction, m_rotation);
    }

    void Transform::setEulerAngles(float pitch, float yaw, float roll)
    {
        m_rotation = Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll);
        markDirty();
    }

    Vector3 Transform::getEulerAngles() const
    {
        return m_rotation.ToEuler();
    }

    void Transform::lookAt(const Vector3& target, const Vector3& up)
    {
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)
            || !std::isfinite(m_position.x) || !std::isfinite(m_position.y) || !std::isfinite(m_position.z)
            || !std::isfinite(up.x) || !std::isfinite(up.y) || !std::isfinite(up.z))
        {
            LOG_WARNING("[Transform] Ignoring LookAt request with invalid or degenerate direction/up vector.");
            return;
        }

        const auto normalize = [](const double x, const double y, const double z, Vector3& normalized)
            {
                const double length = std::hypot(x, y, z);
                if (!std::isfinite(length) || length <= 0.0)
                    return false;

                normalized = Vector3(
                    static_cast<float>(x / length),
                    static_cast<float>(y / length),
                    static_cast<float>(z / length));
                return true;
            };

        Vector3 normalizedDirection;
        Vector3 normalizedUp;
        if (!normalize(
            static_cast<double>(target.x) - m_position.x,
            static_cast<double>(target.y) - m_position.y,
            static_cast<double>(target.z) - m_position.z,
            normalizedDirection)
            || !normalize(up.x, up.y, up.z, normalizedUp))
        {
            LOG_WARNING("[Transform] Ignoring LookAt request with zero-length direction or up vector.");
            return;
        }

        const Vector3 directionCrossUp = normalizedDirection.Cross(normalizedUp);
        const float crossLengthSquared = directionCrossUp.LengthSquared();
        if (!std::isfinite(crossLengthSquared) || crossLengthSquared <= EPSILON * EPSILON)
        {
            LOG_WARNING("[Transform] Ignoring LookAt request with parallel direction and up vectors.");
            return;
        }

        m_rotation = Quaternion::LookRotation(normalizedDirection, normalizedUp);
        markDirty();
    }
} // namespace Engine