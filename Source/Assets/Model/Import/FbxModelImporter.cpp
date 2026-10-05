#include "Pch.h"
#include "Assets\Model\Import\FbxModelImporter.h"
#include <fbxsdk.h>

namespace Engine
{
    namespace
    {
        struct FbxDestroy
        {
            template <typename T>
            void operator()(T* object) const noexcept
            {
                if (object != nullptr)
                    object->Destroy();
            }
        };

        struct ImportedScene
        {
            std::unique_ptr<FbxManager, FbxDestroy> manager;
            FbxScene* scene = nullptr;
        };

        std::string toUtf8(const std::filesystem::path& path)
        {
            const std::wstring widePath = path.wstring();
            if (widePath.empty())
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, widePath.data(),
                static_cast<int>(widePath.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
                return {};
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, widePath.data(),
                static_cast<int>(widePath.size()), result.data(), size, nullptr, nullptr) != size)
                return {};
            return result;
        }

        std::unique_ptr<ImportedScene> loadScene(const std::filesystem::path& path)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                LOG_ERROR_CAT("FbxModelImporter", "File not found: {}", path.string());
                return nullptr;
            }

            const std::string utf8Path = toUtf8(path);
            if (utf8Path.empty())
            {
                LOG_ERROR_CAT("FbxModelImporter", "Could not encode file path as UTF-8: {}", path.string());
                return nullptr;
            }

            auto result = std::make_unique<ImportedScene>();
            result->manager.reset(FbxManager::Create());
            if (result->manager == nullptr)
            {
                LOG_ERROR_CAT("FbxModelImporter", "Could not create FBX manager");
                return nullptr;
            }

            FbxIOSettings* settings = FbxIOSettings::Create(result->manager.get(), IOSROOT);
            if (settings == nullptr)
                return nullptr;
            result->manager->SetIOSettings(settings);

            std::unique_ptr<FbxImporter, FbxDestroy> importer(FbxImporter::Create(result->manager.get(), ""));
            if (importer == nullptr || !importer->Initialize(utf8Path.c_str(), -1, result->manager->GetIOSettings()))
            {
                LOG_ERROR_CAT("FbxModelImporter", "FBX import initialization failed: {}",
                    importer == nullptr ? "FBX importer allocation failed" : importer->GetStatus().GetErrorString());
                return nullptr;
            }

            result->scene = FbxScene::Create(result->manager.get(), "Scene");
            if (result->scene == nullptr || !importer->Import(result->scene))
            {
                LOG_ERROR_CAT("FbxModelImporter", "FBX scene import failed: {}", importer->GetStatus().GetErrorString());
                return nullptr;
            }

            FbxGeometryConverter converter(result->manager.get());
            if (!converter.Triangulate(result->scene, true))
            {
                LOG_ERROR_CAT("FbxModelImporter", "Could not triangulate scene: {}", path.string());
                return nullptr;
            }
            converter.RemoveBadPolygonsFromMeshes(result->scene);
            const FbxAxisSystem rightHandedYUp(FbxAxisSystem::eYAxis,
                FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);
            rightHandedYUp.ConvertScene(result->scene);
            const FbxSystemUnit::ConversionOptions unitOptions{ true, true, true, true, true, true };
            FbxSystemUnit(100.0).ConvertScene(result->scene, unitOptions);
            return result;
        }

        Matrix toMatrix(const FbxAMatrix& value)
        {
            return Matrix(
                static_cast<float>(value[0][0]), static_cast<float>(value[0][1]), static_cast<float>(value[0][2]), static_cast<float>(value[0][3]),
                static_cast<float>(value[1][0]), static_cast<float>(value[1][1]), static_cast<float>(value[1][2]), static_cast<float>(value[1][3]),
                static_cast<float>(value[2][0]), static_cast<float>(value[2][1]), static_cast<float>(value[2][2]), static_cast<float>(value[2][3]),
                static_cast<float>(value[3][0]), static_cast<float>(value[3][1]), static_cast<float>(value[3][2]), static_cast<float>(value[3][3]));
        }

        FbxAMatrix reflectX(const FbxAMatrix& value)
        {
            FbxAMatrix reflection;
            reflection.SetIdentity();
            reflection.SetS(FbxVector4(-1.0, 1.0, 1.0, 1.0));
            return reflection * value * reflection;
        }

        Vector3 toVector3(const FbxVector4& value)
        {
            return Vector3(static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
        }

        Vector3 toVector3(const FbxDouble3& value)
        {
            return Vector3(static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
        }

        Vector4 toVector4(const FbxQuaternion& value)
        {
            return Vector4(static_cast<float>(value[0]), static_cast<float>(value[1]),
                static_cast<float>(value[2]), static_cast<float>(value[3]));
        }

        FbxAMatrix geometricTransform(FbxNode* node)
        {
            return FbxAMatrix(node->GetGeometricTranslation(FbxNode::eSourcePivot),
                node->GetGeometricRotation(FbxNode::eSourcePivot), node->GetGeometricScaling(FbxNode::eSourcePivot));
        }

        std::string resolveTexturePath(FbxFileTexture* texture, const std::filesystem::path& modelPath)
        {
            if (texture == nullptr)
                return {};
            const char* filename = texture->GetRelativeFileName();
            if (filename == nullptr || filename[0] == '\0')
                filename = texture->GetFileName();
            if (filename == nullptr || filename[0] == '\0')
                return {};
            std::filesystem::path resolved(std::u8string(reinterpret_cast<const char8_t*>(filename)));
            if (resolved.is_relative())
                resolved = modelPath.parent_path() / resolved;
            return resolved.lexically_normal().string();
        }

        std::string texturePath(FbxSurfaceMaterial* material, const char* propertyName,
            const std::filesystem::path& modelPath)
        {
            const FbxProperty property = material->FindProperty(propertyName);
            if (!property.IsValid())
                return {};
            const int sourceCount = property.GetSrcObjectCount();
            if (sourceCount == 0)
                return {};
            FbxObject* source = property.GetSrcObject(0);
            if (source == nullptr || std::strcmp(source->GetClassId().GetName(), "FbxFileTexture") != 0)
                return {};
            auto* texture = static_cast<FbxFileTexture*>(source);
            return resolveTexturePath(texture, modelPath);
        }

        using MaterialIndexMap = std::unordered_map<const FbxSurfaceMaterial*, std::uint32_t>;

        void loadMaterials(FbxScene* scene, const std::filesystem::path& modelPath,
            ModelResource& model, MaterialIndexMap& materialIndices)
        {
            for (int index = 0; index < scene->GetMaterialCount(); ++index)
            {
                FbxSurfaceMaterial* source = scene->GetMaterial(index);
                materialIndices.emplace(source, static_cast<std::uint32_t>(model.materials.size()));
                MaterialResource material;
                material.name = source->GetName();
                FbxProperty baseColor = source->FindProperty("BaseColor");
                FbxProperty colorFactor = source->FindProperty("BaseColorFactor");
                if (!baseColor.IsValid())
                {
                    baseColor = source->FindProperty("DiffuseColor");
                    colorFactor = source->FindProperty("DiffuseFactor");
                }
                if (baseColor.IsValid())
                {
                    const FbxDouble3 color = baseColor.Get<FbxDouble3>();
                    const float factor = colorFactor.IsValid() ? static_cast<float>(colorFactor.Get<FbxDouble>()) : 1.0f;
                    material.baseColor = Vector4(static_cast<float>(color[0]) * factor,
                        static_cast<float>(color[1]) * factor, static_cast<float>(color[2]) * factor, 1.0f);
                }
                FbxProperty metallic = source->FindProperty("Metalness");
                if (!metallic.IsValid())
                    metallic = source->FindProperty("Metallic");
                if (metallic.IsValid())
                    material.metallic = std::clamp(static_cast<float>(metallic.Get<FbxDouble>()), 0.0f, 1.0f);
                const FbxProperty roughness = source->FindProperty("Roughness");
                if (roughness.IsValid())
                    material.roughness = std::clamp(static_cast<float>(roughness.Get<FbxDouble>()), 0.0f, 1.0f);
                const FbxProperty transparency = source->FindProperty("TransparencyFactor");
                if (transparency.IsValid())
                    material.opacity = std::clamp(1.0f - static_cast<float>(transparency.Get<FbxDouble>()), 0.0f, 1.0f);
                const FbxProperty emissive = source->FindProperty("EmissiveColor");
                const FbxProperty emissiveFactor = source->FindProperty("EmissiveFactor");
                if (emissive.IsValid())
                    material.emissive = toVector3(emissive.Get<FbxDouble3>())
                    * (emissiveFactor.IsValid() ? static_cast<float>(emissiveFactor.Get<FbxDouble>()) : 1.0f);
                material.textures.baseColor = texturePath(source, "BaseColor", modelPath);
                if (material.textures.baseColor.empty())
                    material.textures.baseColor = texturePath(source, "DiffuseColor", modelPath);
                material.textures.normal = texturePath(source, "NormalMap", modelPath);
                if (material.textures.normal.empty())
                    material.textures.normal = texturePath(source, "Bump", modelPath);
                material.textures.metallic = texturePath(source, "Metalness", modelPath);
                if (material.textures.metallic.empty())
                    material.textures.metallic = texturePath(source, "Metallic", modelPath);
                material.textures.roughness = texturePath(source, "Roughness", modelPath);
                material.textures.ambientOcclusion = texturePath(source, "AmbientOcclusion", modelPath);
                material.textures.emissive = texturePath(source, "EmissiveColor", modelPath);
                material.textures.opacity = texturePath(source, "TransparentColor", modelPath);
                model.materials.push_back(std::move(material));
            }
            if (model.materials.empty())
                model.materials.emplace_back();
        }

        struct BoneInfluence
        {
            std::array<std::uint32_t, MAX_BONE_INFLUENCES> indices{};
            std::array<float, MAX_BONE_INFLUENCES> weights{};

            void add(const std::uint32_t index, const float weight)
            {
                if (!std::isfinite(weight) || weight <= 0.0f || weight <= weights.back())
                    return;
                std::size_t insert = weights.size() - 1;
                while (insert > 0 && weight > weights[insert - 1])
                {
                    indices[insert] = indices[insert - 1];
                    weights[insert] = weights[insert - 1];
                    --insert;
                }
                indices[insert] = index;
                weights[insert] = weight;
            }

            void apply(ModelVertex& vertex) const
            {
                float sum = 0.0f;
                for (const float weight : weights)
                    sum += weight;
                if (sum <= EPSILON)
                    return;
                vertex.boneWeights = Vector4(weights[0] / sum, weights[1] / sum, weights[2] / sum, weights[3] / sum);
                for (std::size_t index = 0; index < weights.size(); ++index)
                    vertex.boneIndices[index] = static_cast<std::uint16_t>(indices[index]);
            }
        };

        using BoneIndexMap = std::unordered_map<const FbxNode*, std::uint32_t>;

        void collectGlobalTransforms(FbxNode* node, std::unordered_map<const FbxNode*, FbxAMatrix>& transforms)
        {
            if (node == nullptr)
                return;
            transforms.emplace(node, node->EvaluateGlobalTransform());
            for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
                collectGlobalTransforms(node->GetChild(childIndex), transforms);
        }

        bool containsWeightedNode(FbxNode* node, const std::unordered_set<const FbxNode*>& weightedNodes)
        {
            if (weightedNodes.contains(node))
                return true;
            for (int index = 0; index < node->GetChildCount(); ++index)
                if (containsWeightedNode(node->GetChild(index), weightedNodes))
                    return true;
            return false;
        }

        bool appendSkeletonNodes(FbxNode* node, const std::int32_t parentIndex,
            const std::unordered_set<const FbxNode*>& weightedNodes,
            const std::unordered_map<const FbxNode*, FbxAMatrix>& bindTransforms,
            BoneIndexMap& boneIndices, SkeletonResource& skeleton)
        {
            if (!containsWeightedNode(node, weightedNodes))
                return true;
            if (skeleton.bones.size() >= MAX_SKINNING_BONES)
                return false;
            const std::string name = node->GetName();
            if (name.empty() || skeleton.boneMap.contains(name))
                return false;

            const std::uint32_t index = static_cast<std::uint32_t>(skeleton.bones.size());
            FbxAMatrix globalBindTransform = reflectX(node->EvaluateGlobalTransform());
            if (const auto bind = bindTransforms.find(node); bind != bindTransforms.end())
                globalBindTransform = reflectX(bind->second);
            FbxAMatrix localTransform = globalBindTransform;
            if (node->GetParent() != nullptr)
            {
                FbxAMatrix parentGlobalTransform = reflectX(node->GetParent()->EvaluateGlobalTransform());
                if (const auto parentBind = bindTransforms.find(node->GetParent()); parentBind != bindTransforms.end())
                    parentGlobalTransform = reflectX(parentBind->second);
                localTransform = parentGlobalTransform.Inverse() * globalBindTransform;
            }
            Matrix inverseBind = Matrix::Identity;
            inverseBind = toMatrix(globalBindTransform.Inverse());
            skeleton.boneMap.emplace(name, index);
            boneIndices.emplace(node, index);
            skeleton.bones.push_back({ index, name, parentIndex, inverseBind, toMatrix(localTransform) });

            for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
                if (!appendSkeletonNodes(node->GetChild(childIndex), static_cast<std::int32_t>(index),
                    weightedNodes, bindTransforms, boneIndices, skeleton))
                    return false;
            return true;
        }

        bool loadSkeleton(FbxScene* scene, ModelResource& model, BoneIndexMap& boneIndices)
        {
            std::unordered_set<const FbxNode*> weightedNodes;
            std::unordered_map<const FbxNode*, FbxAMatrix> bindTransforms;
            collectGlobalTransforms(scene->GetRootNode(), bindTransforms);
            for (int nodeIndex = 0; nodeIndex < scene->GetNodeCount(); ++nodeIndex)
            {
                FbxNode* node = scene->GetNode(nodeIndex);
                FbxMesh* mesh = node == nullptr ? nullptr : node->GetMesh();
                if (mesh == nullptr)
                    continue;
                for (int skinIndex = 0; skinIndex < mesh->GetDeformerCount(FbxDeformer::eSkin); ++skinIndex)
                {
                    auto* skin = static_cast<FbxSkin*>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
                    for (int clusterIndex = 0; clusterIndex < skin->GetClusterCount(); ++clusterIndex)
                    {
                        FbxCluster* cluster = skin->GetCluster(clusterIndex);
                        FbxNode* link = cluster->GetLink();
                        if (link == nullptr)
                            continue;
                        FbxAMatrix bindTransform;
                        cluster->GetTransformLinkMatrix(bindTransform);
                        weightedNodes.insert(link);
                        bindTransforms.insert_or_assign(link, bindTransform);
                    }
                }
            }
            if (weightedNodes.empty())
                return true;

            SkeletonResource skeleton;
            if (!appendSkeletonNodes(scene->GetRootNode(), -1, weightedNodes, bindTransforms, boneIndices, skeleton))
            {
                LOG_ERROR_CAT("FbxModelImporter", "Invalid skeleton hierarchy, duplicate joint name, or joint count exceeds {}",
                    MAX_SKINNING_BONES);
                return false;
            }
            if (skeleton.bones.empty())
                return false;
            model.skeleton = std::move(skeleton);
            return true;
        }

        std::uint32_t getMaterialIndex(FbxMesh* mesh, const int polygonIndex,
            const std::vector<std::uint32_t>& nodeMaterialIndices)
        {
            FbxGeometryElementMaterial* materialElement = mesh->GetElementMaterial(0);
            if (materialElement == nullptr)
                return 0;

            int elementIndex = 0;
            switch (materialElement->GetMappingMode())
            {
            case FbxGeometryElement::eByPolygon:
                elementIndex = polygonIndex;
                break;
            case FbxGeometryElement::eByPolygonVertex:
                elementIndex = mesh->GetPolygonVertexIndex(polygonIndex);
                break;
            case FbxGeometryElement::eByControlPoint:
                elementIndex = mesh->GetPolygonVertex(polygonIndex, 0);
                break;
            case FbxGeometryElement::eAllSame:
                elementIndex = 0;
                break;
            default:
                return 0;
            }

            if (elementIndex < 0 || elementIndex >= materialElement->GetIndexArray().GetCount())
                return 0;
            const int nodeMaterialIndex = materialElement->GetIndexArray().GetAt(elementIndex);
            if (nodeMaterialIndex < 0 || static_cast<std::size_t>(nodeMaterialIndex) >= nodeMaterialIndices.size())
                return 0;
            return nodeMaterialIndices[static_cast<std::size_t>(nodeMaterialIndex)];
        }

        bool loadMesh(FbxNode* node, FbxMesh* source, ModelResource& model,
            const BoneIndexMap& boneIndices, const MaterialIndexMap& sceneMaterialIndices, MeshResource& mesh)
        {
            mesh.name = source->GetName();
            const FbxAMatrix geometry = geometricTransform(node);
            const FbxAMatrix normalTransform = geometry.Inverse().Transpose();
            const int polygonCount = source->GetPolygonCount();
            const int controlPointCount = source->GetControlPointsCount();
            if (polygonCount <= 0 || controlPointCount <= 0)
                return false;

            FbxStringList uvSetNames;
            source->GetUVSetNames(uvSetNames);
            if (uvSetNames.GetCount() > 0)
                source->GenerateTangentsData(0, true);

            std::vector<BoneInfluence> influences(static_cast<std::size_t>(controlPointCount));
            for (int skinIndex = 0; skinIndex < source->GetDeformerCount(FbxDeformer::eSkin); ++skinIndex)
            {
                auto* skin = static_cast<FbxSkin*>(source->GetDeformer(skinIndex, FbxDeformer::eSkin));
                for (int clusterIndex = 0; clusterIndex < skin->GetClusterCount(); ++clusterIndex)
                {
                    FbxCluster* cluster = skin->GetCluster(clusterIndex);
                    const auto bone = boneIndices.find(cluster->GetLink());
                    if (bone == boneIndices.end())
                        continue;
                    const int count = cluster->GetControlPointIndicesCount();
                    const int* indices = cluster->GetControlPointIndices();
                    const double* weights = cluster->GetControlPointWeights();
                    for (int index = 0; index < count; ++index)
                        if (indices[index] >= 0 && indices[index] < controlPointCount)
                            influences[static_cast<std::size_t>(indices[index])].add(bone->second,
                                static_cast<float>(weights[index]));
                }
            }
            mesh.verticesInModelSpace = std::any_of(influences.begin(), influences.end(),
                [](const BoneInfluence& influence) { return influence.weights[0] > EPSILON; });
            Matrix meshToModel = Matrix::Identity;
            Matrix normalToModel = Matrix::Identity;
            if (mesh.verticesInModelSpace)
            {
                meshToModel = toMatrix(reflectX(node->EvaluateGlobalTransform()));
                normalToModel = meshToModel.Invert().Transpose();
            }

            const std::size_t materialCount = model.materials.size();
            std::vector<std::uint32_t> nodeMaterialIndices;
            nodeMaterialIndices.reserve(static_cast<std::size_t>(node->GetMaterialCount()));
            for (int index = 0; index < node->GetMaterialCount(); ++index)
            {
                const FbxSurfaceMaterial* material = node->GetMaterial(index);
                const auto found = sceneMaterialIndices.find(material);
                nodeMaterialIndices.push_back(found == sceneMaterialIndices.end() ? 0 : found->second);
            }
            std::vector<std::vector<std::uint32_t>> materialIndices(materialCount);
            const FbxVector4* controlPoints = source->GetControlPoints();
            int polygonVertexIndex = 0;
            for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
            {
                if (source->GetPolygonSize(polygonIndex) != 3)
                    return false;
                const std::uint32_t materialIndex = getMaterialIndex(source, polygonIndex,
                    nodeMaterialIndices);
                auto& indices = materialIndices[materialIndex < materialCount ? materialIndex : 0];
                for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex, ++polygonVertexIndex)
                {
                    const int controlPointIndex = source->GetPolygonVertex(polygonIndex, vertexIndex);
                    if (controlPointIndex < 0 || controlPointIndex >= controlPointCount)
                        return false;
                    if (mesh.vertices.size() >= (std::numeric_limits<std::uint32_t>::max)()
                        || indices.size() >= (std::numeric_limits<std::uint32_t>::max)())
                    {
                        LOG_ERROR_CAT("FbxModelImporter", "Mesh exceeds supported vertex or index count: {}", mesh.name);
                        return false;
                    }

                    ModelVertex vertex;
                    vertex.position = toVector3(geometry.MultT(controlPoints[controlPointIndex]));
                    vertex.position.x = -vertex.position.x;
                    if (mesh.verticesInModelSpace)
                        vertex.position = Vector3::Transform(vertex.position, meshToModel);
                    FbxVector4 normal;
                    if (source->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal))
                    {
                        normal[3] = 0.0;
                        normal = normalTransform.MultT(normal);
                        normal.Normalize();
                        Vector3 convertedNormal = toVector3(normal);
                        convertedNormal.x = -convertedNormal.x;
                        if (mesh.verticesInModelSpace)
                        {
                            convertedNormal = Vector3::TransformNormal(convertedNormal, normalToModel);
                            convertedNormal.Normalize();
                        }
                        vertex.normal = convertedNormal;
                    }
                    if (uvSetNames.GetCount() > 0)
                    {
                        FbxVector2 uv;
                        bool unmapped = false;
                        if (source->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetNames[0], uv, unmapped) && !unmapped)
                            vertex.texCoord = Vector2(static_cast<float>(uv[0]), 1.0f - static_cast<float>(uv[1]));
                    }
                    FbxGeometryElementTangent* tangents = source->GetElementTangent(0);
                    if (tangents != nullptr && tangents->GetMappingMode() == FbxGeometryElement::eByPolygonVertex
                        && tangents->GetReferenceMode() == FbxGeometryElement::eDirect
                        && tangents->GetDirectArray().GetCount() > polygonVertexIndex)
                    {
                        FbxVector4 tangent = tangents->GetDirectArray().GetAt(polygonVertexIndex);
                        tangent[3] = 0.0;
                        tangent = normalTransform.MultT(tangent);
                        tangent.Normalize();
                        Vector3 convertedTangent = toVector3(tangent);
                        convertedTangent.x = -convertedTangent.x;
                        if (mesh.verticesInModelSpace)
                        {
                            convertedTangent = Vector3::TransformNormal(convertedTangent, normalToModel);
                            convertedTangent.Normalize();
                        }
                        vertex.tangent = convertedTangent;
                        vertex.bitangent = vertex.normal.Cross(vertex.tangent);
                    }
                    influences[static_cast<std::size_t>(controlPointIndex)].apply(vertex);
                    const std::uint32_t vertexOutput = static_cast<std::uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                    indices.push_back(vertexOutput);
                }
            }

            for (std::size_t materialIndex = 0; materialIndex < materialIndices.size(); ++materialIndex)
            {
                auto& materialIndicesForMesh = materialIndices[materialIndex];
                if (materialIndicesForMesh.empty())
                    continue;
                for (std::size_t triangleIndex = 0; triangleIndex < materialIndicesForMesh.size(); triangleIndex += 3)
                    std::swap(materialIndicesForMesh[triangleIndex + 1], materialIndicesForMesh[triangleIndex + 2]);
                const std::size_t maxIndexCount = (std::numeric_limits<std::uint32_t>::max)();
                if (mesh.indices.size() > maxIndexCount
                    || materialIndicesForMesh.size() > maxIndexCount - mesh.indices.size())
                {
                    LOG_ERROR_CAT("FbxModelImporter", "Mesh exceeds supported index count: {}", mesh.name);
                    return false;
                }

                const std::uint32_t start = static_cast<std::uint32_t>(mesh.indices.size());
                mesh.indices.insert(mesh.indices.end(), materialIndicesForMesh.begin(), materialIndicesForMesh.end());
                mesh.subMeshes.push_back({ start, static_cast<std::uint32_t>(materialIndicesForMesh.size()),
                    static_cast<std::uint32_t>(materialIndex) });
            }
            if (mesh.vertices.empty())
                return false;
            Vector3 minimum = mesh.vertices.front().position;
            Vector3 maximum = minimum;
            for (const ModelVertex& vertex : mesh.vertices)
            {
                minimum = Vector3::Min(minimum, vertex.position);
                maximum = Vector3::Max(maximum, vertex.position);
            }
            mesh.boundingBox = makeAABB(minimum, maximum);
            BoundingSphere::CreateFromBoundingBox(mesh.boundingSphere, mesh.boundingBox);
            return true;
        }

        bool loadNodes(FbxNode* source, const std::int32_t parentIndex, ModelResource& model,
            const BoneIndexMap& boneIndices, const MaterialIndexMap& sceneMaterialIndices)
        {
            if (source == nullptr)
                return true;
            if (model.nodes.size() >= static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
                || model.meshes.size() > (std::numeric_limits<std::uint32_t>::max)())
            {
                LOG_ERROR_CAT("FbxModelImporter", "Model exceeds supported node or mesh count.");
                return false;
            }

            const std::uint32_t nodeIndex = static_cast<std::uint32_t>(model.nodes.size());
            model.nodes.push_back({});
            model.nodes[nodeIndex].name = source->GetName();
            model.nodes[nodeIndex].parentIndex = parentIndex;
            model.nodes[nodeIndex].localTransform = toMatrix(reflectX(source->EvaluateLocalTransform()));
            if (parentIndex >= 0)
                model.nodes[static_cast<std::size_t>(parentIndex)].children.push_back(nodeIndex);

            if (FbxMesh* sourceMesh = source->GetMesh())
            {
                const std::uint32_t meshIndex = static_cast<std::uint32_t>(model.meshes.size());
                model.meshes.emplace_back();
                if (!loadMesh(source, sourceMesh, model, boneIndices, sceneMaterialIndices, model.meshes.back()))
                    return false;
                model.nodes[nodeIndex].meshIndices.push_back(meshIndex);
            }
            for (int childIndex = 0; childIndex < source->GetChildCount(); ++childIndex)
                if (!loadNodes(source->GetChild(childIndex), static_cast<std::int32_t>(nodeIndex), model,
                    boneIndices, sceneMaterialIndices))
                    return false;
            return true;
        }

        void calculateBounds(ModelResource& model)
        {
            std::vector<Matrix> worldTransforms(model.nodes.size(), Matrix::Identity);
            bool hasBounds = false;
            for (std::size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
            {
                const ModelNode& node = model.nodes[nodeIndex];
                worldTransforms[nodeIndex] = node.localTransform;
                if (node.parentIndex >= 0)
                    worldTransforms[nodeIndex] *= worldTransforms[static_cast<std::size_t>(node.parentIndex)];
                for (const std::uint32_t meshIndex : node.meshIndices)
                {
                    AABB transformed;
                    if (model.meshes[meshIndex].verticesInModelSpace)
                        transformed = model.meshes[meshIndex].boundingBox;
                    else
                        model.meshes[meshIndex].boundingBox.Transform(transformed, worldTransforms[nodeIndex]);
                    if (!hasBounds)
                    {
                        model.boundingBox = transformed;
                        hasBounds = true;
                    }
                    else
                    {
                        model.boundingBox = makeAABB(Vector3::Min(minimumPointOf(model.boundingBox), minimumPointOf(transformed)),
                            Vector3::Max(maximumPointOf(model.boundingBox), maximumPointOf(transformed)));
                    }
                }
            }
            if (hasBounds)
                BoundingSphere::CreateFromBoundingBox(model.boundingSphere, model.boundingBox);
        }

        void collectNodes(FbxNode* node, std::vector<FbxNode*>& nodes)
        {
            if (node == nullptr)
                return;
            nodes.push_back(node);
            for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
                collectNodes(node->GetChild(childIndex), nodes);
        }

        std::string nodePath(const FbxNode* node)
        {
            const FbxNode* parent = node->GetParent();
            return parent == nullptr ? node->GetName() : nodePath(parent) + "/" + node->GetName();
        }

        bool hasAnimationCurves(FbxNode* node, const std::vector<FbxAnimLayer*>& layers)
        {
            for (FbxAnimLayer* layer : layers)
            {
                if (node->LclTranslation.GetCurveNode(layer) != nullptr
                    || node->LclRotation.GetCurveNode(layer) != nullptr
                    || node->LclScaling.GetCurveNode(layer) != nullptr)
                    return true;
            }
            return layers.empty();
        }

        std::vector<AnimationResource> loadAnimations(FbxScene* scene)
        {
            std::vector<AnimationResource> animations;
            FbxArray<FbxString*> stackNames;
            scene->FillAnimStackNameArray(stackNames);
            std::unordered_map<std::string, std::uint32_t> nameCounts;
            std::vector<FbxNode*> nodes;
            collectNodes(scene->GetRootNode(), nodes);
            const FbxTime::EMode timeMode = scene->GetGlobalSettings().GetTimeMode();
            double frameRate = FbxTime::GetFrameRate(timeMode);
            if (!std::isfinite(frameRate) || frameRate <= 0.0)
                frameRate = 30.0;
            frameRate = std::clamp(std::max(frameRate, 60.0), 1.0, 120.0);

            for (int stackIndex = 0; stackIndex < stackNames.Size(); ++stackIndex)
            {
                FbxString* stackName = stackNames.GetAt(stackIndex);
                FbxAnimStack* stack = nullptr;
                for (int objectIndex = 0; objectIndex < scene->GetSrcObjectCount(); ++objectIndex)
                {
                    FbxObject* source = scene->GetSrcObject(objectIndex);
                    if (source == nullptr || std::strcmp(source->GetClassId().GetName(), "FbxAnimStack") != 0)
                        continue;
                    auto* candidate = static_cast<FbxAnimStack*>(source);
                    if (candidate != nullptr && std::strcmp(candidate->GetName(), stackName->Buffer()) == 0)
                    {
                        stack = candidate;
                        break;
                    }
                }
                if (stack == nullptr)
                    continue;
                scene->SetCurrentAnimationStack(stack);
                std::vector<FbxAnimLayer*> layers;
                for (int objectIndex = 0; objectIndex < stack->GetSrcObjectCount(); ++objectIndex)
                {
                    FbxObject* source = stack->GetSrcObject(objectIndex);
                    if (source != nullptr && std::strcmp(source->GetClassId().GetName(), "FbxAnimLayer") == 0)
                        layers.push_back(static_cast<FbxAnimLayer*>(source));
                }
                const FbxTimeSpan span = stack->GetLocalTimeSpan();
                const double startSeconds = span.GetStart().GetSecondDouble();
                const double duration = span.GetDuration().GetSecondDouble();
                if (!std::isfinite(duration) || duration <= 0.0)
                    continue;
                const double frameCountDouble = std::ceil(duration * frameRate);
                if (frameCountDouble > 360000.0)
                {
                    LOG_ERROR_CAT("FbxModelImporter", "Animation duration exceeds import limit: {}", stackName->Buffer());
                    continue;
                }
                const std::size_t frameCount = static_cast<std::size_t>(frameCountDouble) + 1;

                AnimationResource animation;
                const std::string sourceName = stackName->Buffer()[0] == '\0' ? "Animation" : stackName->Buffer();
                const std::uint32_t duplicateIndex = nameCounts[sourceName]++;
                animation.name = duplicateIndex == 0 ? sourceName : sourceName + "_" + std::to_string(duplicateIndex);
                animation.duration = static_cast<float>(duration);
                animation.ticksPerSecond = 1.0f;
                animation.channels.reserve(nodes.size());
                for (FbxNode* node : nodes)
                {
                    if (!hasAnimationCurves(node, layers))
                        continue;
                    AnimationChannel channel;
                    channel.nodeName = nodePath(node);
                    channel.positions.reserve(frameCount);
                    channel.rotations.reserve(frameCount);
                    channel.scales.reserve(frameCount);
                    for (std::size_t frame = 0; frame < frameCount; ++frame)
                    {
                        const double seconds = std::min(duration, static_cast<double>(frame) / frameRate);
                        FbxTime currentTime;
                        currentTime.SetSecondDouble(startSeconds + seconds);
                        const FbxAMatrix transform = reflectX(node->EvaluateLocalTransform(currentTime));
                        channel.positions.push_back({ toVector3(transform.GetT()), static_cast<float>(seconds) });
                        channel.rotations.push_back({ toVector4(transform.GetQ()), static_cast<float>(seconds) });
                        channel.scales.push_back({ toVector3(transform.GetS()), static_cast<float>(seconds) });
                    }
                    animation.channels.push_back(std::move(channel));
                }
                animations.push_back(std::move(animation));
            }
            return animations;
        }
    }

    std::shared_ptr<ModelResource> FbxModelImporter::importModel(const std::filesystem::path& path) const
    {
        LOG_INFO_CAT("FbxModelImporter", "Import started: {}", path.string());
        std::unique_ptr<ImportedScene> imported = loadScene(path);
        if (imported == nullptr || imported->scene->GetRootNode() == nullptr)
            return nullptr;
        auto model = std::make_shared<ModelResource>();
        model->sourcePath = path;
        MaterialIndexMap materialIndices;
        loadMaterials(imported->scene, path, *model, materialIndices);
        BoneIndexMap boneIndices;
        if (!loadSkeleton(imported->scene, *model, boneIndices)
            || !loadNodes(imported->scene->GetRootNode(), -1, *model, boneIndices, materialIndices))
        {
            LOG_ERROR_CAT("FbxModelImporter", "Could not convert FBX scene: {}", path.string());
            return nullptr;
        }
        model->animations = loadAnimations(imported->scene);
        calculateBounds(*model);
        if (model->meshes.empty())
        {
            LOG_ERROR_CAT("FbxModelImporter", "FBX scene contains no mesh: {}", path.string());
            return nullptr;
        }
        LOG_INFO_CAT("FbxModelImporter", "Import succeeded: {} meshes, {} materials, {} joints, {} animations",
            model->meshes.size(), model->materials.size(), model->skeleton ? model->skeleton->bones.size() : 0,
            model->animations.size());
        return model;
    }

    std::vector<AnimationResource> FbxModelImporter::importAnimations(const std::filesystem::path& path) const
    {
        std::unique_ptr<ImportedScene> imported = loadScene(path);
        if (imported == nullptr || imported->scene->GetRootNode() == nullptr)
            return {};
        std::vector<AnimationResource> animations = loadAnimations(imported->scene);
        LOG_INFO_CAT("FbxModelImporter", "Animation import succeeded: {} animations from {}",
            animations.size(), path.string());
        return animations;
    }
} // namespace Engine