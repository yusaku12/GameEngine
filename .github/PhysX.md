\# PhysX 5 自作ゲームエンジン統合仕様書



\## 1. 目的



既存の自作C++ゲームエンジンへ NVIDIA PhysX 5 を統合し、UnityのPhysics機能に近い使いやすさを持つ物理システムを構築する。



PhysXそのものをゲームコードから直接操作させるのではなく、



```text

GameObject

&#x20;   ↓

Physics Components

&#x20;   ↓

PhysicsSystem

&#x20;   ↓

PhysX 5

```



という構造にする。



ゲーム側はPhysX固有の型への依存を最小限にし、自作エンジン独自のAPIから物理機能を利用できるようにする。



\---



\# 2. 基本方針



\## 2.1 PhysX 5を使用する



PhysXは最新版のPhysX 5系を使用する。



PhysX 5 SDKをプロジェクトへ導入し、Windows + Visual Studio + C++20環境でビルド可能にする。



公式PhysXリポジトリ：



https://github.com/NVIDIA-Omniverse/PhysX



PhysX SDKのビルド方法は公式リポジトリの手順を優先する。



\---



\# 3. 既存エンジンとの統合方針



既存エンジンは以下の思想を維持する。



\* C++20

\* DirectX 12

\* Win32

\* 自作GameObject

\* Componentベース

\* ECSを導入しない

\* Singletonの乱用を避ける

\* RAII

\* smart pointer

\* const correctness

\* Doxygenコメント

\* defensive programming

\* nullptrチェック

\* assert

\* エラーログ

\* PhysX型を可能な限りEngine内部へ閉じ込める



PhysX APIをGameObjectやGameplayコードへ直接ばら撒かない。



悪い例：



```cpp

PxRigidDynamic\* rigidBody;

rigidBody->addForce(...);

```



ゲームコードではなく、以下のようなEngine APIを使用する。



```cpp

auto\* rigidBody = gameObject->getComponent<RigidbodyComponent>();



rigidBody->addForce(force);

```



\---



\# 4. 推奨ディレクトリ構成



以下のような構成を作成する。



```text

Engine/

├── Physics/

│   ├── PhysicsSystem.h

│   ├── PhysicsSystem.cpp

│   │

│   ├── PhysicsWorld.h

│   ├── PhysicsWorld.cpp

│   │

│   ├── PhysicsMaterial.h

│   ├── PhysicsMaterial.cpp

│   │

│   ├── PhysicsFilter.h

│   ├── PhysicsFilter.cpp

│   │

│   ├── PhysicsTypes.h

│   │

│   ├── Rigidbody/

│   │   ├── RigidbodyComponent.h

│   │   └── RigidbodyComponent.cpp

│   │

│   ├── Collider/

│   │   ├── ColliderComponent.h

│   │   ├── ColliderComponent.cpp

│   │   ├── BoxColliderComponent.h

│   │   ├── SphereColliderComponent.h

│   │   ├── CapsuleColliderComponent.h

│   │   ├── MeshColliderComponent.h

│   │   └── ConvexColliderComponent.h

│   │

│   ├── Character/

│   │   ├── CharacterController.h

│   │   └── CharacterController.cpp

│   │

│   ├── Joint/

│   │   ├── FixedJointComponent.h

│   │   ├── RevoluteJointComponent.h

│   │   ├── DistanceJointComponent.h

│   │   └── D6JointComponent.h

│   │

│   ├── Query/

│   │   ├── PhysicsQuery.h

│   │   ├── Raycast.h

│   │   ├── Sweep.h

│   │   └── Overlap.h

│   │

│   ├── Event/

│   │   ├── PhysicsEvent.h

│   │   └── PhysicsEventListener.h

│   │

│   └── Internal/

│       ├── PhysXFoundation.h

│       ├── PhysXFoundation.cpp

│       ├── PhysXWorld.h

│       ├── PhysXWorld.cpp

│       ├── PhysXAllocator.h

│       ├── PhysXErrorCallback.h

│       └── PhysXConversions.h

│

└── ...

```



PhysX SDKのヘッダを必要以上にEngine全体へ公開しない。



\---



\# 5. PhysXの初期化



PhysXの初期化処理を `PhysicsSystem` に集約する。



基本的なライフサイクルは以下とする。



```text

PhysicsSystem::initialize()

&#x20;       ↓

PxFoundation

&#x20;       ↓

PxPhysics

&#x20;       ↓

PxCooking

&#x20;       ↓

PxDefaultCpuDispatcher

&#x20;       ↓

PxScene

&#x20;       ↓

PhysicsSystem::update()

&#x20;       ↓

PhysicsSystem::shutdown()

```



