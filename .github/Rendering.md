---
applyTo: "Engine/**/Rendering/**/*.cpp,Engine/**/Rendering/**/*.h,Engine/**/Rendering/**/*.hpp,Engine/**/Renderer/**/*.cpp,Engine/**/Renderer/**/*.h,Engine/**/Renderer/**/*.hpp"
---

# Rendering System 実装指示書

## 1. 目的

Rendering SystemはEngineのScene DataをGPUで描画可能な形式へ変換し、RHIを通してGraphics APIへ処理を送る。

本EngineではDirectX 12を主要Graphics Backendとして使用する。

Rendering Systemは以下を担当する。

- Render Pipeline
- Render Pass
- Render Scene
- Render Object
- Visibility
- Culling
- Sorting
- Lighting
- Shadow
- Material
- Post Processing
- Camera Rendering
- GPU Command Preparation

ただし、DirectX 12 APIそのものの管理はRHI / Graphics Backendへ分離する。

---

# 2. Architecture

基本Architecture:

```text
Scene / ECS
    ↓
Rendering System
    ↓
Render World
    ↓
Visibility / Culling
    ↓
Render Queue
    ↓
Render Pass
    ↓
RHI
    ↓
DirectX 12
    ↓
GPU
```

RendererがGameplayやECSの内部実装へ直接依存しすぎないようにする。

---

# 3. RendererとRHIの責務分離

Rendererは「何を描画するか」を決定する。

RHIは「GPUへどのように命令するか」を担当する。

### Renderer

- Render Object
- Render Pass
- Material
- Lighting
- Culling
- Sorting
- Camera
- Render Graph
- Rendering Policy

### RHI

- Device
- Command Queue
- Command List
- Resource
- Descriptor
- Pipeline
- Fence
- Swap Chain

Rendererから直接`ID3D12Device`などを操作しない。

---

# 4. DirectX 12依存

Rendering LayerからDirectX 12 APIへ直接依存するコードを増やさない。

DirectX 12固有コードは原則としてRHI / Graphics / DirectX12 Backendへ配置する。

悪い例:

```cpp
ID3D12GraphicsCommandList* commandList;
commandList->DrawIndexedInstanced(...);
```

Renderer側で直接使用しない。

Renderer:

```cpp
commandList.drawIndexed(...);
```

RHI:

```cpp
ID3D12GraphicsCommandList::DrawIndexedInstanced(...)
```

という責務分離を優先する。

---

# 5. Render Frame

1 FrameのRendering処理は明確なPhaseに分ける。

基本:

```text
Begin Frame
    ↓
Update Render World
    ↓
Visibility / Culling
    ↓
Build Render Queue
    ↓
Shadow Pass
    ↓
Depth Pass
    ↓
GBuffer / Forward Pass
    ↓
Lighting
    ↓
Transparent Pass
    ↓
Post Processing
    ↓
UI
    ↓
Present
    ↓
End Frame
```

実際のRendering Architectureに応じて順番を変更してよい。

---

# 6. Render World

Gameplay / ECSのDataを直接Rendererへ渡さない。

必要に応じてRendering専用のRender Worldを構築する。

例:

```text
ECS
 ↓
Render World
 ├─ Render Object
 ├─ Light
 ├─ Camera
 ├─ Reflection Probe
 └─ Decal
```

これによりRendererとGameplayを疎結合にする。

---

# 7. Render Object

Render Objectは描画に必要な情報だけを保持する。

例:

- Transform
- Mesh
- Material
- Bounds
- Visibility
- Rendering Flags
- Layer
- Distance

Gameplay用の情報をRender Objectへ持ち込まない。

---

# 8. Visibility

Rendering前にVisibilityを判定する。

代表:

- Frustum Culling
- Distance Culling
- Layer Culling
- Occlusion Culling
- LOD Selection

描画不要なObjectをRender Queueへ追加しない。

---

# 9. Frustum Culling

Camera Frustumを使用してObjectのVisibilityを判定する。

基本:

```text
World Bounds
    ↓
View Frustum
    ↓
Intersection Test
    ↓
Visible / Invisible
```

Bounding Sphere / AABBなどを適切に利用する。

---

# 10. LOD

大量のObjectを描画する場合はLODを利用できる設計にする。

例:

```text
Distance
    ↓
LOD0
LOD1
LOD2
LOD3
```

LOD選択はRendering Layerで行う。

---

# 11. Render Queue

Render QueueはRendering Orderを管理する。

例:

```text
Background
Opaque
Alpha Test
Sky
Transparent
Overlay
UI
```

Transparent ObjectはDepth Sortingが必要になる場合がある。

---

# 12. Sorting

Render Commandを適切にSortする。

考慮する要素:

- Pipeline State
- Material
- Shader
- Texture
- Mesh
- Depth
- Distance

State Changeを削減する。

---

# 13. State Change

GPU State変更を可能な限り削減する。

特に:

- PSO
- Root Signature
- Descriptor Heap
- Descriptor Table
- Resource State

の変更コストを考慮する。

ただし、過剰なSortingによってCPU処理が増えすぎないようにする。

