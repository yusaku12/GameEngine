#include "Pch.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Material\Serialization\MaterialSerializer.h"
#include "Assets\Material\Serialization\TextureReferencePaths.h"
#include "Assets\Model\Import\FbxModelImporter.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Generated\FlatBuffers\Model_generated.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\Texture\TextureManager.h"
#include <DirectXTex.h>
#include <fbxsdk.h>
#include <cwctype>

namespace
{
    using namespace Engine;
    namespace Ser = Engine::Serialization;
    namespace fs = std::filesystem;

    void require(const bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class Fixture
    {
    public:
        Fixture()
            : workingDirectory(fs::current_path()),
              root(fs::temp_directory_path() / ("GameEngine-TexturePaths-" + std::to_string(ObjectGUID::generate().low)))
        {
            fs::create_directories(root / "Original" / "Textures");
            fs::create_directories(root / "Original" / "Materials");
            fs::create_directories(root / "Original" / "Models");
            fs::create_directories(root / "Elsewhere");
            DirectX::ScratchImage image;
            require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 1)), "Image creation failed");
            std::memset(image.GetPixels(), 255, image.GetPixelsSize());
            require(SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
                DirectX::DDS_FLAGS_NONE, texture().c_str())), "DDS creation failed");
        }

        ~Fixture()
        {
            std::error_code error;
            fs::current_path(workingDirectory, error);
            if (error)
                std::fprintf(stderr, "Cleanup: cannot restore working directory: %s\n", error.message().c_str());
            fs::remove_all(root, error);
            if (error)
                std::fprintf(stderr, "Cleanup: cannot remove fixture: %s\n", error.message().c_str());
        }

        fs::path texture() const { return root / "Original" / "Textures" / "albedo.dds"; }
        const fs::path workingDirectory;
        const fs::path root;
    };

    MaterialAsset makeMaterial(const fs::path& texture)
    {
        MaterialAsset material;
        material.guid = AssetGUID::generate();
        material.name = "Portable material";
        for (const auto member : Ser::MATERIAL_TEXTURE_PATH_MEMBERS)
            material.textures.*member = texture;
        material.textures.baseColor = AssetGUID::generate();
        return material;
    }

    void requireRelativeHints(const Ser::MaterialTextureReferences* hints, const fs::path& expected)
    {
        require(hints != nullptr, "Serialized texture references missing");
        const std::array paths = { hints->base_color_path(), hints->normal_path(),
            hints->metallic_roughness_path(), hints->ambient_occlusion_path(), hints->emissive_path() };
        for (const flatbuffers::String* path : paths)
            require(path != nullptr && fs::path(path->str()) == expected
                && !fs::path(path->str()).has_root_path(), "Serialized hint is not the expected relative path");
    }

    void testMaterialPersistence(Fixture& fixture)
    {
        const fs::path target = fixture.root / "Original" / "Materials" / "surface.mat";
        const MaterialAsset material = makeMaterial(fixture.texture());
        const MaterialHandle handle = MaterialManager::instance().create(material);
        require(handle.isValid() && MaterialManager::instance().save(handle, target), "Material manager save failed");
        Ser::FlatBufferReader reader;
        require(reader.open(target), "Cannot inspect serialized material");
        const Ser::MaterialFile* file = Ser::GetMaterialFile(reader.data());
        require(file->header()->asset_version() == 2, "New material format version missing");
        requireRelativeHints(file->material()->textures(), fs::path("..") / "Textures" / "albedo.dds");
        require(MaterialManager::instance().get(handle)->textures.baseColorPath == fixture.texture(),
            "Saving changed the runtime path");

        const fs::path saveAs = fixture.root / "Original" / "Models" / "copy.mat";
        require(MaterialManager::instance().save(handle, saveAs), "Material Save As failed");
        fs::current_path(fixture.root / "Elsewhere");
        MaterialAsset loaded;
        require(Ser::MaterialSerializer{}.load(saveAs, loaded), "Material load with unrelated CWD failed");
        for (const auto member : Ser::MATERIAL_TEXTURE_PATH_MEMBERS)
            require(loaded.textures.*member == fixture.texture(), "Material Save As changed texture resolution");
        MaterialManager::instance().clear();
    }

    void testModelPersistence(Fixture& fixture)
    {
        ModelResource model;
        model.sourcePath = fixture.root / "Original" / "Models" / "source.fbx";
        model.embeddedMaterials.push_back(makeMaterial(fixture.texture()));
        model.materialSlots.push_back({ "Surface", model.embeddedMaterials.front().guid, {} });
        const fs::path target = fixture.root / "Original" / "Models" / "mesh.model";
        require(Ser::ModelSerializer{}.save(target, model), "Embedded model save failed");
        {
            Ser::FlatBufferReader reader;
            require(reader.open(target), "Cannot inspect serialized model");
            const Ser::ModelFile* file = Ser::GetModelFile(reader.data());
            require(file->header()->asset_version() == 2, "New model format version missing");
            requireRelativeHints(file->embedded_materials()->Get(0)->textures(), fs::path("..") / "Textures" / "albedo.dds");
            require(!fs::path(file->source_path()->str()).has_root_path(), "Serialized source path is absolute");
        }

        fs::rename(fixture.root / "Original", fixture.root / "Moved");
        const fs::path movedTarget = fixture.root / "Moved" / "Models" / "mesh.model";
        const fs::path movedTexture = fixture.root / "Moved" / "Textures" / "albedo.dds";
        ModelResource loaded;
        require(Ser::ModelSerializer{}.load(movedTarget, loaded), "Moved model load failed");
        for (const auto member : Ser::MATERIAL_TEXTURE_PATH_MEMBERS)
            require(loaded.embeddedMaterials.front().textures.*member == movedTexture, "Moved model uses the old root");
        const ModelHandle handle = ModelManager::instance().load(movedTarget);
        require(handle.isValid(), "Moved model manager load failed");
        require(TextureManager::instance().getAssetPath(model.embeddedMaterials.front().textures.baseColor) == movedTexture,
            "Moved texture GUID was not registered at the new root");
        const fs::path saveAs = fixture.root / "Moved" / "mesh-copy.model";
        require(ModelManager::instance().save(handle, saveAs), "Model manager Save As failed");
        require(Ser::ModelSerializer{}.load(saveAs, loaded), "Model Save As reload failed");
        require(loaded.embeddedMaterials.front().textures.baseColorPath == movedTexture,
            "Model Save As changed the texture target");

        MaterialAsset movedMaterial;
        require(Ser::MaterialSerializer{}.load(fixture.root / "Moved" / "Materials" / "surface.mat", movedMaterial),
            "Moved standalone material load failed");
        require(movedMaterial.textures.baseColorPath == movedTexture, "Moved standalone material uses the old root");
        ModelManager::instance().clear();
        MaterialManager::instance().clear();
        fs::rename(fixture.root / "Moved", fixture.root / "Original");
    }

    void testLegacyModelPaths(Fixture& fixture)
    {
        ModelResource model;
        model.sourcePath = fixture.root / "Original" / "Models" / "source.fbx";
        MaterialResource material;
        material.textures.baseColor = (fs::path("..") / "Textures" / "albedo.dds").string();
        material.textures.normal = fixture.texture().string();
        model.materials.push_back(material);
        const fs::path target = fixture.root / "Original" / "legacy.model";
        require(Ser::ModelSerializer{}.save(target, model), "Legacy texture model save failed");
        Ser::FlatBufferReader reader;
        require(reader.open(target), "Legacy model inspection failed");
        const auto* textures = Ser::GetModelFile(reader.data())->materials()->Get(0)->textures();
        require(fs::path(textures->base_color()->str()) == fs::path("Textures") / "albedo.dds",
            "Legacy relative input was not rebased from source owner");
        require(fs::path(textures->normal()->str()) == fs::path("Textures") / "albedo.dds",
            "Legacy absolute input was not made relative");
        ModelResource loaded;
        require(Ser::ModelSerializer{}.load(target, loaded), "Legacy texture model load failed");
        require(fs::path(loaded.materials.front().textures.baseColor) == fixture.texture(),
            "Legacy texture did not resolve from the model file");
    }

    void writeVersion1Material(const fs::path& target, const fs::path& texture)
    {
        flatbuffers::FlatBufferBuilder builder;
        const Ser::AssetGuid guid(10, 20);
        const Ser::Vec4 color(1, 1, 1, 1);
        const Ser::Vec3 emissive(0, 0, 0);
        const auto textures = Ser::CreateMaterialTextureReferences(builder, &guid, nullptr, nullptr, nullptr, nullptr,
            builder.CreateString(texture.generic_string()));
        const auto state = Ser::CreateMaterialRenderState(builder);
        const auto data = Ser::CreateMaterialAssetData(builder, &guid, builder.CreateString("Version 1"),
            nullptr, state, &color, 0, 1, &emissive, 0, 1, 1, 0.5f, textures);
        const auto header = Ser::CreateFileHeader(builder, 1, 1, 0);
        Ser::FinishMaterialFileBuffer(builder, Ser::CreateMaterialFile(builder, header, data));
        require(Ser::FlatBufferWriter{}.saveAtomic(target,
            std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize())), "Version 1 fixture save failed");
    }

    void testCompatibilityAndErrors(Fixture& fixture)
    {
        const fs::path target = fixture.root / "Original" / "Materials" / "old.mat";
        writeVersion1Material(target, fixture.texture());
        MaterialAsset loaded;
        require(Ser::MaterialSerializer{}.load(target, loaded) && loaded.textures.baseColorPath == fixture.texture(),
            "Version 1 absolute material compatibility failed");
        fs::current_path(fixture.root / "Original");
        writeVersion1Material(target, fs::path("Textures") / "albedo.dds");
        require(Ser::MaterialSerializer{}.load(target, loaded) && loaded.textures.baseColorPath == fixture.texture(),
            "Version 1 CWD-relative material compatibility failed");
        require(Ser::MaterialSerializer{}.save(target, loaded), "Version 1 material migration failed");

        flatbuffers::FlatBufferBuilder builder;
        const Ser::Vec3 zero(0, 0, 0);
        const Ser::Vec4 color(1, 1, 1, 1);
        const auto legacyPaths = Ser::CreateMaterialTexturePaths(builder,
            builder.CreateString("Textures/albedo.dds"), builder.CreateString(fixture.texture().generic_string()));
        const auto legacyMaterial = Ser::CreateMaterial(builder, builder.CreateString("Version 1"), &color,
            0, 1, &zero, 1, legacyPaths);
        const std::array legacyMaterials = { legacyMaterial };
        const auto materialVector = builder.CreateVector(legacyMaterials.data(), legacyMaterials.size());
        const auto bounds = Ser::CreateBounds(builder, &zero, &zero, &zero, 0);
        const auto modelHeader = Ser::CreateFileHeader(builder, 1, 1, 0);
        Ser::FinishModelFileBuffer(builder,
            Ser::CreateModelFile(builder, modelHeader, 0, materialVector, 0, 0, 0, bounds));
        const fs::path oldModel = fixture.root / "Original" / "version1.model";
        require(Ser::FlatBufferWriter{}.saveAtomic(oldModel,
            std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize())), "Version 1 model fixture save failed");
        ModelResource loadedModel;
        fs::current_path(fixture.root / "Elsewhere");
        require(Ser::ModelSerializer{}.load(oldModel, loadedModel), "Version 1 model load failed");
        require(fs::path(loadedModel.materials.front().textures.baseColor) == fixture.texture()
            && fs::path(loadedModel.materials.front().textures.normal) == fixture.texture(),
            "Version 1 model absolute or owner-relative hints changed behavior");

        Ser::FlatBufferReader before;
        require(before.open(target), "Cannot read asset before rejected save");
        MaterialAsset invalid = makeMaterial(fixture.texture());
        invalid.textures.baseColorPath = L"Z:\\unavailable\\albedo.dds";
        require(!Ser::MaterialSerializer{}.save(target, invalid), "Cross-volume path silently saved as absolute");
        Ser::FlatBufferReader after;
        require(after.open(target) && before.size() == after.size()
            && std::memcmp(before.data(), after.data(), before.size()) == 0, "Failed save modified the existing asset");
        fs::path output;
        require(!Ser::resolveAssetReference(L"C:albedo.dds", {}, output), "Drive-relative path was accepted");
        require(Ser::relativizeAssetReference({}, {}, target, output) && output.empty(), "Empty hint was not preserved");
        std::wstring lowerDrive = fixture.texture().wstring();
        lowerDrive.front() = static_cast<wchar_t>(std::towlower(lowerDrive.front()));
        require(Ser::relativizeAssetReference(lowerDrive, {}, target, output)
            && output == fs::path("..") / "Textures" / "albedo.dds", "Drive-letter casing changed relative resolution");
        fs::current_path(fixture.root / "Elsewhere");
    }

    void testFbxRelativeInput(Fixture& fixture)
    {
        const auto destroyManager = [](FbxManager* manager) { if (manager != nullptr) manager->Destroy(); };
        const std::unique_ptr<FbxManager, decltype(destroyManager)> manager(FbxManager::Create(), destroyManager);
        require(manager != nullptr, "FBX fixture manager creation failed");
        manager->SetIOSettings(FbxIOSettings::Create(manager.get(), IOSROOT));
        FbxScene* const scene = FbxScene::Create(manager.get(), "Texture fixture");
        FbxMesh* const mesh = FbxMesh::Create(scene, "Triangle");
        mesh->InitControlPoints(3);
        mesh->SetControlPointAt(FbxVector4(0, 0, 0), 0);
        mesh->SetControlPointAt(FbxVector4(1, 0, 0), 1);
        mesh->SetControlPointAt(FbxVector4(0, 1, 0), 2);
        mesh->BeginPolygon();
        mesh->AddPolygon(0);
        mesh->AddPolygon(1);
        mesh->AddPolygon(2);
        mesh->EndPolygon();
        FbxNode* const node = FbxNode::Create(scene, "Triangle");
        node->SetNodeAttribute(mesh);
        scene->GetRootNode()->AddChild(node);
        FbxSurfacePhong* const fbxMaterial = FbxSurfacePhong::Create(scene, "Surface");
        node->AddMaterial(fbxMaterial);
        FbxFileTexture* const texture = FbxFileTexture::Create(scene, "Albedo");
        texture->SetFileName(fixture.texture().string().c_str());
        texture->SetRelativeFileName("../Textures/albedo.dds");
        fbxMaterial->Diffuse.ConnectSrcObject(texture);
        const fs::path target = fixture.root / "Original" / "Models" / "fixture.fbx";
        FbxExporter* const exporter = FbxExporter::Create(manager.get(), "");
        require(exporter->Initialize(target.string().c_str(), -1, manager->GetIOSettings()) && exporter->Export(scene),
            "FBX fixture export failed");
        fs::current_path(fixture.root);
        const auto imported = FbxModelImporter{}.importModel(fs::path("Original") / "Models" / "fixture.fbx");
        require(imported != nullptr && !imported->materials.empty(), "Relative FBX import failed");
        bool foundTexture = false;
        for (const MaterialResource& material : imported->materials)
        {
            if (material.textures.baseColor.empty())
                continue;
            foundTexture = true;
            require(fs::path(material.textures.baseColor).is_absolute(), "FBX produced a double-join-prone relative path");
            require(fs::is_regular_file(material.textures.baseColor), "FBX texture reference does not exist");
        }
        require(foundTexture, "FBX test did not exercise a texture reference");
        fs::current_path(fixture.root / "Elsewhere");
    }

    void testOtherModelReferences(Fixture& fixture)
    {
        ModelResource model;
        model.sourcePath = fixture.root / "Original" / "Models" / "source.model";
        model.materialSlots.push_back({ "External", AssetGUID::generate(), fs::path("..") / "Materials" / "surface.mat" });
        model.animationClipGuids.push_back(AssetGUID::generate());
        model.animationClipPaths.push_back(fs::path("..") / "animation.clip");
        const fs::path target = fixture.root / "Original" / "rebased.model";
        require(Ser::ModelSerializer{}.save(target, model), "Model reference save failed");
        ModelResource loaded;
        require(Ser::ModelSerializer{}.load(target, loaded), "Model reference reload failed");
        require(loaded.materialSlots.front().defaultMaterialPath == fixture.root / "Original" / "Materials" / "surface.mat",
            "External material reference was not rebased");
        require(loaded.animationClipPaths.front() == fixture.root / "Original" / "animation.clip",
            "External animation reference was not rebased");
        require(loaded.sourcePath == target, "Loaded model owner does not match its current file");
    }

    void testRuntimeReferences(Fixture& fixture)
    {
        fs::current_path(fixture.root / "Original");
        MaterialAsset material = makeMaterial(fs::path("Textures") / "albedo.dds");
        MaterialManager& materials = MaterialManager::instance();
        const MaterialHandle handle = materials.create(material);
        require(handle.isValid() && materials.get(handle)->textures.baseColorPath == fixture.texture(),
            "Material creation did not resolve working-directory-relative input");
        fs::current_path(fixture.root / "Elsewhere");
        require(materials.save(handle, fixture.root / "Original" / "Materials" / "runtime.mat"),
            "Material save after CWD change failed");
        material = *materials.get(handle);
        material.textures.normalPath = L"C:ambiguous.dds";
        require(!materials.update(handle, material), "Invalid texture update reported success");
        materials.clear();

        ModelResource model;
        model.sourcePath = fixture.root / "Original" / "Models" / "source.fbx";
        material = makeMaterial(fs::path("..") / "Textures" / "albedo.dds");
        model.embeddedMaterials.push_back(material);
        model.materialSlots.push_back({ "Relative embedded", material.guid, {} });
        const ModelHandle modelHandle = ModelManager::instance().create(model);
        require(modelHandle.isValid(), "Model with owner-relative embedded material creation failed");
        const auto snapshot = ModelManager::instance().get(modelHandle);
        require(snapshot->embeddedMaterials.front().textures.baseColorPath == fixture.texture(),
            "Embedded material runtime hints were not resolved from the model owner");
        const MaterialHandle embedded = materials.findByGuid(snapshot->embeddedMaterials.front().guid);
        const fs::path target = fixture.root / "Original" / "embedded.mat";
        require(materials.save(embedded, target), "Embedded material standalone save failed");
        MaterialAsset loaded;
        require(Ser::MaterialSerializer{}.load(target, loaded) && loaded.textures.baseColorPath == fixture.texture(),
            "Embedded-to-standalone save changed the texture target");
        ModelManager::instance().clear();
        materials.clear();
    }

    void testBundledModel(Fixture& fixture)
    {
        const fs::path target = fixture.workingDirectory / "Assets" / "Model" / "Jammo" / "Models.model";
        Ser::FlatBufferReader reader;
        require(reader.open(target), "Bundled model cannot be inspected");
        const auto* file = Ser::GetModelFile(reader.data());
        require(file->header()->asset_version() == 2, "Bundled model was not migrated");
        ModelResource loaded;
        require(Ser::ModelSerializer{}.load(target, loaded), "Migrated bundled model verification failed");
        require(loaded.meshes.size() == 25 && loaded.embeddedMaterials.size() == 2
            && loaded.animations.size() == 3, "Bundled model content changed during migration");
        std::size_t hints = 0;
        for (const MaterialAsset& material : loaded.embeddedMaterials)
        {
            for (const auto member : Ser::MATERIAL_TEXTURE_PATH_MEMBERS)
            {
                const fs::path& texture = material.textures.*member;
                if (texture.empty())
                    continue;
                ++hints;
                require(fs::is_regular_file(texture), "Bundled model texture did not resolve to a real file");
            }
        }
        require(hints == 3, "Bundled model texture references changed during migration");
    }

    class GpuFixture
    {
    public:
        ~GpuFixture()
        {
            if (!TextureManager::instance().finalize())
                std::fputs("Cleanup: texture finalization failed\n", stderr);
            fence.finalize();
            queue.finalize();
            device.finalize();
        }

        DX12Device device;
        DX12CommandQueue queue;
        DX12Fence fence;
    };

    void testGpuLoading(Fixture& fixture)
    {
        GpuFixture gpu;
        TextureManager& textures = TextureManager::instance();
        require(gpu.device.initialize(), "D3D12 device initialization failed");
        require(gpu.queue.initialize(*gpu.device.get(), DX12CommandQueueType::DIRECT), "Queue initialization failed");
        require(gpu.fence.initialize(*gpu.device.get()), "Fence initialization failed");
        require(textures.initialize(gpu.device, gpu.queue, gpu.fence), "Texture manager initialization failed");
        const TextureHandle absolute = textures.load(fixture.texture());
        const TextureHandle relative = textures.load(fs::path("..") / "Original" / "Textures" / "albedo.dds");
        require(absolute.isValid() && absolute == relative, "Absolute and relative texture cache entries differ");
        const AssetGUID guid = textures.registerAssetPath(fixture.texture());
        require(guid.isValid() && textures.load(guid) == absolute, "GUID loading did not use the same texture cache");
        MaterialAsset guidOnly = makeMaterial({});
        guidOnly.textures.baseColor = guid;
        const MaterialHandle guidMaterial = MaterialManager::instance().create(guidOnly);
        const fs::path guidTarget = fixture.root / "Original" / "Materials" / "guid-only.mat";
        require(MaterialManager::instance().save(guidMaterial, guidTarget), "GUID-only material save failed");
        MaterialAsset loadedGuidMaterial;
        require(Ser::MaterialSerializer{}.load(guidTarget, loadedGuidMaterial)
            && loadedGuidMaterial.textures.baseColorPath == fixture.texture(), "GUID-only save did not fill a portable hint");
        MaterialManager::instance().clear();
        TextureLoadDesc linear;
        linear.colorSpace = TextureColorSpace::Linear;
        const TextureHandle differentOptions = textures.load(fixture.texture(), linear);
        require(differentOptions.isValid() && differentOptions != absolute, "Load options lost their separate cache entry");
        require(textures.get(absolute)->getState() == TextureState::Ready, "Texture was not uploaded");
        require(!textures.load(fixture.root / "missing.dds").isValid(), "Missing texture reported success");
        require(textures.finalize(), "Texture cleanup failed");
    }
}

int main(const int argc, const char* const argv[])
{
    Engine::LoggerConfig logging;
    logging.useFile = false;
    logging.useDebugOutput = false;
    if (!Engine::Logger::instance().initialize(logging))
        return 1;
    try
    {
        Fixture fixture;
        testMaterialPersistence(fixture);
        testModelPersistence(fixture);
        testLegacyModelPaths(fixture);
        testCompatibilityAndErrors(fixture);
        testFbxRelativeInput(fixture);
        testOtherModelReferences(fixture);
        testRuntimeReferences(fixture);
        if (argc == 2 && std::string_view(argv[1]) == "--verify-jammo")
            testBundledModel(fixture);
        else
            require(argc == 1, "Usage: TexturePathTests.exe [--verify-jammo]");
        testGpuLoading(fixture);
        std::puts("PASS: 8 texture path tests (saved paths, relocation, Save As, version 1, FBX, runtime references and D3D12 loading).");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    Engine::Logger::instance().finalize();
    return 0;
}