必要に応じてPVDなどのデバッグ機能も接続できる設計にする。



\---



\# 6. PhysicsSystem



PhysicsSystemはEngineの物理システム全体を管理する。



例：



```cpp

class PhysicsSystem

{

public:

&#x20;   bool initialize();

&#x20;   void shutdown();



&#x20;   void update(float deltaTime);



&#x20;   PhysicsWorld\* getWorld();



&#x20;   bool isInitialized() const noexcept;

};

```



責務：



\* PhysX初期化

\* PhysX終了処理

\* PhysicsWorld管理

\* Simulation実行

\* Scene Query

\* Physics Event処理

\* PhysXエラー処理

\* Physics Debug機能

\* Physics timestep管理



\---



\# 7. PhysicsWorld



PhysXの `PxScene` をEngine側から抽象化する。



```cpp

class PhysicsWorld

{

public:

&#x20;   bool initialize();

&#x20;   void shutdown();



&#x20;   void simulate(float deltaTime);

&#x20;   void fetchResults();



&#x20;   RaycastHit raycast(...);

&#x20;   std::vector<RaycastHit> raycastAll(...);



&#x20;   bool overlap(...);

&#x20;   bool sweep(...);

};

```



PhysXの



```cpp

PxScene

```



をEngineコードから直接操作させない。



\---



\# 8. Fixed Timestep



PhysicsはRenderingのFPSとは独立したFixed Timestepで実行する。



例えば、



```text

Render FPS

60～240 FPS



Physics

60 Hz

```



のようにする。



Accumulator方式を採用する。



```cpp

accumulator += deltaTime;



while (accumulator >= fixedDeltaTime)

{

&#x20;   physicsSystem.update(fixedDeltaTime);

&#x20;   accumulator -= fixedDeltaTime;

}

```



デフォルト値：



```cpp

fixedDeltaTime = 1.0f / 60.0f;

```



ただし設定可能にする。



Physics処理をRendering FPSへ直接依存させない。



\---



\# 9. GameObjectとの統合



自作EngineのGameObjectへComponentとしてPhysics機能を追加する。



基本構成：



```text

GameObject

├── TransformComponent

├── RigidbodyComponent

└── ColliderComponent

```



例えば、



```cpp

auto player = scene->createGameObject("Player");



player->addComponent<TransformComponent>();

player->addComponent<RigidbodyComponent>();

player->addComponent<CapsuleColliderComponent>();

```



という形で使用できるようにする。



\---



\# 10. RigidbodyComponent



UnityのRigidbodyに近いAPIを提供する。



```cpp

class RigidbodyComponent final : public Component

{

public:

&#x20;   void setMass(float mass);

&#x20;   float getMass() const;



&#x20;   void setLinearVelocity(const Vector3\& velocity);

&#x20;   Vector3 getLinearVelocity() const;



&#x20;   void setAngularVelocity(const Vector3\& velocity);

&#x20;   Vector3 getAngularVelocity() const;



&#x20;   void addForce(const Vector3\& force);

&#x20;   void addImpulse(const Vector3\& impulse);



&#x20;   void addTorque(const Vector3\& torque);



&#x20;   void setKinematic(bool enabled);

&#x20;   bool isKinematic() const;



&#x20;   void setGravityEnabled(bool enabled);

&#x20;   bool isGravityEnabled() const;



&#x20;   void setLinearDamping(float damping);

&#x20;   void setAngularDamping(float damping);



&#x20;   void wakeUp();

&#x20;   void sleep();

};

```



内部ではPhysXの



```cpp

PxRigidDynamic

```



等を利用する。



ゲーム側にはPhysX型を返さない。



\---



\# 11. RigidbodyのTransform同期



PhysicsとTransformの同期方向を明確にする。



\## Dynamic Rigidbody



```text

PhysX

&#x20;↓

TransformComponent

&#x20;↓

Rendering

```



PhysicsがTransformを決定する。



\## Kinematic Rigidbody



```text

TransformComponent

&#x20;↓

PhysX

```



Game側からTransformを変更する。



このルールを厳守する。



Dynamic Rigidbodyに対して毎フレームTransformを直接書き換えるような設計は禁止する。



\---



\# 12. Collider Component



以下のColliderを実装する。



```text

BoxCollider

SphereCollider

CapsuleCollider

ConvexCollider

MeshCollider

```



それぞれPhysXのShapeへ対応させる。



```text

BoxCollider

&#x20;   ↓

PxBoxGeometry



SphereCollider

&#x20;   ↓

PxSphereGeometry



CapsuleCollider

&#x20;   ↓

PxCapsuleGeometry



ConvexCollider

&#x20;   ↓

PxConvexMeshGeometry



MeshCollider

&#x20;   ↓

PxTriangleMeshGeometry

```



