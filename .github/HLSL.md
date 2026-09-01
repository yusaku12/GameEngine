---
applyTo: "Engine/**/Shader/**/*.cpp,Engine/**/Shader/**/*.h,Engine/**/Shader/**/*.hpp,Engine/**/Shaders/**/*.hlsl,Engine/**/Shaders/**/*.hlsli,Shaders/**/*.hlsl,Shaders/**/*.hlsli"
---

# Shader System 実装指示書

## 1. 目的

Shader SystemはGPU上で実行されるShaderのコンパイル、管理、Cache、Reflection、Hot Reload、Pipelineとの連携を担当する。

本EngineではDirectX 12 / HLSL / Shader Modelを基本とする。

---

# 2. 対応Shader

基本Shader:

- Vertex Shader
- Pixel Shader
- Compute Shader
- Geometry Shader
- Hull Shader
- Domain Shader

必要になったShaderのみ実装する。

---

# 3. Shader Model

Shader ModelはProject全体で統一する。

Target例:

```text
vs_6_x
ps_6_x
cs_6_x
```

使用するShader ModelはProject Configurationで管理する。

Shaderごとに勝手にTarget Versionを変更しない。

---

# 4. HLSL

ShaderはHLSLで記述する。

Shader Codeは可能な限りGraphics API固有処理とRendering Logicを分離する。

---

# 5. Shader Directory

基本構造:

```text
Shaders/
├── Common/
├── PBR/
├── Forward/
├── Deferred/
├── Shadow/
├── PostProcess/
├── Particle/
├── Debug/
└── Compute/
```

Shaderの用途ごとに整理する。

---

# 6. HLSL Include

共通処理は`.hlsli`へ分離する。

例:

```text
Common/
 ├── Common.hlsli
 ├── Math.hlsli
 ├── Lighting.hlsli
 ├── BRDF.hlsli
 ├── PBR.hlsli
 └── Sampling.hlsli
```

重複Shader Codeを増やさない。

---

# 7. Shader Entry Point

Entry Pointは明確な名前を使用する。

例:

```text
VSMain
PSMain
CSMain
```

特殊用途では:

```text
ShadowVS
GBufferPS
LightingCS
BloomPS
```

など、役割が明確な名前を使用する。

---

# 8. Constant Buffer

Constant Bufferは用途ごとに分離する。

例:

```text
CameraCB
ObjectCB
MaterialCB
LightCB
PostProcessCB
```

巨大なConstant BufferへすべてのDataを詰め込まない。

---

# 9. Constant Buffer Alignment

Constant Bufferは256-byte Alignmentを考慮する。

CPU側のBuffer LayoutとHLSL側のLayoutを一致させる。

---

# 10. Register Convention

Shader RegisterはProject全体でルールを統一する。

例:

```text
b0 = Frame / Camera
b1 = Object
b2 = Material

t0+ = Texture
s0+ = Sampler
u0+ = UAV
```

実際のEngine仕様に合わせて変更可能だが、ファイルごとに勝手なConventionを作らない。

---

# 11. Root Signature

Root SignatureとShader Register Layoutを一致させる。

基本:

```text
Root Parameter
    ↓
Register
    ↓
Shader Resource
```

Register Bindingの不一致を作らない。

---

# 12. Descriptor Table

大量のResourceをBindingする場合はDescriptor Tableを利用する。

例:

```text
Texture2D[] t0
SamplerState[] s0
```

Descriptor Heap / Descriptor Tableの詳細管理はRHIへ分離する。

---

# 13. Bindless

将来的にBindless Resourceを導入可能な設計にする。

例:

```hlsl
Texture2D g_Textures[];
```

ただし、最初からすべてをBindless化する必要はない。

---

# 14. Structured Buffer

大量のStructured DataにはStructured Bufferを利用する。

例:

- Instance Data
- Particle Data
- Light Data
- Bone Data

---

# 15. Byte Address Buffer

Byte Address Bufferは必要な場合のみ使用する。

Data Layoutを明確にする。

---

# 16. UAV

Compute ShaderやGPU Driven RenderingではUAVを使用する。

代表:

- Particle Buffer
- Visibility Buffer
- Indirect Argument Buffer
- Histogram
- Post Process Data

UAV Resource Stateを正しく管理する。

---

# 17. Texture

Texture Resourceは用途に応じて以下を区別する。

- Texture2D
- Texture3D
- TextureCube
- Texture2DArray

Mip Level / Array / Formatを正しく扱う。

---

# 18. Sampler

Sampler Stateは必要以上に増やさない。

代表:

- Point
- Linear
- Anisotropic
- Comparison
- Clamp
- Wrap

---

# 19. sRGB

TextureのColor Spaceを明確にする。

Base ColorなどのColor Texture:

```text
sRGB → Linear
```

Normal / Roughness / MetallicなどのData Textureでは通常sRGBを使用しない。

---

# 20. Normal Map

Normal MapはLinear Dataとして扱う。

必要に応じてTangent SpaceからWorld Spaceへ変換する。

Tangent / Bitangent / NormalのConventionをEngine全体で統一する。

---

# 21. PBR Shader

PBR Shaderでは以下を基本Inputとする。

- Base Color
- Metallic
- Roughness
- Normal
- AO
- Emission

PBR計算はLinear Spaceで行う。

---

# 22. BRDF

BRDFは共通`.hlsli`へ分離可能とする。

代表:

- Lambert
- GGX
- Smith
- Fresnel Schlick

同じ数学処理を複数Shaderへコピーしない。

---

# 23. IBL

IBLでは必要に応じて:

- Irradiance Map
- Prefiltered Environment Map
- BRDF LUT

を利用する。

Importance Samplingなどの共通処理は共通HLSLへ分離する。

---

# 24. Shadow Shader

Shadow ShaderはLighting Shaderと責務を分離する。

Depth-only Renderingなど、不要な処理をPixel Shaderへ入れない。

---

# 25. Compute Shader

Compute ShaderはGPU Parallel Processingに利用する。

用途例:

- Culling
- Particle Simulation
- Bloom
- Blur
- SSR
- SSAO
- Post Processing
- Light Culling
- Mip Generation

Thread Group Sizeを適切に設定する。

---

# 26. Thread Group

Compute ShaderではThread Group Sizeを明示する。

例:

```hlsl
[numthreads(8, 8, 1)]
```

Hardware特性と処理内容を考慮する。

---

# 27. GPU Synchronization

Shader間のResource Dependencyを明確にする。

Compute ShaderでUAVを書き込んだ後にGraphics Shaderで読む場合などは、RHI / Render Graph側で適切なSynchronizationを行う。

---

# 28. Shader Compile

ShaderはRuntimeで毎FrameCompileしない。

基本:

```text
Shader Source
    ↓
Compile
    ↓
Bytecode
    ↓
Cache
    ↓
Runtime Load
```

---

# 29. Shader Cache

Shader Cacheを利用する。

Cache Keyには必要に応じて:

- Source Path
- Source Timestamp / Hash
- Entry Point
- Shader Type
- Shader Model
- Defines
- Compiler Options

を含める。

古いBytecodeを誤って使用しない。

---

# 30. Shader Compilation Error

Compile Errorでは以下をログへ出力する。

- Shader Path
- Entry Point
- Shader Type
- Define
- Compiler Error
- Line
- Column

Errorを握り潰さない。

---

# 31. Shader Hot Reload

Editor / Debug BuildではShader Hot Reloadを利用可能にする。

基本:

```text
Shader File Changed
    ↓
Recompile
    ↓
Validate
    ↓
Create New Shader
    ↓
Rebuild PSO
    ↓
Replace
```

コンパイル失敗時は現在正常に動作しているShaderを破棄しない。

---

# 32. PSOとの関係

Shader変更時には関連PSOへの影響を確認する。

例:

```text
Shader
 ↓
Root Signature
 ↓
Input Layout
 ↓
Blend State
 ↓
Rasterizer State
 ↓
Depth State
 ↓
RTV / DSV Format
 ↓
PSO
```

Shaderだけ更新してPSOを古い状態のまま使用しない。

---

# 33. Shader Reflection

Shader Reflectionを利用して以下を検証可能にする。

- Constant Buffer
- Resource
- Register
- Resource Type
- Input
- Output

CPU側BindingとShader Bindingの不一致を検出する。

---

# 34. Validation

Debug Buildでは可能な限りBinding Validationを行う。

例:

```text
CPU:
b1 = MaterialCB

Shader:
b1 = MaterialCB
```

一致していることを確認する。

---

# 35. Input / Output

Vertex Shader InputとMesh Vertex Layoutを一致させる。

例:

```text
POSITION
NORMAL
TANGENT
TEXCOORD
COLOR
BLENDWEIGHT
BLENDINDICES
```

不要なVertex AttributeをShaderへ要求しない。

---

# 36. Skinning

Skeletal Animation Shaderでは必要に応じてBone Matrix Bufferを使用する。

代表:

```text
BoneIndex
BoneWeight
BoneMatrix[]
```

CPU側Animation DataとGPU側Layoutを一致させる。

---

# 37. Motion Vector

Temporal Renderingを使用する場合はMotion Vectorを生成可能にする。

必要に応じて:

```text
Current Position
Previous Position
    ↓
Motion Vector
```

Camera MotionとObject Motionの両方を考慮する。

---

# 38. Precision

Precisionは必要に応じて選択する。

