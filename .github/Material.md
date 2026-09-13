---
applyTo: "Source/Assets/Material/**/*.cpp,Source/Assets/Material/**/*.h,Source/Graphics/Material/**/*.cpp,Source/Graphics/Material/**/*.h,Source/Core/GameObject/Component/*RendererComponent.cpp,Source/Core/GameObject/Component/*RendererComponent.h,Source/Graphics/Renderer/**/*.cpp,Source/Graphics/Renderer/**/*.h,Assets/Shaders/**/*,Schemas/FlatBuffers/Material.fbs,Schemas/FlatBuffers/Model.fbs"
---

# Material System 実装指示書

## 1. 目的

この自作ゲームエンジンへ、Unityの使いやすさを参考にしつつ、既存のDirectX 12描画構造に適合したMaterial Systemを実装する。

Material Systemは以下を実現する。

- Materialをモデルから独立したAssetとして保存、共有できる
- Shader、描画状態、数値Parameter、Texture参照をMaterialとして管理できる
- ModelのSubMeshごとに既定Materialを割り当てられる
- ModelRendererComponentからMaterial Slotを上書きできる
- CPU AssetとGPU Runtime Resourceを分離する
- Opaque、Alpha Test、Transparentを明示的に扱う
- Shader Hot Reloadや将来のPBR拡張へ対応できる

最初からUnityの全機能を再現せず、固定Standard PBR Materialから段階的に拡張する。

---

## 2. 最重要方針

以下を必ず守る。

- `MaterialComponent`は作成しない
- MaterialはComponentではなく共有可能なAssetとして扱う
- Materialの割り当ては各RendererComponentが所有する
- Material AssetはDirectX 12 API型を保持しない
- GPU用MaterialはMaterial GPU Cacheで管理する
- Model GPU CacheへMaterial生成責務を持たせない
- Asset間の永続参照にはPathではなくGUIDを優先する
- Runtimeの描画Hot Pathで文字列検索を行わない
- Runtimeの描画Hot PathでParameter Mapを毎回検索しない
- TransparencyをOpacity値から推測しない
- Shader Variantを無制限に生成しない
- GPU使用中のResourceを即座に破棄しない
- 既存のModel表示を維持しながら段階的に移行する

---

## 3. 全体Architecture

```text
Shader Asset
    ↓ Parameter Layout / Pass / Default Value
Material Asset
    ↓ Runtime Resolve
Material GPU Cache
    ├─ Pipeline Cache
    ├─ Texture Manager
    └─ Constant Buffer / Descriptor Table

Model Asset
    └─ Material Slot[]

ModelRendererComponent
    ├─ ModelHandle
    └─ Material Override[]
            ↓
ModelRenderSubmission
            ↓
Render Queue
            ↓
Render Pass / Draw
```

---

## 4. 責務分離

### Shader Asset

Shader AssetはMaterialが使用可能なParameterとPassの契約を定義する。

主な情報:

- Shader GUID
- Shader Program参照
- Vertex / Pixel Shader
- Material Parameter Layout
- Texture Slot Layout
- Default Value
- Render Pass定義
- 使用可能なKeywordとVariant
- 対応するVertex Layout
- Shader Version

初期実装では汎用Reflection Systemを必須にしない。

まず固定のStandard PBR Shader定義を作成し、既定Parameter LayoutをコードまたはAsset Metadataで明示する。

### Shared Material Asset

Material Assetはシリアライズ可能な不変の共有Resourceとする。

主な情報:

- Material GUID
- Name
- Shader GUID
- Surface Type
- Render State
- Shader Keywords
- Parameter Values
- Texture Asset GUIDs
- Sampler設定
- Asset Version

Material Assetは以下を保持してはならない。

- `ID3D12Resource`
- `ID3D12PipelineState`
- `D3D12_GPU_DESCRIPTOR_HANDLE`
- Command List
- Renderer固有の一時状態

### Material Manager

Material ManagerはMaterial Assetのロード、キャッシュ、寿命管理を担当する。

既存のModelManager、TextureManagerと同様にIndexとGenerationを持つ`MaterialHandle`を使用する。

担当:

- `load`
- `create`
- `get`
- `unload`
- Path Cache
- GUID Lookup
- Generation検証