\---



\# 13. Colliderの責務



Colliderは以下を管理する。



```cpp

class ColliderComponent : public Component

{

public:

&#x20;   void setMaterial(...);



&#x20;   void setIsTrigger(bool enabled);

&#x20;   bool isTrigger() const;



&#x20;   void setLayer(uint32\_t layer);

&#x20;   uint32\_t getLayer() const;



&#x20;   void setCollisionMask(uint32\_t mask);

&#x20;   uint32\_t getCollisionMask() const;

};

```



ColliderとRigidbodyを分離する。



\---



\# 14. Physics Material



PhysX MaterialをEngine側でラップする。



```cpp

class PhysicsMaterial

{

public:

&#x20;   void setStaticFriction(float value);

&#x20;   void setDynamicFriction(float value);

&#x20;   void setRestitution(float value);

};

```



例えば：



```text

Metal

Wood

Stone

Ice

Rubber

Default

```



などのMaterialをAssetとして管理できるようにする。



\---



\# 15. Collision Layer / Filter



Collision LayerをEngine側で実装する。



例えば：



```cpp

enum class PhysicsLayer : uint32\_t

{

&#x20;   Default = 0,

&#x20;   Player,

&#x20;   Enemy,

&#x20;   Environment,

&#x20;   Projectile,

&#x20;   Trigger,

};

```



Layer同士の衝突判定をBitMaskで管理する。



例：



```text

Player

&#x20;   ↓

Enemy

Environment

Projectile



Trigger

&#x20;   ↓

Player

Enemy

```



PhysXのFilterDataへ変換する。



\---



\# 16. Trigger



Unityの



```text

OnTriggerEnter

OnTriggerStay

OnTriggerExit

```



に近い機能をEngine側で提供する。



例：



```cpp

class PhysicsEventListener

{

public:

&#x20;   virtual void onTriggerEnter(

&#x20;       GameObject\& other) {}



&#x20;   virtual void onTriggerExit(

&#x20;       GameObject\& other) {}

};

```



GameplayコードがPhysXのcallback interfaceへ直接依存しないようにする。



\---



\# 17. Collision Event



Collisionについてもイベントを提供する。



```cpp

virtual void onCollisionEnter(

&#x20;   const CollisionInfo\& collision);



virtual void onCollisionStay(

&#x20;   const CollisionInfo\& collision);



virtual void onCollisionExit(

&#x20;   const CollisionInfo\& collision);

```



`CollisionInfo` はEngine独自型にする。



例えば：



```cpp

struct CollisionInfo

{

&#x20;   GameObjectHandle other;



&#x20;   Vector3 point;

&#x20;   Vector3 normal;



&#x20;   float impulse;

};

```



\---



\# 18. Physics Query



PhysXのScene QueryをEngine APIとして提供する。



PhysXはRaycast、Overlap、SweepなどのScene Queryを提供しているため、自作Engineでも統一APIとして扱えるようにする。



\## Raycast



```cpp

RaycastHit PhysicsWorld::raycast(

&#x20;   const Vector3\& origin,

&#x20;   const Vector3\& direction,

&#x20;   float maxDistance,

&#x20;   PhysicsLayerMask layerMask);

```



\## Raycast All



```cpp

std::vector<RaycastHit> PhysicsWorld::raycastAll(...);

```



\## Sphere Cast



```cpp

SweepHit PhysicsWorld::sphereCast(...);

```



\## Capsule Cast



```cpp

SweepHit PhysicsWorld::capsuleCast(...);

```



\## Overlap



```cpp

std::vector<GameObjectHandle>

PhysicsWorld::overlapSphere(...);

```



\---



\# 19. RaycastHit



PhysX型をそのまま返さない。



Engine独自の構造体を作る。



```cpp

struct RaycastHit

{

&#x20;   GameObjectHandle gameObject;



&#x20;   Vector3 point;

&#x20;   Vector3 normal;



&#x20;   float distance;



&#x20;   ColliderComponent\* collider;

};

```



\---



\# 20. Character Controller



キャラクター用にPhysX Character ControllerをEngine側でラップする。



```cpp

class CharacterControllerComponent final

&#x20;   : public Component

{

public:

&#x20;   void move(

&#x20;       const Vector3\& displacement,

&#x20;       float deltaTime);



&#x20;   void setRadius(float radius);

&#x20;   void setHeight(float height);



&#x20;   bool isGrounded() const;



&#x20;   Vector3 getPosition() const;

};

```