`float`を基本とし、`half` / `min16float`などは実際のHardwareとQualityを確認して使用する。

Precision低下によるArtifactを許容しない。

---

# 39. Branching

Shader内のBranchingは必要性を確認する。

Material FeatureをCompile-time Defineで分岐する場合、Permutation数が爆発しないよう注意する。

---

# 40. Shader Permutation

Permutation数を制御する。

例えば:

```text
PBR
× NormalMap
× Skinning
× ClearCoat
× IBL
× Shadow
× Fog
```

のように無制限にPermutationを増やさない。

Featureの組み合わせを整理する。

---

# 41. Material Feature

Material Featureは可能な限り明示的に管理する。

例:

```text
HAS_NORMAL_MAP
HAS_EMISSION
HAS_SKINNING
HAS_CLEAR_COAT
```

不要なShader Variantを生成しない。

---

# 42. Debug Shader

Debug用Shaderを用意可能にする。

例:

- Normal Visualization
- Depth Visualization
- UV Visualization
- Roughness Visualization
- Metallic Visualization
- GBuffer Visualization
- Motion Vector Visualization

---

# 43. Post Process Shader

Post Process Shaderは可能な限り共通処理を再利用する。

例:

```text
Fullscreen Triangle
    ↓
Post Process Shader
    ↓
Render Target
```

不要なVertex Bufferを作らずFullscreen Triangleを利用できる設計を優先する。

---

# 44. Tone Mapping

Tone MappingはHDR Linear ColorをLDRへ変換する。

必要に応じて:

- ACES
- Reinhard
- AgX
- Custom Tone Mapping

を利用可能とする。

---

# 45. Bloom

Bloom Shaderは必要に応じて:

```text
Threshold
 ↓
Downsample
 ↓
Blur
 ↓
Upsample
 ↓
Combine
```

という構造にする。

---

# 46. SSAO / SSR

Screen Space EffectではDepth / Normal / Motion Vectorなどの必要なInputを明確にする。

Screen Spaceで存在しない情報を扱う場合のArtifactを考慮する。

---

# 47. Shader Code Style

HLSLでは以下を守る。

- 意味のあるVariable Name
- Magic Numberを避ける
- 共通処理を関数化
- 共通処理を`.hlsli`へ分離
- 不要なBranchを避ける
- 不要なTexture Sampleを避ける
- Coordinate Spaceを明示する

---

# 48. Coordinate Space

Shader内のCoordinate Spaceを明確にする。

例:

- Object Space
- World Space
- View Space
- Tangent Space
- Clip Space
- Screen Space

Variable Nameなどで可能な限り区別する。

---

# 49. Matrix Convention

Matrix ConventionをEngine全体で統一する。

CPU側DirectXMathとHLSL側のMatrix Layout / Multiplication Orderを一致させる。

Transposeを場当たり的に追加しない。

---

# 50. Shader Performance

Shader Performanceでは以下を確認する。

- Texture Sample数
- ALU
- Register Pressure
- Branch
- Wave Occupancy
- Thread Group Size
- LDS / Shared Memory
- UAV Access
- Divergence

最適化はGPU Profiler / PIXなどの結果に基づいて行う。

---

# 51. Shader Debugging

Debug時にはShader Debugger / GPU Captureを利用する。

問題発生時には:

1. Input
2. Resource
3. Register
4. Constant Buffer
5. Texture
6. Sampler
7. Shader
8. PSO
9. Resource State

の順番で確認する。

---

# 52. 禁止事項

以下は禁止する。

- 毎Frame Shader Compile
- Binding Registerの無秩序な変更
- CPU / HLSL Layoutの不一致
- Shader Compile Errorの無視
- Hot Reload失敗時に正常Shaderを破棄
- 無制限Shader Permutation
- sRGB / Linearの混同
- Coordinate Spaceの混同
- ShaderからRenderer内部へ直接依存
- PSOとShaderの不整合
- ProfilingなしのShader最適化

---

# 53. 実装優先順位

Shader Systemは以下の順番で構築する。

1. Shader Compile
2. Shader Cache
3. Shader Resource
4. Root Signature Integration
5. PSO Integration
6. Reflection
7. Hot Reload
8. PBR Shader
9. Shadow Shader
10. Compute Shader
11. Post Process Shader
12. Advanced Shader Pipeline

---

# 54. 最終原則

Shader Systemは単なる「HLSLをCompileするクラス」ではない。

以下を一貫して管理する。

```text
HLSL
 ↓
Compilation
 ↓
Bytecode
 ↓
Reflection
 ↓
Root Signature
 ↓
PSO
 ↓
Rendering
```

Shader、Root Signature、PSO、Resource Bindingの整合性を常に維持する。

Shaderの変更によってRendering Pipeline全体のBinding Contractが壊れないように設計する。