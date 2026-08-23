#include "Pch.h"

#include "Core\Math\Transform.h"

namespace Engine
{
    Transform::Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
        : m_position(position)
        , m_rotation(rotation)
        , m_scale(scale)
    {
    }

    Transform Transform::fromMatrix(const Matrix& matrix)
    {
        Transform result;

        // Decomposeが非constのため作業用の複製を用意する
        Matrix source = matrix;

        if (!source.Decompose(result.m_scale, result.m_rotation, result.m_position))
        {
            result.m_position = source.Translation();
            result.m_rotation = Quaternion::Identity;
            result.m_scale = Vector3::One;
        }

        return result;
    }

    Transform Transform::lerp(const Transform& from, const Transform& to, float alpha)
    {
        const float t = saturate(alpha);

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
        return toMatrix().Invert();
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
    }

    Vector3 Transform::getEulerAngles() const
    {
        return m_rotation.ToEuler();
    }

    void Transform::lookAt(const Vector3& target, const Vector3& up)
    {
        const Vector3 direction = target - m_position;
        if (direction.LengthSquared() <= EPSILON)
            return;

        m_rotation = Quaternion::LookRotation(direction, up);
    }
} // namespace Engine