プレイヤーキャラクターなどはこちらを使用できるようにする。



\---



\# 21. Joint



将来的な拡張としてJointシステムを設計する。



最低限、



```text

Fixed Joint

Revolute Joint

Distance Joint

D6 Joint

```



をサポートできる構造にする。



```cpp

class JointComponent : public Component

{

protected:

&#x20;   RigidbodyComponent\* m\_bodyA;

&#x20;   RigidbodyComponent\* m\_bodyB;

};

```



\---



\# 22. Mesh Collider



Mesh ColliderはRuntimeで毎回Cookしない。



Asset PipelineにPhysics Cooking処理を用意する。



```text

FBX / OBJ / GLTF

&#x20;      ↓

ModelImporter

&#x20;      ↓

Physics Mesh Cooking

&#x20;      ↓

Cooked Physics Asset

&#x20;      ↓

Runtime

&#x20;      ↓

PxTriangleMesh / PxConvexMesh

```



Physics用Meshを通常のRendering Meshと分離して管理できるようにする。



\---



\# 23. Convex Mesh



Dynamic Rigidbodyでは、基本的に適切なConvex Colliderを使用する。



大量のTriangle MeshをDynamic Rigidbodyへ直接使用する設計は避ける。



例えば、



```text

Player

&#x20;   ↓

Capsule



Box

&#x20;   ↓

BoxCollider



Dynamic debris

&#x20;   ↓

ConvexCollider



Static level

&#x20;   ↓

MeshCollider

```



という設計を基本とする。



\---



\# 24. Physics Asset



将来的にPhysics専用Asset形式を追加できる設計にする。



例：



```text

Assets/

├── Models/

│   └── Player.fbx

│

└── Physics/

&#x20;   └── Player.physics

```



Physics Assetには、



```text

Collider

Material

Mass

Center Of Mass

Inertia

Collision Layer

Collision Mask

```



などを保存できるようにする。



\---



\# 25. Transformとの座標変換



EngineのMath型とPhysXの型を変換する専用クラスを作成する。



```cpp

namespace PhysicsConversions

{

&#x20;   physx::PxVec3 toPx(const Vector3\& value);



&#x20;   Vector3 fromPx(const physx::PxVec3\& value);



&#x20;   physx::PxQuat toPx(const Quaternion\& value);



&#x20;   Quaternion fromPx(const physx::PxQuat\& value);

}

```



変換処理をEngine全体へ散らさない。



\---



\# 26. Memory管理



PhysX独自Allocatorを使用する場合はEngine側のMemory Systemとの統合を検討する。



PhysXオブジェクトを勝手に



```cpp

new

delete

```



する設計にしない。



PhysXのライフタイムルールに従う。



\---



\# 27. Threading



Physics SimulationはRender Threadと責務を分離する。



基本構造：



```text

Main Thread

&#x20;   │

&#x20;   ├── Gameplay

&#x20;   │

&#x20;   ├── Transform

&#x20;   │

&#x20;   └── Physics Commands

&#x20;            │

&#x20;            ▼

&#x20;       Physics System

&#x20;            │

&#x20;            ▼

&#x20;         PhysX

&#x20;            │

&#x20;            ▼

&#x20;      Physics Results

&#x20;            │

&#x20;            ▼

&#x20;       Main Thread

```



PhysXのSimulation中にGameObjectやComponentを破壊するなど、不正な同時アクセスを発生させない。



Physics Callbackから直接GameObjectを破壊する設計は禁止する。



イベントをQueueへ入れ、Simulation終了後に処理する。



\---



\# 28. Physics Event Queue



Physics Callbackでは、



```text

CollisionEvent

TriggerEvent

```



をQueueへ保存する。



その後、



```cpp

PhysicsSystem::dispatchEvents();

```



でGameplayへ通知する。



これによりPhysX CallbackとGameObject Lifecycleを分離する。



\---



\# 29. Physics Debug Rendering



ImGui + DirectX 12の既存DebugPrimitiveシステムと連携する。



Debug表示：



```text

Collider AABB

Box Collider

Sphere Collider

Capsule Collider

Convex Collider

Raycast

Contact Point

Contact Normal

Velocity

Center Of Mass

```



などを表示できるようにする。



ImGuiにPhysics Debug Windowを追加する。



例：



```text

Physics Debug



\[ ] Enable Debug Draw

\[ ] Draw Colliders

\[ ] Draw Contacts

\[ ] Draw AABB

\[ ] Draw Velocity

\[ ] Draw Raycasts



Simulation

Fixed Delta Time : 0.016666

Active Bodies    : 128

Active Shapes    : 245

```



\---



