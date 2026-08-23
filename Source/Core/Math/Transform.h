#pragma once

#include "Core\Math\MathFunction.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /**
     * @brief 位置・回転・拡大縮小を保持する変換
     */
    class Transform
    {
    public:

        Transform() = default;

        /**
         * @brief 各要素を指定して構築する
         * @param position 位置
         * @param rotation 回転
         * @param scale 拡大縮小
         */
        Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);

        /**
         * @brief 行列から変換を取り出す
         * @param matrix 変換行列
         * @return Transform 取り出した変換
         */
        static Transform fromMatrix(const Matrix& matrix);

        /**
         * @brief 2つの変換を補間する
         * @param from 開始する変換
         * @param to 終了する変換
         * @param alpha 補間係数
         * @return Transform 補間結果
         */
        static Transform lerp(const Transform& from, const Transform& to, float alpha);

        /**
         * @brief 変換行列を求める
         * @return Matrix 変換行列
         */
        Matrix toMatrix() const;

        /**
         * @brief 変換行列の逆行列を求める
         * @return Matrix 逆行列
         */
        Matrix toInverseMatrix() const;

        /**
         * @brief 別の変換を後段に適用した合成変換を求める
         * @param parent 親となる変換
         * @return Transform 合成した変換
         */
        Transform combine(const Transform& parent) const;

        /**
         * @brief 点をこの変換で移す
         * @param point 対象の点
         * @return Vector3 変換後の点
         */
        Vector3 transformPoint(const Vector3& point) const;

        /**
         * @brief 方向ベクトルをこの変換で移す（平行移動を無視する）
         * @param direction 対象の方向
         * @return Vector3 変換後の方向
         */
        Vector3 transformDirection(const Vector3& direction) const;

        /**
         * @brief 位置を移動する
         * @param delta 移動量
         */
        void translate(const Vector3& delta) { m_position += delta; }

        /**
         * @brief 回転を加える
         * @param delta 加える回転
         */
        void rotate(const Quaternion& delta) { m_rotation = Quaternion::Concatenate(m_rotation, delta); }

        /**
         * @brief オイラー角で回転を設定する
         * @param pitch X軸まわりの回転（ラジアン）
         * @param yaw Y軸まわりの回転（ラジアン）
         * @param roll Z軸まわりの回転（ラジアン）
         */
        void setEulerAngles(float pitch, float yaw, float roll);

        /**
         * @brief 回転をオイラー角で取得する
         * @return Vector3 X/Y/Z軸まわりの回転（ラジアン）
         */
        Vector3 getEulerAngles() const;

        /**
         * @brief 指定した位置を向くよう回転を設定する
         * @param target 注視する位置
         * @param up 上方向
         */
        void lookAt(const Vector3& target, const Vector3& up = Vector3::Up);

        /**
         * @brief 前方向を取得する
         * @return Vector3 前方向
         */
        Vector3 forward() const { return Vector3::Transform(Vector3::Forward, m_rotation); }

        /**
         * @brief 右方向を取得する
         * @return Vector3 右方向
         */
        Vector3 right() const { return Vector3::Transform(Vector3::Right, m_rotation); }

        /**
         * @brief 上方向を取得する
         * @return Vector3 上方向
         */
        Vector3 up() const { return Vector3::Transform(Vector3::Up, m_rotation); }

        /**
         * @brief 位置を取得する
         * @return Vector3 位置
         */
        const Vector3& getPosition() const { return m_position; }

        /**
         * @brief 回転を取得する
         * @return Quaternion 回転
         */
        const Quaternion& getRotation() const { return m_rotation; }

        /**
         * @brief 拡大縮小を取得する
         * @return Vector3 拡大縮小
         */
        const Vector3& getScale() const { return m_scale; }

        /**
         * @brief 位置を設定する
         * @param position 位置
         */
        void setPosition(const Vector3& position) { m_position = position; }

        /**
         * @brief 回転を設定する
         * @param rotation 回転
         */
        void setRotation(const Quaternion& rotation) { m_rotation = rotation; }

        /**
         * @brief 拡大縮小を設定する
         * @param scale 拡大縮小
         */
        void setScale(const Vector3& scale) { m_scale = scale; }

        /**
         * @brief 拡大縮小を設定する（均一スケール）
         * @param scale 均一スケール
         */
        void setScale(float scale) { m_scale = Vector3(scale, scale, scale); }

    private:

        Vector3    m_position = Vector3::Zero;         //!< 位置
        Quaternion m_rotation = Quaternion::Identity;  //!< 回転
        Vector3    m_scale = Vector3::One;             //!< 拡大縮小
    };
} // namespace Engine
