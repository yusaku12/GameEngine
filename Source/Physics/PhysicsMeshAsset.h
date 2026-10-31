#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "Core\Math\MathTypes.h"

namespace Engine
{
    struct MeshResource;

    enum class PhysicsMeshType : std::uint8_t
    {
        Triangle = 1,
        Convex = 2
    };

    /**
     * @brief .physicsアセットとして保存できる、不変のPhysXメッシュ調理済みデータ。
     */
    class PhysicsMeshAsset final
    {
    public:

        /** @brief 頂点インデックスから静的三角形メッシュを調理する。 */
        static std::shared_ptr<const PhysicsMeshAsset> cookTriangle(
            std::span<const Vector3> vertices,
            std::span<const std::uint32_t> indices);
        /** @brief モデルメッシュのインデックス付きジオメトリ全体を調理する。 */
        static std::shared_ptr<const PhysicsMeshAsset> cookTriangle(const MeshResource& mesh);
        /** @brief 点群から凸メッシュを調理する。 */
        static std::shared_ptr<const PhysicsMeshAsset> cookConvex(
            std::span<const Vector3> vertices);
        /** @brief モデルメッシュの頂点座標から凸包を調理する。 */
        static std::shared_ptr<const PhysicsMeshAsset> cookConvex(const MeshResource& mesh);

        /** @brief 物理アセットファイルを読み込み、調理済みデータを検証する。 */
        static std::shared_ptr<const PhysicsMeshAsset> load(const std::filesystem::path& path);

        /** @brief 調理済みデータとその種類を.physicsアセットファイルに保存する。 */
        bool save(const std::filesystem::path& path) const;

        /** @brief 調理済みメッシュの種類を取得する。 */
        PhysicsMeshType getType() const noexcept { return m_type; }
        /** @brief PhysXの調理済みシリアライズデータを取得する。 */
        std::span<const std::uint8_t> getCookedData() const noexcept { return m_cookedData; }
        /** @brief このアセットの読み込み元、または最後に保存したパスを取得する。 */
        const std::filesystem::path& getPath() const noexcept { return m_path; }

    private:

        PhysicsMeshAsset(PhysicsMeshType type, std::vector<std::uint8_t> cookedData)
            : m_type(type)
            , m_cookedData(std::move(cookedData))
        {
        }

        PhysicsMeshType m_type;
        std::vector<std::uint8_t> m_cookedData;
        mutable std::filesystem::path m_path;
    };
} // namespace Engine