\# 30. ImGui Physics Inspector



既存InspectorへPhysics Componentの情報を追加する。

Inspectorの「Add Component」メニューから、Rigidbody、Box Collider、Sphere Collider、
Capsule Collider、Mesh Collider、Convex Collider、Character Controller、Jointを追加できる。
同じ種類の重複追加は無効化する。JointにはRigidbodyが必要であり、Mesh Colliderと
Rigidbodyは併用できない。無効な項目にカーソルを合わせると理由を表示する。
Mesh ColliderとConvex Colliderは追加後に「Physics Asset」へ調理済みの.physicsファイルの
パスを入力して「Load」を押す。アセット未設定の間は物理形状を生成しない。

追加メニューはComponentRegistryに登録された型から表示名順に自動生成する。
新しいコンポーネントは起動時に`registerType<T>("表示名", false, false, true)`で登録すれば
メニューに表示され、EditorUiへの追記は不要。ファイルやクラスの作成だけでは登録されない。
必須コンポーネントは表示しない。複数追加の可否は登録時の`allowMultiple`に従う。
追加条件が必要な場合は、登録時の第5引数へ
`std::string_view(const GameObject&)`形式の関数を渡す。
追加可能なら空文字列、追加不可なら理由を返す。理由はメニューのツールチップに表示される。
この条件はエディターの追加メニュー専用で、既存の実行時追加処理には影響しない。

Character ControllerのHeightは半球を含む全高であり、Radiusの2倍より大きい必要がある。
Inspectorではドラッグと直接入力の両方を有効な寸法範囲に制限する。
APIから不正な寸法を指定した場合は、エラーを記録して元の値を維持する。

Kinematic RigidbodyではWake Up／Sleepを無効化する。APIからの起床・休眠も拒否する。
PhysicsのResetはKinematic Rigidbodyの姿勢と保存済み速度を初期化するが、
PhysXの速度設定・力のクリア・起床は動的Rigidbodyだけに適用する。


Rigidbody：



```text

Rigidbody



Mass

Linear Velocity

Angular Velocity

Linear Damping

Angular Damping

Gravity

Kinematic

```



Collider：



```text

Collider



Shape

Material

Is Trigger

Layer

Mask

```



を編集可能にする。



\---



\# 31. Scene Save / Load



Scene保存時にPhysics Componentの設定も保存できるようにする。



例：



```text

GameObject

&#x20;├── Transform

&#x20;├── Rigidbody

&#x20;│    ├── Mass

&#x20;│    ├── Gravity

&#x20;│    └── Kinematic

&#x20;│

&#x20;└── Collider

&#x20;     ├── Shape

&#x20;     ├── Size

&#x20;     ├── Material

&#x20;     └── Trigger

```



ただしPhysXのRuntimeオブジェクトそのものをSceneファイルへ保存しない。



保存するのはEngine側の設定値のみとする。



\---



\# 32. Physics WorldとSceneの関係



SceneManagerとPhysicsWorldを同期させる。



```text

SceneManager

&#x20;    │

&#x20;    ├── GameObjects

&#x20;    │

&#x20;    └── PhysicsWorld

&#x20;            │

&#x20;            └── PhysX Scene

```



Sceneロード時：



```text

Scene Load

&#x20;↓

GameObject生成

&#x20;↓

Component生成

&#x20;↓

Physics Component Initialize

&#x20;↓

PhysX Actor生成

&#x20;↓

Physics Sceneへ登録

```



Scene破棄時は逆順に安全に破棄する。



\---



\# 33. Component Lifecycle



Physics Componentには以下のLifecycleを考慮する。



```cpp

onCreate()

onEnable()

onDisable()

onDestroy()

```



特に、



```text

GameObject生成

&#x20;↓

RigidbodyComponent生成

&#x20;↓

ColliderComponent生成

&#x20;↓

PhysX Actor生成

```



という順序を保証する。



\---



\# 34. RigidbodyとColliderの関連付け



基本的な構造：



```text

GameObject

│

├── RigidbodyComponent

│       │

│       └── PxRigidDynamic

│

└── ColliderComponent

&#x20;       │

&#x20;       └── PxShape

```



Collider追加時に適切なActorへShapeをAttachする。



RigidbodyなしColliderはStatic Actorとして扱える設計を検討する。



\---



\# 35. Static Collider



Rigidbodyが存在しないColliderはStatic Physics Objectとして扱う。



例：



```text

Level

&#x20;├── Floor

&#x20;├── Wall

&#x20;├── Building

&#x20;└── Terrain

```



これらは基本的にStatic ActorとしてPhysX Sceneへ登録する。



\---



\# 36. Dynamic Rigidbody