---

# 14. Render Pass

Rendering処理をRender Pass単位に分割する。

例:

```text
ShadowPass
DepthPass
GBufferPass
LightingPass
SkyPass
TransparentPass
PostProcessPass
UIPass
```

各Passは責務を明確にする。

---

# 15. Render Target

Render Targetの用途を明確にする。

例:

- Back Buffer
- Scene Color
- Depth
- GBuffer
- Velocity
- HDR Buffer
- Bloom Buffer
- Post Process Buffer

Render Target FormatとUsageを適切に管理する。

---

# 16. Resource State

DirectX 12 Resource State TransitionはRHI / Render Graph側で適切に管理する。

例えば:

```text
RENDER_TARGET
    ↓
PIXEL_SHADER_RESOURCE
```

Resource Stateを無視したRenderingは禁止する。

---

# 17. Render Graph

Rendering Architectureは将来的にRender Graphへ拡張可能な設計にする。

Render Graphは以下を管理可能とする。

- Render Pass
- Resource
- Dependency
- Resource Lifetime
- Resource State
- Barrier

Pass間のDependencyを明示する。

---

# 18. Render Graph Resource

Render Graph内で使用するTemporary Resourceは、必要なLifetimeだけ確保する。

例:

```text
Pass A
  ↓
Temporary Texture
  ↓
Pass B
  ↓
Release
```

不要なFrame-wide Resourceを増やさない。

---

# 19. Deferred Rendering

Deferred Renderingを使用する場合、GBufferを明確に定義する。

例:

```text
GBuffer0 = Base Color
GBuffer1 = Normal
GBuffer2 = World Position
GBuffer3 = Metallic / Roughness / AO
GBuffer4 = Emission
GBuffer5 = Velocity
```

FormatとPrecisionを必要以上に高くしない。

---

# 20. Forward Rendering

Forward Renderingも利用可能な設計にする。

用途例:

- Transparent
- Particle
- Special Effect
- UI
- Hair
- Volumetric Effect

DeferredとForwardの責務を明確にする。

---

# 21. HDR

Lighting計算は必要に応じてHDR Linear Spaceで行う。

基本:

```text
Texture
    ↓
Linear
    ↓
Lighting
    ↓
HDR
    ↓
Tone Mapping
    ↓
LDR
```

Gamma SpaceでLighting計算を行わない。

---

# 22. Color Space

Color Spaceを明確に管理する。

基本:

- Input Texture → sRGB適切に解釈
- Lighting → Linear
- HDR → Linear
- Tone Mapping → Linear HDRからLDR
- Output → Display Color Space

sRGB / Linear変換を二重に行わない。

---

# 23. PBR

PBR Renderingを基本Rendering Modelとして利用可能にする。

代表:

- Base Color
- Metallic
- Roughness
- Normal
- Ambient Occlusion
- Emission

Lighting ModelはShader Systemと連携する。

---

# 24. Image Based Lighting

IBLを利用可能な設計にする。

代表:

- Irradiance
- Prefiltered Environment
- BRDF LUT

Environment MapをRendering Systemから参照できるようにする。

---

# 25. Shadow

Shadow Renderingは独立したPassとして設計する。

必要に応じて:

- Directional Shadow
- Spot Shadow
- Point Shadow
- Cascaded Shadow Map

を実装可能にする。

---

# 26. Lighting

Lighting SystemではLight Typeを明確にする。

例:

- Directional Light
- Point Light
- Spot Light
- Area Light

Lighting DataをGPUへ効率的に転送する。

---

# 27. Post Processing

Post Processingは独立Passとして管理する。

代表:

- SSAO
- SSR
- Bloom
- DOF
- Motion Blur
- Fog
- Color Grading
- Tone Mapping
- FXAA

各EffectのInput / Outputを明確にする。

---

# 28. Post Processing Order

Post Processingの順番はEffectの依存関係に基づいて決定する。

例えば:

```text
Scene HDR
 ↓
SSAO / SSR
 ↓
Lighting
 ↓
Volumetric / Fog
 ↓
Bloom
 ↓
Tone Mapping
 ↓
Color Grading
 ↓
Anti Aliasing
 ↓
UI
```

ただし、実装するEffectの性質に応じて変更する。

---

# 29. Anti-Aliasing

AAはRendering Pipelineの最後付近で適切に適用する。

選択可能:

- MSAA
- FXAA
- TAA
- TSR
- DLSS
- XeSS
- FSR

Upscaler / Temporal AAを導入する場合はVelocity / Motion Vectorを適切に生成する。

---

# 30. Upscaling

FSR / DLSS / XeSSなどを導入する場合、Upscaling前に必要なDataを準備する。

代表:

- Motion Vector
- Depth
- Exposure
- Reactive Mask
- Transparency Data

Vendor API固有コードはRendererから分離する。

---

# 31. GPU Particles

GPU Particle SystemをRendering Pipelineへ統合できる設計にする。

必要に応じて:

- Compute Shader
- Structured Buffer
- UAV
- Indirect Draw

を利用する。

Particle UpdateとParticle Renderingを分離する。

