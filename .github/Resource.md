---
applyTo: "Engine/Resource/**/*.cpp,Engine/Resource/**/*.h,Engine/Resource/**/*.hpp"
---

# Resource System 実装指示書

## 1. 目的

Resource SystemはEngineで使用するAssetおよびRuntime Resourceのロード、管理、Cache、Lifetimeを担当する。

対象:

- Texture
- Mesh
- Material
- Shader
- Model
- Skeleton
- Animation
- Scene
- Audio
- Compute Resource
- その他Engine Asset

---

# 2. ResourceとImporterの分離

Resource SystemとAsset Importerを分離する。

Importer:

外部Asset
↓
Parse
↓
Convert
↓
Engine Resource Data

Resource System:

Engine Resource
↓
Cache
↓
Lifetime
↓
GPU Resource

---

# 3. Importer

ImporterはAsset形式ごとの処理を担当する。

将来的に以下を追加可能とする。

- FBX
- PMX
- glTF
- OBJ
- DDS
- PNG
- JPG
- TGA
- WAV
- その他形式

ImporterがRendererへ直接依存してはならない。

---

# 4. Resource Manager

ResourceManagerは主に以下を担当する。

- Load
- Find
- Cache
- Reference
- Unload
- Lifetime Management

Renderer LogicをResourceManagerへ入れない。

---

# 5. Resource Handle

長寿命ResourceはHandle方式を検討する。

Handleには必要に応じて:

- Index
- Generation

を持たせる。

無効Handleを検出できる設計を優先する。

---

# 6. Resource ID

Resourceを識別するためにAsset PathやGUIDなどを利用できる。

同一Assetを異なるPath表現によって二重ロードしないようにする。

---

# 7. Path Normalization

Asset Pathは可能な限り正規化する。

例えば:

- 相対Path
- 絶対Path
- `./`
- `../`
- 区切り文字
- 大文字小文字

などを適切に処理する。

---

# 8. Cache

同一Resourceを複数回ロードしない。

基本:

Request
↓
Cache Lookup
↓
存在
→ Return Existing Resource

存在しない
→ Load
→ Create
→ Cache
→ Return

---

# 9. Async Loading

重いAssetは非同期ロード可能な設計にする。

例:

Request
↓
Loading
↓
CPU Processing
↓
GPU Upload
↓
Ready

Resource Stateを必要に応じて:

- Unloaded
- Loading
- CPU Ready
- GPU Uploading
- Ready
- Failed

などに分ける。

---

# 10. GPU Resource

CPU ResourceとGPU Resourceを分離して考える。

例:

TextureAsset
↓
TextureData
↓
GPU Texture

MeshAsset
↓
MeshData
↓
GPU Buffer

---

# 11. GPU Lifetime

GPU ResourceはGPU使用完了前に破棄してはならない。

必要に応じてFenceによるDeferred Destructionを使用する。

---

# 12. Texture

Texture Systemは必要に応じて以下を管理する。

- Format
- Width
- Height
- Mip Levels
- Array Size
- Usage
- Resource State
- SRV
- UAV
- RTV
- DSV

---

# 13. Mesh

Mesh Resourceは必要に応じて以下を保持する。

- Vertex Data
- Index Data
- Vertex Layout
- Submesh
- Material Reference
- Bounding Volume

---

# 14. Material

MaterialはRendering Parameterを管理する。

代表:

- Base Color
- Metallic
- Roughness
- Normal
- Emissive
- AO

MaterialがGPU API固有オブジェクトへ直接依存しすぎないようにする。

---

# 15. Shader Resource

Shader Resourceは:

- Source Path
- Entry Point
- Shader Model
- Defines
- Bytecode
- Reflection Data

などを管理可能とする。

Shader Cacheを利用する。

---

# 16. Shader Hot Reload

Editor / Debug BuildではShader Hot Reloadを実装可能な設計にする。

Hot Reloadによって:

- Shader
- Root Signature
- PSO

の整合性が壊れないようにする。

---

# 17. Model

Model Resourceは必要に応じて:

- Mesh
- Material
- Skeleton
- Animation
- Node Hierarchy
- Skinning Data

を参照する。

---

# 18. Animation

Animation Resourceでは:

- Skeleton
- Bone
- Animation Clip
- Keyframe
- Skinning Data

を管理可能とする。

Animation RuntimeとAsset Import処理を分離する。

---

# 19. Reference Counting

Resource LifetimeをReference Countで管理する場合、Thread Safetyを考慮する。

ただし、すべてのResourceをshared_ptrだけで管理する設計は禁止する。

---

# 20. Resource State

Resource Stateは明確に管理する。

CPU ResourceとGPU ResourceでStateを混同しない。

---

# 21. Error Handling

Load Failureを無視しない。

エラーには可能な限り:

- Asset Path
- Resource Type
- Importer
- Error Message

を含める。

---

# 22. Failed Resource

必要に応じてPlaceholder Resourceを利用できる設計にする。

例:

Missing Texture
→ Default Texture

Missing Material
→ Default Material

ただし、元のLoad Errorを隠さない。

---

# 23. Resource Dependencies

Resource間のDependencyを明確にする。

例:

Model
├── Mesh
├── Material
│   └── Texture
└── Skeleton
    └── Animation

依存ResourceのLifetimeを考慮する。

---

# 24. Threading

Resource LoadingはWorker Threadで実行可能な設計にする。

GPU UploadはGraphics / RHIのThreading Modelに従う。

CPU Asset ProcessingとGPU Resource Creationを分離する。

---

# 25. Serialization

Resourceを必要に応じてBinary形式へ変換できる設計を検討する。

例:

Source Asset
↓
Importer
↓
Engine Asset
↓
Binary Cache
↓
Runtime Load

Runtimeで重いImport処理を毎回行わない。

---

# 26. Asset Database

将来的にAsset Databaseを追加可能な設計にする。

Asset Databaseは:

- Asset ID
- Path
- Type
- Dependency
- Import State
- Metadata

などを管理できる。

---

# 27. 禁止事項

以下は禁止する。

- ResourceManagerへRendering Logicを追加
- Importerから直接Sceneへ登録
- 毎FrameAssetをLoad
- 同一Assetの二重Cache
- GPU使用中Resourceの破棄
- Shaderの毎FrameCompile
- RendererからImporterを直接呼ぶ
- ResourceのOwnershipを曖昧にする
- エラーをPlaceholderだけで隠す

---

# 28. 実装方針

Resource機能を追加するときは、

1. Resource Typeを定義
2. Ownershipを設計
3. CPU / GPU Resourceを分離
4. Importerを設計
5. Cacheを設計
6. Lifetimeを設計
7. Async Loadingを検討
8. Error Handlingを実装
9. GPU Synchronizationを確認
10. Test

の順番で実装する。