Dynamic RigidbodyはPhysX Simulationによって移動する。



```text

Gravity

Force

Collision

Impulse

Joint

```



などの結果をPhysics Simulationから取得し、



```text

PxRigidDynamic

&#x20;     ↓

TransformComponent

&#x20;     ↓

Renderer

```



へ反映する。



\---



\# 37. Kinematic Rigidbody



Kinematic RigidbodyはGame側から移動させる。



```text

TransformComponent

&#x20;     ↓

PhysicsSystem

&#x20;     ↓

PhysX Kinematic Actor

```



Dynamic Objectとの衝突を正しく処理できる設計にする。



\---



\# 38. Physics APIの設計思想



ゲームコードからPhysX APIを直接呼ばない。



悪い：



```cpp

gameObject->getComponent<RigidbodyComponent>()

&#x20;   ->getPxActor()

&#x20;   ->addForce(...);

```



禁止。



良い：



```cpp

gameObject->getComponent<RigidbodyComponent>()

&#x20;   ->addForce(force);

```



PhysXの型を返すGetterをPublic APIへ追加しない。



必要な場合はPhysics内部専用APIに限定する。



\---



\# 39. PhysX依存の隔離



以下のようなEngine内部ファイルだけがPhysX APIへ強く依存する。



```text

Physics/Internal/

```



GameObjectやGameplayなどの高レベルコードでは、



```cpp

physx::Px\*

```



を極力使用しない。



これにより将来的にPhysics Backendを変更できる。



```text

PhysicsSystem

&#x20;    │

&#x20;    ▼

IPhysicsBackend

&#x20;    │

&#x20;    ├── PhysXBackend

&#x20;    │

&#x20;    └── FutureBackend

```



という拡張も可能にする。



\---



\# 40. Physics Backend



将来的な拡張を考慮し、



```cpp

class IPhysicsBackend

{

public:

&#x20;   virtual \~IPhysicsBackend() = default;



&#x20;   virtual bool initialize() = 0;

&#x20;   virtual void shutdown() = 0;



&#x20;   virtual void simulate(float deltaTime) = 0;

};

```



というBackend抽象化を検討する。



ただし初期実装では過剰な抽象化を避け、PhysX 5を直接Backendとして実装してよい。



\---



\# 41. CMake統合



PhysXをEngineのExternal Dependencyとして管理する。



推奨構造：



```text

GameEngine/

├── Engine/

├── Editor/

├── Game/

├── Assets/

├── External/

│   └── PhysX/

├── Tools/

└── CMakeLists.txt

```



PhysXのビルド成果物をEngineへリンクする。



Debug / ReleaseのRuntime Library設定を既存Engineと一致させる。



x64 / Windows環境を前提として構築する。



\---



\# 42. PhysX Build



PhysX SDK自身のビルド設定を勝手に変更しない。



まず公式PhysX SDKを正常にBuildできることを確認する。



その後、



```text

PhysX Build

&#x20;   ↓

Engine Link

&#x20;   ↓

PhysicsSystem

&#x20;   ↓

ゲーム側のScene

```



の順番で統合する。



PhysX SDKのビルド失敗とEngine側のリンク失敗を混同しない。



\---



\# 43. 初期実装の優先順位



一度にすべてのPhysX機能を実装しない。



以下の順番で実装する。



\## Phase 1



```text

PhysX Initialization

PhysicsWorld

PhysicsSystem

Fixed Timestep

```



\## Phase 2



```text

Static Collider

Box Collider

Sphere Collider

Capsule Collider

```



\## Phase 3



```text

Rigidbody

Dynamic Body

Kinematic Body

Gravity

Force

Impulse

```



\## Phase 4



```text

Collision

Trigger

Physics Event

```



\## Phase 5



```text

Raycast

Sweep

Overlap

```



\## Phase 6



```text

Physics Material

Collision Layer

Collision Mask

```



\## Phase 7



```text

Character Controller

```



\## Phase 8



```text

Joint

```



\## Phase 9



```text

Mesh Collider

Convex Cooking

Physics Asset

```



\## Phase 10



```text

Physics Debug Rendering

ImGui Physics Inspector

Physics Profiler

```



\---



\# 44. 最初の動作確認



エンジン独自のテストコードとPhysicsテストシーン生成機能は削除済み。
Physicsの動作確認は、通常のSceneにInspectorからコンポーネントを追加して行う。
以下は手動で構成するSceneの例であり、自動生成機能ではない。