---

# 32. Indirect Rendering

大量Objectを描画する場合はIndirect Renderingを検討する。

例:

```text
Culling
 ↓
GPU Command Generation
 ↓
Indirect Draw
```

必要性をProfilingしてから導入する。

---

# 33. Instancing

同一Mesh / Materialを大量描画する場合はInstancingを利用する。

例:

```text
Mesh
Material
    ↓
Instance Data[]
    ↓
Instanced Draw
```

---

# 34. GPU Driven Rendering

将来的にGPU Driven Renderingへ拡張可能な設計にする。

例:

```text
Instance Buffer
 ↓
GPU Culling
 ↓
LOD
 ↓
Indirect Arguments
 ↓
ExecuteIndirect
```

ただし、初期段階からGPU Driven Renderingを強制しない。

---

# 35. Descriptor

Descriptor管理はRHI / Descriptor Systemへ分離する。

RendererがDescriptor Heap内部実装を直接操作しない。

---

# 36. Material

MaterialはShader ParameterとTextureを管理する。

RendererからMaterialへ必要なGPU Dataを渡す。

Material SystemとShader Systemを密結合にしすぎない。

---

# 37. Camera

CameraはRenderingに必要な以下の情報を提供する。

- View
- Projection
- View Projection
- Position
- Near Plane
- Far Plane
- FOV
- Resolution
- Jitter

Temporal Renderingを使用する場合はPrevious Frame Matrixも管理する。

---

# 38. Frame Data

FrameごとのDataはFrame Contextへまとめる。

例:

```text
FrameContext
 ├─ Camera Data
 ├─ Lighting Data
 ├─ Render Statistics
 ├─ Temporary Resources
 └─ Command Context
```

Frame間で不必要なData共有をしない。

---

# 39. Triple Buffering

CPU / GPU Parallelismを考慮し、Frame Resourceを複数保持できる設計にする。

GPUが使用中のFrame DataをCPUが上書きしない。

---

# 40. Synchronization

GPU SynchronizationはFenceを使用する。

CPU MutexでGPU処理を同期しようとしない。

---

# 41. Performance

Rendering Performanceでは以下を計測可能にする。

- CPU Render Time
- GPU Frame Time
- Draw Calls
- Dispatch Calls
- Triangle Count
- Visible Objects
- Culled Objects
- PSO Changes
- Descriptor Changes
- Resource Barriers
- Render Pass Time
- VRAM Usage

---

# 42. Debug Rendering

Debug Build / Editorでは以下を表示できる設計を検討する。

- Wireframe
- Bounding Box
- Bounding Sphere
- Frustum
- Normals
- Tangents
- Light Bounds
- Shadow Map
- GBuffer
- Depth
- Motion Vector

---

# 43. Render Statistics

RendererはDebug用Statisticsを提供可能にする。

例:

```text
Draw Calls
Triangles
Visible Objects
Culled Objects
PSO Changes
GPU Time
CPU Time
```

---

# 44. Resize

Window Resize時には以下を正しく処理する。

- Swap Chain
- Back Buffer
- Depth Buffer
- Scene Render Target
- Post Process Buffer
- Resolution-dependent Resource

GPU使用中Resourceを安全にResizeする。

---

# 45. Device Lost

GPU Device Removed / Device Lostを考慮した設計にする。

必要に応じて:

```text
Device Lost
 ↓
Release GPU Resources
 ↓
Recreate Device
 ↓
Recreate GPU Resources
 ↓
Resume Rendering
```

---

# 46. Error Handling

Rendering Errorを無視しない。

D3D12 Error / HRESULTを必要に応じてEngine Errorへ変換する。

Debug BuildではD3D12 Debug Layer / GPU Validationを活用する。

---

# 47. 禁止事項

以下は禁止する。

- Rendererから直接ID3D12Deviceを操作
- Rendererから直接Descriptor Heap内部を操作
- GPU使用中Resourceの破棄
- Resource Stateを無視
- 毎FrameのPSO生成
- 毎FrameのShader Compile
- RendererへGameplay Logicを追加
- ECS RegistryをRenderer内部へ直接持ち込む
- 不要なDraw Call
- ProfilingなしのRendering最適化
- Render Pass間のDependencyを曖昧にする

---

# 48. 実装優先順位

Rendering機能は基本的に以下の順番で構築する。

1. Basic Forward Rendering
2. Camera
3. Mesh Rendering
4. Material
5. Texture
6. Depth
7. Lighting
8. Shadow
9. PBR
10. IBL
11. Deferred Rendering
12. Post Processing
13. Render Graph
14. GPU Culling
15. Indirect Rendering
16. Advanced Upscaling

機能追加時は既存Rendering Pipelineを壊さない。

---

# 49. 最終原則

Rendering Systemは、

「何を描画するか」

を決定するLayerであり、

「DirectX 12 APIをどう操作するか」

を決定するLayerではない。

Renderer / RHI / DirectX12 Backendの責務を明確に分離する。

性能最適化では、必ずProfiler / GPU Capture / Benchmarkの結果を根拠にする。