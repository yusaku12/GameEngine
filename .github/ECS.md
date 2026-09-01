---
applyTo: "Engine/ECS/**/*.cpp,Engine/ECS/**/*.h,Engine/ECS/**/*.hpp,Engine/Entity/**/*.cpp,Engine/Entity/**/*.h,Engine/Entity/**/*.hpp"
---

# ECS / Entity 実装指示書

# 1. ECSライブラリ

このEngineではECS実装に **EnTT** を使用する。

ECSをEngine独自に一から実装してはならない。

Entity Registry、Component Storage、View、Group、Signalなど、EnTTが提供している機能については、まずEnTTの既存機能を利用できないか確認する。

EnTT公式ドキュメントおよび既存プロジェクトの使用方法を優先する。

---

# 2. EnTTの位置付け

EnTTはECSの実装基盤として使用する。

ただし、Engine全体がEnTTへ直接依存する構造を避ける。

基本構造:

Gameplay / Engine Systems
↓
Engine ECS Abstraction
↓
EnTT
↓
Component Storage

可能な限りEnTT固有型をEngine全体へ漏らさない。

---

# 3. ECS Layer

Engine側では必要に応じてEnTTをラップする。

例:

Engine:

- Entity
- EntityManager
- Registry
- Component Access
- System

Backend:

- entt::entity
- entt::registry
- entt::basic_view
- entt::basic_storage

ただし、単純なWrapperを大量に作らない。

EnTT APIを隠すこと自体を目的にしない。

---

# 4. Entity

Entityは軽量なIdentifierとして扱う。

Entity自身へ大量のDataを保持させない。

基本的には:

Entity
↓
Component
↓
System

という構造にする。

---

# 5. EnTT Registry

EnTT RegistryをComponent / Entity管理の中心として利用する。

Engine側で独自Registryを一から実装しない。

RegistryのLifetimeを明確にする。

Scene単位でRegistryを持つ場合はScene LifetimeとRegistry Lifetimeを一致させる。

---

# 6. Component

Componentは基本的にData-orientedに設計する。

例:

- TransformComponent
- MeshComponent
- MaterialComponent
- CameraComponent
- LightComponent
- RigidBodyComponent
- ColliderComponent
- AnimationComponent
- AudioComponent
- ParticleComponent

Componentへ巨大なManagerやSubsystemを持たせない。

---

# 7. Component設計

Componentは可能な限り:

- 小さい
- 明確
- 独立
- Serialize可能
- Cache Friendly

な構造にする。

不要なPointerやshared_ptrを大量にComponentへ持たせない。

---

# 8. System

SystemはComponentを処理する。

例:

- TransformSystem
- AnimationSystem
- PhysicsSystem
- RenderPreparationSystem
- AudioSystem
- ParticleSystem
- LifetimeSystem

Systemへ無関係な責務を追加しない。

---

# 9. View

Componentを検索するときはEnTTのViewを優先する。

例:

Registry
↓
View<TransformComponent, MeshComponent>
↓
Iterate

独自Component検索システムを作る前にEnTTのView / Groupが利用できないか確認する。

---

# 10. Group

頻繁に一緒に処理するComponentについてはEnTT Groupの利用を検討する。

ただし、Groupを無条件に使用しない。

Data Layoutと更新頻度を考慮する。

---

# 11. System Dependency

System間の依存関係を明確にする。

例:

Input
↓
Gameplay
↓
Animation
↓
Physics
↓
Transform
↓
Render Preparation
↓
Renderer

実際のプロジェクト構造に合わせてDependencyを定義する。

Circular Dependencyを作らない。

---

# 12. Rendering

RendererがECS Registryへ深く依存しないようにする。

推奨:

ECS
↓
Render Preparation System
↓
Render Data
↓
Renderer
↓
RHI
↓
DirectX 12

Rendererが毎回Entity Registryを直接走査する構造を基本にしない。

---

# 13. Render Data

Rendererへ渡すDataは可能な限りRendering専用Dataへ変換する。

例:

ECS:

TransformComponent
MeshComponent
MaterialComponent

↓

Render Object

↓

Renderer

これによってRendererとECSを疎結合にする。

---

# 14. Physics

Physics SystemはECS ComponentとPhysics Backendの橋渡しを担当する。

例:

RigidBodyComponent
↓
Physics System
↓
Physics Backend

Physics Engine固有型をComponentへ大量に露出させない。

---

# 15. Animation

Animation Systemは:

- AnimationComponent
- Skeleton
- Animation Clip
- Transform