```text

通常のScene



Ground

&#x20;   Static Box Collider



Box01

&#x20;   Dynamic Rigidbody

&#x20;   Box Collider



Box02

&#x20;   Dynamic Rigidbody

&#x20;   Box Collider



Sphere01

&#x20;   Dynamic Rigidbody

&#x20;   Sphere Collider



Capsule01

&#x20;   Dynamic Rigidbody

&#x20;   Capsule Collider

```



Simulation開始：



```text

Box

&#x20;↓

Gravity

&#x20;↓

Ground

&#x20;↓

Collision

&#x20;↓

Rest

```



まで正常に動作することを確認する。



\---



\# 45. Raycastテスト



通常のSceneでPhysics DebugパネルからRaycastを実行する。



```text

Camera

&#x20;  │

&#x20;  │ Ray

&#x20;  ▼

Collider

```



HitしたGameObject、Hit Position、Normal、DistanceをImGuiへ表示する。



\---



\# 46. Collisionテスト



2つのDynamic Rigidbodyを落下させ、



```text

Collision Enter

Collision Stay

Collision Exit

```



が正しく発生することを確認する。



\---



\# 47. Transform同期テスト



Physics Simulation後に、



```text

PxRigidDynamic Transform

```



と



```text

TransformComponent

```



が一致していることを確認する。



特に、



```text

Position

Rotation

```



の両方を確認する。



\---



\# 48. エラー処理



PhysXのErrorCallbackをEngineのLoggerへ接続する。



例えば：



```text

PhysX Error

PhysX Warning

PhysX Info

```



をEngine Loggerへ変換する。



`printf` や `std::cout` をPhysics Debugの主要出力として使用しない。



既存EngineのLoggerを使用する。



\---



\# 49. Null / Invalidチェック



以下を必ずチェックする。



```text

PhysicsSystem未初期化

PhysicsWorld未初期化

Invalid GameObject

Invalid Component

nullptr Actor

nullptr Shape

Invalid Material

Invalid Collider

Invalid Physics Handle

```



PhysX API呼び出し前に必要な状態を検証する。



\---



\# 50. Handle設計



Physics Objectへの参照には必要に応じてEngine側のHandleを使用する。



例：



```cpp

using PhysicsActorHandle = uint32\_t;

```



GameObjectのLifetimeとPhysX ActorのLifetimeを直接同一視しない。



\---



\# 51. Resource Lifetime



以下の破棄順序を厳守する。



```text

Physics Components

&#x20;       ↓

PhysX Actors

&#x20;       ↓

PxScene

&#x20;       ↓

PxDispatcher

&#x20;       ↓

PxCooking

&#x20;       ↓

PxPhysics

&#x20;       ↓

PxFoundation

```



PhysX Sceneが存在する状態でFoundationを破棄するなどのLifecycle違反を絶対に起こさない。



\---



\# 52. Thread Safety



Physics Simulation中に以下を直接変更しない。



```text

GameObject hierarchy

Component ownership

Physics Actor lifetime

Scene object lifetime

```



必要な変更はCommand Queueへ登録し、安全なタイミングで適用する。



\---



\# 53. Editor対応



Editorでは以下をリアルタイム表示する。



```text

Physics World

Active Actors

Dynamic Bodies

Static Bodies

Shapes

Contacts

Simulation Time

Fixed Delta Time

```



さらに、



```text

Pause Physics

Step Physics

Reset Physics

```



を実装できるようにする。



\---



\# 54. Physics Pause



Editorから、



```text

Play

Pause

Step

```



を制御できるようにする。



Stepを押した場合、



```text

Physics Simulation

&#x20;   ↓

1 Fixed Timestep

```



だけ実行する。



\---



\# 55. Performance



Physics処理では毎フレーム不要なAllocationを発生させない。



特に、



```text

Raycast

Collision Event

Trigger Event

Physics Query

```



では可能な限り再利用可能なBufferを使用する。



Physics Simulation中の動的メモリアロケーションを最小化する。



\---



\# 56. Debug / Release



Debug Buildでは、



```text

PhysX Debug Check

Physics Debug Draw

Validation

Profiling

```



を有効にできるようにする。



Release Buildでは不要なDebug処理を無効化する。



\---



\# 57. Unityとの対応関係



Engine APIはUnityの概念を参考にするが、Unity APIをそのままコピーしない。



対応関係：



```text

Unity

&#x20;   Rigidbody

&#x20;       ↓

Engine

&#x20;   RigidbodyComponent



Unity

&#x20;   Collider

&#x20;       ↓

Engine

&#x20;   ColliderComponent



Unity

&#x20;   BoxCollider

&#x20;       ↓

Engine

&#x20;   BoxColliderComponent



Unity

&#x20;   SphereCollider

&#x20;       ↓

Engine

&#x20;   SphereColliderComponent



Unity

&#x20;   CapsuleCollider

&#x20;       ↓

Engine

&#x20;   CapsuleColliderComponent



Unity

&#x20;   Physics.Raycast

&#x20;       ↓

Engine

&#x20;   PhysicsWorld::raycast()



Unity

&#x20;   CharacterController

&#x20;       ↓

Engine

&#x20;   CharacterControllerComponent

```



