#pragma once

#include "Pch.h"

namespace Engine
{
    /**
     * @brief GameObjectのローカル変換を保持する簡易Transform。
     */
    class Transform
    {
    public:
        Transform() = default;
        Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);

        [[nodiscard]] const Vector3& GetLocalPosition() const noexcept { return m_localPosition; }
        [[nodiscard]] const Quaternion& GetLocalRotation() const noexcept { return m_localRotation; }
        [[nodiscard]] const Vector3& GetLocalScale() const noexcept { return m_localScale; }

        void SetLocalPosition(const Vector3& position) noexcept { m_localPosition = position; m_dirty = true; }
        void SetLocalRotation(const Quaternion& rotation) noexcept { m_localRotation = rotation; m_dirty = true; }
        void SetLocalScale(const Vector3& scale) noexcept { m_localScale = scale; m_dirty = true; }

        [[nodiscard]] Vector3 GetWorldPosition() const noexcept;
        [[nodiscard]] Quaternion GetWorldRotation() const noexcept;
        [[nodiscard]] Vector3 GetWorldScale() const noexcept;
        [[nodiscard]] Matrix GetLocalMatrix() const noexcept;
        [[nodiscard]] Matrix GetWorldMatrix() const noexcept;

        void SetParent(Transform* parent) noexcept;
        [[nodiscard]] Transform* GetParent() const noexcept { return m_parent; }

        void MarkDirty() noexcept { m_dirty = true; }
        [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }

        void UpdateWorldTransform() noexcept;

    private:
        Vector3 m_localPosition = Vector3::Zero;
        Quaternion m_localRotation = Quaternion::Identity;
        Vector3 m_localScale = Vector3::One;

        Vector3 m_worldPosition = Vector3::Zero;
        Quaternion m_worldRotation = Quaternion::Identity;
        Vector3 m_worldScale = Vector3::One;
        Matrix m_localMatrix = Matrix::Identity;
        Matrix m_worldMatrix = Matrix::Identity;

        Transform* m_parent = nullptr;
        bool m_dirty = true;
    };
} // namespace Engine