などを処理する。

Animation Evaluationは将来的にJob Systemで並列化可能な構造にする。

---

# 16. Lifetime

EntityのLifetimeとComponentのLifetimeを正しく管理する。

Entity Destroy時に関連Componentが安全に破棄されることを保証する。

Systemが破棄済みEntityを参照しないようにする。

---

# 17. Entity Handle

Entityを長期間保持する場合、Entity IDだけを無条件に信頼しない。

Stale Entityを検出できる仕組みを検討する。

EnTTが提供するEntity Versioningの仕組みを必要に応じて利用する。

---

# 18. Scene

SceneはECS Registryの管理単位として利用可能とする。

例:

Scene
├── Registry
├── Systems
├── Scene Resources
└── Scene State

Scene切り替え時には旧Registryへの参照が残らないようにする。

---

# 19. Serialization

ECS Entity / ComponentをSerializeする場合、EnTT内部実装へ直接依存した形式を基本フォーマットにしない。

Engine独自のSerialization形式を定義する。

例:

Entity ID
Component Type
Component Data

---

# 20. Editor

EditorはECSを利用してSceneを編集できるようにする。

例:

Hierarchy
↓
Entity
↓
Components
↓
Inspector

EditorがEnTT内部Storageへ直接アクセスしすぎないようにする。

---

# 21. Threading

ECS Systemは将来的に並列実行可能な設計にする。

Read / Write Accessを明確にする。

例:

Transform Read
Mesh Read
Material Read

↓

Parallel Render Preparation

同一Componentへの同時Writeは適切に同期する。

---

# 22. Performance

ECSのPerformanceを意識する。

重要な要素:

- Cache Locality
- Component Size
- Iteration
- View
- Group
- Allocation
- Memory Layout
- Branching

ただし、早期最適化は禁止する。

---

# 23. EnTT APIの扱い

EnTTに既に存在する機能を独自実装しない。

例えばEntity / Component操作について、まず以下を検討する。

- entt::registry
- entt::entity
- registry.emplace
- registry.emplace_or_replace
- registry.remove
- registry.destroy
- registry.get
- registry.try_get
- registry.all_of
- registry.any_of
- registry.view
- registry.group

必要な機能がEnTTに存在する場合は、それを優先する。

---

# 24. EnTT Version

EnTTのVersionはProject Dependencyとして明確に管理する。

依存Versionを無断で変更しない。

EnTT APIを利用する場合は、現在Projectで使用しているVersionに対応したAPIを使用する。

---

# 25. EnTT Dependency

EnTT依存をEngineのすべてのLayerへ広げない。

可能な限りECS Layerへ閉じ込める。

Gameplay / Renderer / Resourceなどが直接EnTTへ依存する必要があるか検討する。

---

# 26. ECS独自実装禁止

以下を独自実装してはならない。

- Entity Registry
- Archetype Storage
- Sparse Set
- Component Pool
- Entity Generation System
- View System

EnTTで提供される機能を利用する。

Engine独自の機能が必要な場合は、EnTTの上位Layerとして実装する。

---

# 27. 禁止事項

以下は禁止する。

- ECSを自作する
- EnTTと独自ECSを二重実装する
- RendererからEnTT Registryへ深く依存する
- Componentへ巨大なSubsystemを格納する
- Componentに無関係なGameplay Logicを詰め込む
- Entityへ大量のStateを直接格納する
- Stale Entityを無検証で使用する
- ECS内部Storageへ直接依存する
- EnTT Versionを無断変更する
- EnTTに存在する機能を再実装する

---

# 28. 新しいComponentを追加する場合

以下を確認する。

1. Componentの責務
2. Data構造
3. Size
4. Alignment
5. Ownership
6. Serialization
7. Thread Safety
8. System Dependency
9. Rendering / Physicsへの影響
10. EnTTで適切に管理できるか

---

# 29. 新しいSystemを追加する場合

以下を確認する。

1. Input Component
2. Output Component
3. Read Access
4. Write Access
5. System Dependency
6. Thread Safety
7. Job化可能か
8. Scene Lifetime
9. Performance

---

# 30. 最終原則

このEngineのECSは、

「EnTTを使っているゲーム」

ではなく、

「EngineのECS Architectureを実現するための基盤としてEnTTを利用している」

という位置付けにする。

EnTTの性能と機能を活用しながら、Engine側のArchitectureは明確に保つ。

EnTTを変更する必要がある場合は、原則としてEnTT自体を改造するのではなく、Engine側のAdapter / Wrapper / Extensionで解決する。