Renderer LogicやDirectX 12 API呼び出しをMaterial Managerへ入れない。

### Material GPU Cache

Material GPU CacheはMaterialHandleに対応するGPU描画表現をRender Thread上で遅延作成する。

主な情報:

- 元のMaterial Assetへの共有参照
- 解決済みTextureHandle
- パック済みMaterial Constant Data
- Material Constant Buffer
- Descriptor TableまたはDescriptor Index
- PipelineHandle
- Shader Variant ID
- Resource Generation
- 最終使用Fence値

Material GPU CacheはRender Thread専用とし、Thread SafetyをDoxygenへ明記する。

### Renderer Component

ModelRendererComponentは以下を保持する。

```text
ModelHandle model
MaterialHandle[] materialOverrides
bool castShadows
```

Material OverrideはModelのMaterial Slotと同じIndexで対応させる。

Material解決順:

1. RendererのMaterial Override
2. Model Assetの既定Material
3. Engine Default Material

無効なOverrideは既定MaterialへFallbackする。

---

## 5. MaterialComponentを作成しない理由

MaterialはGameObjectの振る舞いではなく、Rendererが描画に使用するResourceである。

MaterialComponentを作ると以下が曖昧になる。

- 1モデルに複数SubMeshと複数Materialがある場合の対応
- 同じGameObjectに複数Rendererが存在する場合の所有先
- Sprite、Particle、DecalなどRendererごとの差異
- Rendererが存在しないGameObjectへMaterialComponentだけが付いた状態

将来Rendererの種類が増えた場合も、それぞれのRendererComponentがMaterial Slotを保持する。

共通処理はComponent継承ではなく、`MaterialSlotSet`などの値型またはRenderer内部Helperとして共有する。

---

## 6. Materialの種類

### Material Asset

永続化され、複数Rendererから共有される基本Material。

通常は変更不能なResource Snapshotとして公開する。

### Material Instance

実行中に個体固有のMaterial値が必要な場合だけ作成する。

親Material AssetとOverride値を保持し、全Parameterを不用意に複製しない設計を優先する。

Material Instanceの作成を暗黙に行わない。

### Material Property Block

Renderer単位またはDraw単位の一時的なParameter Overrideに使用する。

用途:

- Damage表示による一時的な色変更
- ObjectごとのTint
- Dissolve量
- Selection表示

Property BlockからShader、Surface Type、Blend、Cull、Depth Stateを変更してはならない。

Pipeline Stateを変える変更は別MaterialまたはMaterial Instanceとして扱う。

---

## 7. Standard PBR Material

初期実装ではMetallic-Roughness方式を採用する。

最低限のParameter:

- Base Color: `Vector4`
- Metallic: `float`
- Roughness: `float`
- Emissive Color: `Vector3`
- Emissive Intensity: `float`
- Normal Scale: `float`
- Occlusion Strength: `float`
- Alpha Cutoff: `float`

最低限のTexture Slot:

- Base Color
- Normal
- Metallic-Roughness
- Ambient Occlusion
- Emissive

MetallicとRoughnessの個別Textureを入力として受け取る場合、ImporterまたはAsset Build時に統合する方針を検討する。

初期実装で両方式を同時にShaderへ持ち込み、Variantを増やしすぎない。

---

## 8. Surface TypeとRender State

Surface Typeは明示的な`enum class`として管理する。

- Opaque
- AlphaTest
- Transparent

Opacityが1未満であることだけを理由にTransparentへ変更しない。

Materialが保持する描画状態:

- Surface Type
- Cull Mode
- Depth Test
- Depth Write
- Blend Mode
- Alpha Cutoff
- Render Queue Offset

自由なRender Stateをすべて初期実装へ入れず、必要な状態を型安全な列挙値として追加する。

基本設定:

| Surface Type | Blend | Depth Write | Pass |
| --- | --- | --- | --- |
| Opaque | Off | On | Opaque |
| AlphaTest | Off | On | AlphaTest |
| Transparent | SrcAlpha | Off | Transparent |

---

## 9. Shader Pass

Material Shaderは必要に応じて複数Passを提供できる設計にする。

想定Pass:

- ForwardOpaque
- ForwardTransparent
- DepthOnly
- ShadowCaster
- ObjectID

すべてのMaterialがすべてのPassを持つ必要はない。

Passが存在しない場合は、その描画Passへの提出を行わない。

ShadowCasterではMaterialのBase Color全体をBindせず、AlphaTestに必要なTextureとCutoffだけを利用できる構造を目指す。

---

## 10. Parameter System

EditorおよびSerialized AssetではParameter名を保持してよい。

RuntimeではParameter名から安定した`MaterialParameterID`を生成し、Shader Layoutが次を解決する。

- Type
- Constant Buffer Offset
- Size
- Texture Slot
- Default Value

対応型の初期範囲:

- Bool
- Int
- Float
- Vector2
- Vector3
- Vector4
- Color
- Texture2D
- TextureCube

描画ごとに`unordered_map<string, Value>`を検索しない。

Materialロード時またはGPU Cache生成時に、Parameterを連続したConstant Buffer Dataへパックする。

型不一致、存在しないParameter、重複Parameterは警告し、安全なDefault Valueを使用する。

---

## 11. Texture Fallback

Texture未設定またはロード失敗時は用途別Fallback Textureを使用する。

- Base Color: White
- Normal: Flat Normal `(0.5, 0.5, 1.0)`
- Metallic-Roughness: Metallic 0、Roughness 1となる既定Texture
- Ambient Occlusion: White
- Emissive: Black
- Opacity: White

Textureロード失敗によってMaterial全体の描画を停止しない。

Fallback発生時はAsset PathまたはGUIDを含む警告を一度だけ記録する。

Color Space:

- Base Color、Emissiveは原則sRGB
- Normal、Metallic、Roughness、AOはLinear

---

## 12. Model Assetとの関係

現在のModelResource内MaterialResourceは、外部モデルをImportするための中間データとして扱う。

最終的なModel AssetはMaterial本体を埋め込まず、Material SlotごとのMaterial GUIDを保持する構造へ移行する。

```text
Model Asset
    meshes[]
    materialSlots[]
        name
        defaultMaterialGUID

SubMesh
    materialSlotIndex
```

Import時:

1. AssimpからMaterial情報を読み取る
2. Engine Material Assetへ変換する
3. 必要ならModelのSub Assetとして生成する
4. Model Material SlotへGUIDを登録する
5. SubMeshはSlot Indexだけを保持する

Model再Import時はSlot名を利用して既存Overrideを可能な限り維持する。

---

## 13. Serialization

Material用のFlatBuffers Schemaを追加する。

推奨ファイル:

- `Schemas/FlatBuffers/Material.fbs`
- `Source/Assets/Material/Serialization/MaterialSerializer.h`
- `Source/Assets/Material/Serialization/MaterialSerializer.cpp`

Material FileにはFileHeader、Schema Version、Asset Versionを含める。

Shader更新時のParameter移行規則:

- 同名かつ同型: 値を維持
- 新規Parameter: Shader Default Valueを使用
- 削除済みParameter: 読み飛ばして警告
- 型変更: 既定値へ戻して警告

不正なMaterial File、無効GUID、欠落Shaderによってクラッシュしてはならない。

Atomic SaveとFlatBuffers Verifierを使用する。

---

## 14. CPU/GPUデータ分離

Material AssetとMaterial GPU Resourceを明確に分離する。

```text
MaterialAsset
    ↓ MaterialManager
MaterialHandle
    ↓ MaterialGpuCache::getOrCreate
MaterialGpuResource
```

Material GPU Resourceの作成失敗時はError MaterialへFallbackする。

GPU Resourceの破棄にはFenceを利用し、GPU使用完了前にConstant BufferやDescriptorを再利用しない。

Hot Reload成功時は新しいGPU Resourceを作成してから切り替える。

失敗時は現在使用中のMaterialとPipelineを維持する。

---

## 15. Binding Frequency

GPUへ送るDataを更新頻度で分離する。

### Frame Data

- View Matrix
- Projection Matrix
- Camera Position
- Light Data

### Object Data

- World Matrix
- Object ID
- Bone Palette
- Material Property Block

### Material Data