\---



\# 58. ECS禁止



このPhysics統合ではECSを導入しない。



既存のGameObject + Componentアーキテクチャを維持する。



禁止：



```text

ECS Registry

Entity Component System

Archetype

Sparse Set based Physics Component

```



Physics内部の最適化構造としてPhysXを利用するだけであり、Engine全体をECS化しない。



\---



\# 59. 実装時の注意



既存Engineの設計を尊重する。



既存の



```text

GameObject

Component

TransformComponent

SceneManager

AssetManager

ResourceManager

Input

Time

Threading

Logger

ImGui

DirectX 12 Renderer

```



を勝手に作り直さない。



必要な部分だけ拡張する。



\---



\# 60. コード品質



生成するコードには必ずDoxygenコメントを付ける。



例：



```cpp

/\*\*

&#x20;\* @brief Dynamic Rigidbodyを管理するComponent。

&#x20;\*

&#x20;\* PhysXのDynamic ActorとGameObjectのTransformを同期する。

&#x20;\*/

class RigidbodyComponent final : public Component

{

};

```



命名規則は既存Engineに合わせる。



不要なSingletonを追加しない。



不要なグローバル変数を作らない。



マジックナンバーを使用しない。



constexprを適切に使用する。



RAIIを使用する。



所有権を明確にする。



\---



\# 61. 実装完了条件



以下をすべて満たした時点でPhase 1～3の実装完了とする。



\* PhysX 5をBuildできる

\* EngineからPhysXをLinkできる

\* PhysicsSystemを初期化できる

\* PhysicsWorldを生成できる

\* Fixed TimestepでSimulationできる

\* Static Colliderを生成できる

\* Box Colliderを生成できる

\* Sphere Colliderを生成できる

\* Capsule Colliderを生成できる

\* Rigidbodyを生成できる

\* Dynamic BodyがGravityで落下する

\* Ground ColliderとCollisionする

\* RigidbodyのTransformがGameObjectへ同期される

\* Kinematic Bodyが動作する

\* Physics Shutdownが安全に実行される



\---



\# 62. 実装方針



実装を開始する際は、いきなり全Physics機能を作成しない。



必ず以下の順序で実装する。



```text

1\. PhysX Build

&#x20;      ↓

2\. CMake Integration

&#x20;      ↓

3\. PhysXFoundation

&#x20;      ↓

4\. PhysicsSystem

&#x20;      ↓

5\. PhysicsWorld

&#x20;      ↓

6\. Fixed Timestep

&#x20;      ↓

7\. Static Collider

&#x20;      ↓

8\. Rigidbody

&#x20;      ↓

9\. Transform Synchronization

&#x20;      ↓

10\. Collision

&#x20;      ↓

11\. Trigger

&#x20;      ↓

12\. Physics Query

&#x20;      ↓

13\. Material

&#x20;      ↓

14\. Character Controller

&#x20;      ↓

15\. Joint

&#x20;      ↓

16\. Mesh Cooking

&#x20;      ↓

17\. Debug Rendering

&#x20;      ↓

18\. ImGui Inspector

```



各Phaseが正常動作することを確認してから次のPhaseへ進む。



既存コードを破壊する大規模な変更を避け、段階的に実装する。



\---



\# 63. 最終目標



最終的にゲームコードから以下のように使用できることを目標とする。



```cpp

auto player = scene->createGameObject("Player");



auto\* transform =

&#x20;   player->addComponent<TransformComponent>();



auto\* rigidbody =

&#x20;   player->addComponent<RigidbodyComponent>();



auto\* collider =

&#x20;   player->addComponent<CapsuleColliderComponent>();



rigidbody->setMass(70.0f);

rigidbody->setGravityEnabled(true);



collider->setRadius(0.35f);

collider->setHeight(1.8f);

```



ゲーム中：



```cpp

rigidbody->addForce(force);

```



Raycast：



```cpp

const auto hit =

&#x20;   physicsWorld->raycast(

&#x20;       origin,

&#x20;       direction,

&#x20;       100.0f,

&#x20;       PhysicsLayerMask::Default);

```



というように、\*\*ゲーム側はPhysXを意識せず、自作EngineのPhysics APIだけを使用できる状態\*\*を完成形とする。
