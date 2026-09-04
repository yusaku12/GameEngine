#include "Pch.h"

#include "Core\GameObject\Transform.h"

namespace Engine
{
    Transform::Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
        : m_localPosition(position), m_localRotation(rotation), m_localScale(scale), m_dirty(true)
    {
    }

    void Transform::SetParent(Transform* parent) noexcept
    {
        m_parent = parent;
        MarkDirty();
    }

    Vector3 Transform::GetWorldPosition() const noexcept
    {
        return m_worldPosition;
    }

    Quaternion Transform::GetWorldRotation() const noexcept
    {
        return m_worldRotation;
    }

    Vector3 Transform::GetWorldScale() const noexcept
    {
        return m_worldScale;
    }

    Matrix Transform::GetLocalMatrix() const noexcept
    {
        return m_localMatrix;
    }

    Matrix Transform::GetWorldMatrix() const noexcept
    {
        return m_worldMatrix;
    }

    void Transform::UpdateWorldTransform() noexcept
    {
        if (!m_dirty)
            return;

        if (m_parent != nullptr)
        {
            const auto parentMatrix = m_parent->GetWorldMatrix();
            const auto localMatrix = Matrix::CreateScale(m_localScale) * Matrix::CreateFromQuaternion(m_localRotation) * Matrix::CreateTranslation(m_localPosition);
            m_worldMatrix = localMatrix * parentMatrix;
            m_worldPosition = Vector3::Transform(m_localPosition, parentMatrix);
            m_worldRotation = m_localRotation * m_parent->GetWorldRotation();
            m_worldScale = m_localScale * m_parent->GetWorldScale();
        }
        else
        {
            m_worldMatrix = Matrix::CreateScale(m_localScale) * Matrix::CreateFromQuaternion(m_localRotation) * Matrix::CreateTranslation(m_localPosition);
            m_worldPosition = m_localPosition;
            m_worldRotation = m_localRotation;
            m_worldScale = m_localScale;
        }

        m_dirty = false;
    }
} // namespace Engine