- Base Color
- Metallic
- Roughness
- Emissive
- Texture Descriptor

現在のWorld View ProjectionとBase Colorを同じObject Constantsとして扱う構造は分離する。

同じMaterialを連続描画するとき、Material Constant BufferとDescriptor Tableを再Bindしない。

---

## 16. Pipeline Cache

PSOはMaterialごとに無条件で生成しない。

Pipeline Cache Keyには必要に応じて以下を含める。

- Shader Variant ID
- Render Pass
- Vertex Layout
- Blend State
- Rasterizer State
- Depth State
- Render Target Format
- Depth Stencil Format
- Sample Count

Material GPU ResourceはPipelineを所有せず、共有PipelineHandleを参照する設計を優先する。

Shader Hot Reload時は影響を受けるPipelineだけを再構築する。

---

## 17. Render SubmissionとSorting

ModelRenderSubmissionはModel Handleに加え、Material Overrideまたは解決に必要なMaterial Set HandleをRender Threadへ渡す。

GameObjectやComponentへのPointerをRender Threadへ渡さない。

RenderItemは個別のMaterial値を多数保持せず、主に以下を参照する。

- Pipeline Handle
- Material GPU Handle
- Mesh GPU Resource
- Object Data
- Render Pass
- Camera Depth

Material IDにはモデル内のローカルMaterial Indexを使用しない。

全モデル間で一意なRuntime Material IDまたはMaterial GPU Handleを使用する。

Opaque系Sort順:

1. Render Pass
2. Pipeline
3. Material
4. Mesh
5. Object

Transparent Sort順:

1. Render Pass
2. Cameraから遠い順
3. 同距離の場合のみPipelineやMaterialを補助Keyとして使用

固定12bit FieldへIDを切り詰める方式に依存しない。

---

## 18. Shader Variant

Shader Keywordは定義済みのものだけ使用可能にする。

初期候補:

- USE_NORMAL_MAP
- USE_EMISSIVE_MAP
- USE_SKINNING
- USE_ALPHA_TEST

すべてのKeywordの自由な組み合わせを生成しない。

Shader Assetが有効な組み合わせを定義し、Asset Build時またはShader登録時に必要Variantだけを生成する。

数値Parameterの変更だけではVariantやPSOを再生成しない。

---

## 19. Editor UX

ModelRendererComponentのInspectorでは以下を提供する。

- Model選択
- Model Material Slot一覧
- 各SlotのMaterial表示
- Material Overrideの設定
- Override解除
- Material Assetを開く操作

Material InspectorではShader Layoutに従ってParameter UIを生成する。

- ColorはColor Editor
- TextureはAsset Picker
- BoolはCheckbox
- EnumはCombo Box
- 範囲付きFloatはSlider

共有Materialを編集していることが分かるUIにする。

Runtime Instance生成を暗黙に行わない。

---

## 20. Error Material

Engine組み込みのDefault MaterialとError Materialを用意する。

Default Material:

- Standard PBR
- White Base Color
- Opaque

Error Material:

- ShaderまたはMaterial解決失敗を視認できる色
- 外部Assetへ依存しない
- Shaderロード失敗時にも可能な範囲で描画できる

無効Material HandleによってDraw全体を中断しない。

---

## 21. Thread Safety

基本方針:

- Material Manager: 複数Threadからの取得を考慮する
- Material Asset: 公開後はImmutable Snapshot
- Material GPU Cache: Render Thread Only
- File Watch / Import: Worker Thread可
- GPU Resource交換: Render Threadで実行

Main ThreadからRender Threadへ渡すSubmissionは値またはHandleで構成する。

Asset Reload中のResourceを直接書き換えず、新しいSnapshotを作成して安全な地点で交換する。

---

## 22. 推奨ディレクトリ

```text
Source/
├─ Assets/
│  └─ Material/
│     ├─ MaterialAsset.h
│     ├─ MaterialManager.h
│     ├─ MaterialManager.cpp
│     ├─ MaterialTypes.h
│     └─ Serialization/
│        ├─ MaterialSerializer.h
│        └─ MaterialSerializer.cpp
│
└─ Graphics/
   └─ Material/
      ├─ MaterialGpuCache.h
      ├─ MaterialGpuCache.cpp
      ├─ MaterialGpuResource.h
      ├─ MaterialPropertyBlock.h
      └─ MaterialParameterLayout.h

Schemas/
└─ FlatBuffers/
   └─ Material.fbs
```

