#pragma once

#include "Assets\Model\Resource\ModelResource.h"

struct aiAnimation;
struct aiMaterial;
struct aiMesh;
struct aiNode;
struct aiScene;

namespace Engine
{
    /**
     * @brief Assimpを使用して外部モデルをCPU側ModelResourceへ変換するImporter。
     * AssimpのシーンポインタやDirectX 12リソースを保持しない。
     */
    class AssimpModelImporter
    {
    public:

        /**
         * @brief モデルファイルをインポートする。
         * @param path 読み込むモデルファイルのパス
         * @return 成功時はModelResource、失敗時はnullptr
         */
        std::shared_ptr<ModelResource> importModel(const std::filesystem::path& path) const;

    private:

        /**
         * @brief AssimpのシーンをModelResourceへ変換する。
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         */
        void processScene(const aiScene* scene, ModelResource& model) const;

        /**
         * @brief AssimpのノードをModelResourceへ変換する。
         * @param node Assimpのノード
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         * @param parentTransform 親ノードの変換行列
         */
        void processNode(const aiNode* node, const aiScene* scene, ModelResource& model, const Matrix& parentTransform) const;

        /**
         * @brief AssimpのノードをModelResourceへ変換する（内部処理）。
         * @param node Assimpのノード
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         * @param parentIndex 親ノードのインデックス
         * @param parentTransform 親ノードの変換行列
         */
        void processNodeInternal(const aiNode* node, const aiScene* scene, ModelResource& model, std::int32_t parentIndex, const Matrix& parentTransform) const;

        /**
         * @brief AssimpのメッシュをModelResourceへ変換する。
         * @param mesh Assimpのメッシュ
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         * @return 変換後のメッシュインデックス
         */
        std::uint32_t processMesh(const aiMesh* mesh, const aiScene* scene, ModelResource& model) const;

        /**
         * @brief AssimpのマテリアルをModelResourceへ変換する。
         * @param material Assimpのマテリアル
         * @param model 変換先のModelResource
         * @return 変換後のマテリアルインデックス
         */
        std::uint32_t processMaterial(const aiMaterial* material, ModelResource& model) const;

        /**
         * @brief AssimpのスケルトンをModelResourceへ変換する。
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         */
        void processSkeleton(const aiScene* scene, ModelResource& model) const;

        /**
         * @brief AssimpのアニメーションをModelResourceへ変換する。
         * @param scene Assimpのシーン
         * @param model 変換先のModelResource
         */
        void processAnimations(const aiScene* scene, ModelResource& model) const;
    };
} // namespace Engine