既存の実際の命名、Namespace、Header構造と異なる場合は既存Projectを優先する。

---

## 23. 段階的な実装順序

### Phase 1: 固定Standard Material

- MaterialHandle
- Material Asset
- Material Manager
- Material Serializer
- Standard PBR固定Parameter
- Default MaterialとError Material

### Phase 2: Renderer統合

- Model Material Slot
- ModelRendererComponent Material Override
- ModelRenderSubmissionへのMaterial情報追加
- グローバルMaterial IDによるSorting

### Phase 3: GPU分離

- Material GPU Cache
- Model GPU CacheからTexture解決を移動
- Material Constant Buffer
- Texture Descriptor Binding
- Fence対応

### Phase 4: Render Pass

- Opaque
- AlphaTest
- Transparent
- ShadowCaster
- DepthOnly

### Phase 5: 拡張機能

- Material Property Block
- Material Instance
- Shader Parameter Layout
- Shader Variant
- Shader Hot Reload連携
- Editor自動UI

各Phase終了時にBuildと描画確認を行い、複数Phaseを一度に実装しない。

---

## 24. 既存コードからの移行

移行中は既存ModelResourceのMaterialResourceを読み込める状態を維持する。

初期移行:

1. MaterialResourceから一時Material Assetを生成する
2. MaterialHandleへ登録する
3. SubMesh Material IndexをMaterial Slot Indexとして扱う
4. Material GPU Cache経由でBase ColorとBase Color TextureをBindする
5. 既存描画結果と一致することを確認する

その後、Metallic、Roughness、Normal、AO、Emissiveを順に追加する。

古いModel Fileを即座に読めなくするSchema変更は避ける。

Schema VersionまたはAsset Versionで旧形式を識別し、必要に応じて変換する。

---

## 25. 禁止事項

- MaterialComponentの追加
- ModelGpuResourceによるMaterial全体の所有
- Model AssetへのGPU Descriptor埋め込み
- RendererからMaterial Asset内部値を直接書き換える処理
- OpacityによるSurface Type自動判定
- DrawごとのTextureロード
- Drawごとの文字列Parameter検索
- Materialごとの重複PSO生成
- 無制限なShader Variant生成
- raw pointerによるResource所有
- GPU Fenceを無視したResource破棄
- Shader Reload失敗時の旧Pipeline破棄
- Material不正時のEngine Crash

---

## 26. 検証項目

最低限、以下を確認する。

- 同じMaterial Assetを複数ModelRendererで共有できる
- 1モデル内の複数SubMeshへ異なるMaterialを設定できる
- Renderer単位でMaterial SlotをOverrideできる
- Override解除後にModel既定Materialへ戻る
- Texture未設定時に用途別Fallbackが表示される
- Opaque、AlphaTest、Transparentが正しいPassへ分類される
- 異なるモデルの同じローカルMaterial Indexが衝突しない
- Material切替回数がRender QueueのSortingで削減される
- 無効Material、Shader、Textureでクラッシュしない
- Material Reload失敗時に旧Materialが維持される
- GPU使用中ResourceがFence完了前に破棄されない
- Shader Hot Reload後に影響Pipelineだけが更新される
- DebugとReleaseの両方で新しいWarningが発生しない

---

## 27. 完了条件

Material Systemの初期実装は以下を満たした時点で完了とする。

- Materialが独立Assetとして保存、ロードできる
- MaterialHandleで安全に参照できる
- Standard PBR Materialを描画できる
- ModelRendererComponentでMaterial Slot Overrideができる
- Material GPU CacheがGPU Resourceを管理している
- Model GPU CacheがMaterial生成を担当していない
- Surface Typeが明示されている
- Default MaterialとError Materialが動作する
- FlatBuffers検証とVersion確認がある
- Buildが成功する
- 既存Modelの描画がRegressionしていない
- Public APIへDoxygenが記載されている

実装後は変更箇所、責務分離、Build結果、残課題を報告すること。
