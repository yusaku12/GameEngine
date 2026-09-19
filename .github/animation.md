---
name: "Implement Animation System"
description: "GameEngineへUnity風のアニメーションデータ管理とozz-animationベースの実行系を段階的に実装する"
agent: "agent"
---

# Animation System実装指示書

## 目的

このGameEngineへ、Unityの使いやすさを参考にした汎用的で拡張可能なAnimation Systemを実装する。
アニメーションのサンプリング、ブレンド、ローカル姿勢からモデル空間姿勢への変換には、必ず`ozz-animation`を使用する。

単にモデル内のAnimationキーを再生するだけではなく、以下を独立した責務として設計する。

- Skeleton Asset
- Animation Clip Asset
- Animator Controller Asset
- Animator Component
- GameObjectごとのAnimator Instance
- Import / Build Pipeline
- Runtime Evaluation
- Render Threadへ渡すSkinning Snapshot
- Editor / Inspector
- Scene / Prefab Serialization

最初からUnityの全機能を再現しない。各Phaseをビルド・テスト可能な状態で完了させ、既存の静的モデル描画を壊さず段階的に導入すること。

## 作業開始前の必須調査

実装前に、以下とその呼び出し元を確認し、現在のコードを正として設計を調整すること。

- [ModelResource](../../Source/Assets/Model/Resource/ModelResource.h)
- [AnimationResource](../../Source/Assets/Model/Resource/AnimationResource.h)
- [SkeletonResource](../../Source/Assets/Model/Resource/SkeletonResource.h)
- [AssimpModelImporter](../../Source/Assets/Model/Import/AssimpModelImporter.cpp)
- [ModelManager](../../Source/Assets/Model/ModelManager.cpp)
- [Model schema](../../Schemas/FlatBuffers/Model.fbs)
- [ModelRendererComponent](../../Source/Core/GameObject/Component/ModelRendererComponent.cpp)
- [ModelRenderSubmission](../../Source/Graphics/Renderer/ModelRenderSubmission.h)
- [Model shader](../../Assets/Shaders/Model.hlsl)
- [Scene schema](../../Schemas/FlatBuffers/Scene.fbs)
- [Prefab schema](../../Schemas/FlatBuffers/Prefab.fbs)
- [Serialization versions](../../Source/Core/Serialization/SerializationVersions.h)
- [Visual Studio project](../../GameEngine.vcxproj)

最初に短い実装計画を提示し、変更対象、依存関係、データ移行、各Phaseの検証方法を明示すること。その後は質問待ちで止まらず、最小のPhaseから実装と検証を進めること。既存仕様と衝突する不明点だけを質問すること。

## 最重要方針

- `ozz-animation`を単なるデータ形式としてではなく、Runtime評価処理の中核として使用する。
- 手書きのキー補間、手書きのSkeleton階層評価、独自Blend計算を並行実装しない。
- Import時だけ`ozz::animation::offline`を使用し、ゲーム実行時は`ozz::animation` Runtime APIだけへ依存する。
- Assetは共有可能で原則不変、再生時刻・遷移状態・Sampling Context・PoseはGameObjectごとのInstanceに置く。
- Model、Skeleton、Animation Clip、Animator Controllerを別Assetとして管理し、永続参照はPathではなく`AssetGUID`を優先する。
- Asset ManagerはDirectX 12型を保持せず、AnimatorはCommand ListやGPU Resourceを操作しない。
- Render ThreadへAnimatorの可変メモリを直接参照させない。描画Queueへ所有権と寿命が明確なSkinning Snapshotを渡す。
- UpdateおよびRenderのHot Pathで文字列検索、ファイルI/O、Assetロード、毎フレームのHeap Allocationを行わない。
- Skeleton互換性を名前だけで判定しない。Joint順序、親Index、階層Pathなどから安定したSkeleton Signatureを生成する。
- 既存の非アニメーションModelはIdentity Paletteなしでも従来どおり描画できること。
- Public APIには既存形式のDoxygenと`@thread_safety`を記載する。
- raw pointerで所有権を管理しない。既存のHandle、RAII、Snapshot方式に合わせる。
- エラーは既存のLogging / Result方針へ合わせ、主要フローへ例外を新規導入しない。

## 目標Architecture

```text
FBX / glTF
    -> Assimp Import Data
    -> ozz Offline Builder
    -> Skeleton Asset + Animation Clip Asset + Model Asset

Animator Controller Asset
    -> AnimatorComponent
    -> AnimatorInstance
       -> SamplingJob
       -> BlendingJob
       -> LocalToModelJob
       -> Skinning Palette Snapshot
    -> ModelRenderSubmissionQueue
    -> DX12 Renderer
    -> Model.hlsl
```

依存方向は次を維持すること。

```text
Importer / Serializer -> Asset Data
Animator Runtime      -> Asset Managers + ozz Runtime
Renderer              -> Render Submission Snapshot
DX12 Backend          -> GPU Upload / Draw
```

RendererからAnimatorComponentやSceneを参照してはならない。

## 依存ライブラリ導入

1. `ozz-animation`は公式Repositoryの安定したRelease TagまたはCommitへ固定する。追従不能なブランチ指定は禁止する。
2. Repositoryの既存方針に合わせ、`Library/ozz-animation`配下へのsubmoduleまたは同等に再現可能な取得方法を採用する。
3. x64 Debug / ReleaseでRuntime Libraryをリンクする。ImporterをEngine実行ファイルへ含める場合のみOffline Libraryもリンクする。
4. Include Path、Library Path、Library名、Runtime Library設定を全Configurationで整合させる。
5. ozzのTest、Sample、ToolをGameEngine本体へ不用意にリンクしない。
6. ozzのLicenseを配布物で追跡できるようにする。
7. 依存追加後、空の型参照だけで先へ進まず、最小のSkeleton/Animation生成・Archive Round Tripを検証する。

## Asset設計

### Skeleton Asset

SkeletonをModelの付属データだけにせず、共有可能なAssetとして扱う。最低限以下を持つ。

- `AssetGUID`
- Name
- `ozz::animation::Skeleton`
- Joint名とIndexのImport時Lookup
- 親Indexおよび階層Path
- Inverse Bind Pose
- Skeleton Signature
- Asset Version
- Source Asset Metadata

Runtime Hot PathではJoint名検索を行わず、Indexへ解決済みにする。`ozz::animation::Skeleton`のJoint順と頂点のBone Index、Inverse Bind Pose配列の順序が一致する契約を明記し、Import時に検証する。

### Animation Clip Asset

ClipはModelから独立して共有・差し替え可能にする。最低限以下を持つ。

- `AssetGUID`
- Name
- 対象Skeleton SignatureまたはSkeleton GUID
- `ozz::animation::Animation`
- Duration
- Wrap Mode: Once / Loop / PingPong
- Additiveかどうか
- Root Motion設定
- Animation Event配列
- Asset Version
- Source Clip名とImport Metadata

キー列をRuntimeで独自評価しない。Assimpの入力キーをImport時に`ozz::animation::offline::RawAnimation`へ変換し、Validation後に`AnimationBuilder`でRuntime Animationへ変換する。

### Animator Controller Asset

Controllerは再生状態を持たない共有Assetとする。最低限以下を持つ。

- `AssetGUID`
- Parameters: Float / Int / Bool / Trigger
- Layers
- States
- Default State
- State Motion参照
- Speed
- Loop Override
- Transitions
- Conditions
- Exit Time
- Transition Duration
- Any State
- Asset Version

State、Parameter、Transitionは保存用GUIDまたは安定IDを持たせる。Runtimeでは文字列名を型付きIDへ事前解決する。Triggerは遷移成立時だけ消費し、評価順序と優先順位を決定的にする。

初期版は1 Layer、単一Clip State、Cross Fade、上記4種Parameterを完成させる。次に1D Blend Tree、複数Layer、Avatar Mask、Additive Layer、Override Controllerを追加できる構造にする。ただし未実装機能の空クラスを大量に作らない。

### Source DataとRuntime Data

Assimp由来の可読な中間データと、ozzの最適化済みRuntime Objectを区別する。Shipping RuntimeでRaw Key配列を保持し続けない。

保存方式は次のどちらかを選び、理由を記録する。

- FlatBuffers Asset内に、ozz Archiveで生成したVersion付きBinary Blobを格納する。
- ozz Archiveを専用Asset Payloadとして保存し、FlatBuffers Metadataから参照する。

ozzの内部メモリ表現を`memcpy`で永続化してはならない。必ず公式Archive APIを使用し、破損データ、Version不一致、Skeleton不一致をロード時に拒否する。

## Runtime設計

### AnimatorComponent

Unityの利用感を参考に、最低限以下を提供する。

- Controller Assetの設定
- `play(stateId, normalizedTime)`
- `crossFade(stateId, duration)`
- `setFloat` / `setInt` / `setBool` / `setTrigger` / `resetTrigger`
- `getCurrentState`
- Speed
- Enabled
- Apply Root Motion
- Inspector表示

公開APIは文字列版を利便APIとして許可してよいが、内部では一度だけIDへ変換する。存在しないParameter、型不一致、互換性のないSkeletonを安全に失敗させ、ログを毎Frame出し続けない。

### AnimatorInstance

GameObjectごとの可変状態として以下を所有する。

- 現在Stateと遷移先State
- State Time / Normalized Time / Loop Count
- Parameter値とTrigger状態
- Clipごとの`ozz::animation::SamplingJob::Context`
- SoA Local Transform Buffer
- Blend Buffer
- Model Space Matrix Buffer
- Skinning Palette Buffer
- Event Cursor
- Root Motionの前回Sample

SkeletonまたはController変更時に必要容量を再確保し、通常FrameではBufferを再利用する。`deltaTime`が大きい場合、負値Speed、Loop境界、Transition完了、無効Assetを明示的に扱う。

### ozz評価Pipeline

各Frameの基本処理は次とする。

1. State MachineとParameter条件を更新する。
2. ClipごとのRatioを`[0, 1]`へ正規化する。
3. `ozz::animation::SamplingJob`でLocal PoseをSampleする。
4. 遷移中は`ozz::animation::BlendingJob`でPoseをBlendする。
5. `ozz::animation::LocalToModelJob`でModel Space Joint Matrixへ変換する。
6. Model Space Joint MatrixとInverse Bind PoseからShader契約に合うSkinning Paletteを生成する。
7. Root MotionとEventをMain Thread側で解決する。
8. 不変SnapshotとしてRender Queueへ提出する。

既存エンジンはrow-major行列と`mul(vector, matrix)`を使用しているため、ozzの列・行規約、Assimp変換、DirectX行列、Inverse Bind Poseの乗算順を推測で決めない。Bind Poseで全頂点が元位置になるテストと、既知の1 Joint回転テストで変換契約を確定する。

## GPU Skinningと描画境界

- 既存の`SkinningConstants : register(b1)`と`MAX_SKINNING_BONES = 256`を起点にする。
- Bone上限超過はImport時またはAsset Build時に明確なエラーとし、Shader側のClampで破損を隠さない。
- Skinning Paletteを`ModelRenderSubmission`へ追加し、Queueのconsumeまで有効なSnapshotにする。
- 非Skinned ModelはSkinning Uploadを省略できるようにする。
- PositionだけでなくNormal、Tangent、BitangentもSkinningし、非一様Scaleの扱いを明記する。
- Shadow / Depth / Alpha Testを含む全Skinned Passで同じPoseを使う。
- 1 Drawごとの256行列Constant Bufferが容量や更新回数の問題になる場合、Structured Buffer等への移行点を分離する。ただし初期実装では既存構造に合う最小変更を優先する。
- Dynamic Boundsは初期版では保守的なImport Boundsまたは明示的な拡張Boundsを使用し、将来のPer-Clip Boundsへ拡張可能にする。毎FrameのCPU頂点変形は禁止する。

## Import Pipeline

- AssimpのSkeleton階層から`RawSkeleton`を構築し、親子順序、重複名、欠損Joint、複数Rootを検証する。
- Assimp Animationのtickを秒へ一度だけ変換する。`ticksPerSecond == 0`のFallback規則を固定する。
- Translation / Rotation / Scale KeyをJoint Trackへ対応付ける。欠損ChannelはBind Poseを使う。
- Quaternionを正規化し、座標系・Handedness・単位Scale変換をSkeleton、Mesh、Animationへ一貫して適用する。
- Clip名が空または重複する場合の安定した命名規則を定める。
- Builder前後でValidationし、不正Clipだけを黙って捨てずAsset Pathと理由をLogする。
- 同じSkeletonを持つ複数FBXからClipだけをImportできる設計にする。
- 再Import時もAsset GUIDとController参照を維持する。

## Serialization

新しいFlatBuffers Schemaを用途別に追加する。少なくともSkeleton、Animation Clip、Animator ControllerをModel Schemaから独立させることを検討し、責務が異なるAssetを1つの巨大Schemaへまとめない。

- File IdentifierとVersionを持たせる。
- 既存Fieldの順序やDefault値を破壊的に変更しない。
- Schema追加時は`Tools/FlatBuffers/build_schema.ps1`、`.bat`、`GameEngine.vcxproj`のInput / Outputも更新する。
- Generated Headerを手編集しない。
- Save直後のLoadで同値になるRound Trip Testを追加する。
- 旧`.model` v2のSkeleton / Animationを読める移行期間を設け、新形式へ変換できるようにする。
- Migration完了前に`ModelResource::skeleton`や`animations`を削除しない。

現在のScene / PrefabはComponent型名だけを保存している。Animatorの設定値だけをSchemaへ特例追加せず、Componentが型付きPayloadを保存・復元できる共通契約を先に設計する。未知Component Payloadを安全に無視でき、既存Scene / Prefabを読み込める後方互換性を維持する。

## ManagerとHandle

Skeleton、Animation Clip、Animator Controllerは既存のModelManager / MaterialManager規約に合わせる。

- Index + Generationの型付きHandle
- Invalid Handle
- Path正規化Cache
- GUID Lookup
- Immutable Snapshot取得
- Load / Create / Save / Unload / Clear
- 重複ロード防止
- MutexでManager内部状態を保護

複数Managerへ同じ実装をコピーし続けず、既存パターンを壊さない範囲で共通化を検討する。ただしAnimation導入と無関係なResource System全面改修は行わない。

## Editor UX

初期版で以下を操作可能にする。

- AnimatorComponentの追加・削除
- Controllerの選択、解除、保存状態表示
- Default State、再生 / 一時停止、Speed、Normalized Timeの確認
- Parameterの型に応じた編集
- Current StateとTransition進捗のDebug表示
- Model Import時に生成されたSkeleton / Clip一覧とImport Errorの表示

次段階でAnimator Controller Editorを独立Windowとして追加する。State Graph、Transition、Parameter、Blend Treeを編集可能にするが、Runtime State MachineとEditor描画コードを分離する。Editor操作から非同期Dialogを開く場合は既存の`MainThreadDispatcher`規約に従う。

## 実装Phase

### Phase 0: 設計固定と依存導入

- ozzのVersion固定、Build統合、License追跡
- 行列、座標系、Joint順序、Asset参照、Thread Ownershipの契約を短い設計文書へ記録
- 最小のozz Offline -> Archive -> Runtime Round Trip Test

完了条件: Debug / Release x64でozzをLinkでき、Round Trip Testが通る。

### Phase 1: Skeleton / Clip Asset Pipeline

- Skeleton / Clip型、Handle、Manager、Serializer
- Assimpからozz Offline型への変換
- Skeleton Signatureと互換性検証
- 旧Model内データのMigration Path

完了条件: 実AssetをImport、保存、再読込し、Joint数、Duration、Signatureが一致する。

### Phase 2: 単一Clip再生

- AnimatorComponent / AnimatorInstance
- SamplingJob / LocalToModelJob
- 再利用Buffer
- Skinning Snapshot
- Shaderで完全なPosition / Normal / Tangent Skinning

完了条件: Bind Pose、Loop、Once、速度変更、静的Model fallbackが正しく描画される。

### Phase 3: ControllerとCross Fade

- Parameter、State、Transition、Any State
- BlendingJobによるCross Fade
- Trigger消費規則
- Controller SerializationとInspector

完了条件: 条件遷移とCross Fadeが決定的に動作し、保存・再読込後も同じ結果になる。

### Phase 4: Scene / Prefab永続化と運用性

- 共通Component Payload Serialization
- Animator参照と設定値の保存・復元
- 再Import時のGUID維持
- Asset欠損時の安全なFallback

完了条件: Scene / Prefab Round Trip後にController、Parameter Override、再生設定が復元される。

### Phase 5: 拡張機能

優先順位に従い、必要になったものだけを追加する。

1. Animation Event
2. Root Motion
3. 1D Blend Tree
4. Layer / Avatar Mask / Additive
5. Animator Override Controller
6. Animation Culling / Job化
7. Retargeting

IK、Humanoid自動Retarget、Timeline、State Machine Behaviour、2D Blend Tree、GPU Animationは初期スコープ外とする。

## APIと挙動の必須テスト

- ozz Archiveの正常系、破損、Version不一致
- Skeleton Signature一致 / 不一致
- Bind Pose PaletteがIdentity相当になること
- 1 JointのTranslation / Rotation / Scale
- Clipの先頭、末尾、Loop境界、空Clip、Duration 0
- Once / Loop / PingPongと正負Speed
- Cross Fade開始、中間、完了
- Float / Int / Bool / Trigger条件とTrigger消費
- 大きな`deltaTime`で複数Loopを跨ぐEvent
- Controller / Clip / Skeleton Asset欠損時のFallback
- Bone上限ちょうど、上限超過、無効Bone Index、Weight合計0
- Scene / Prefab / 各AssetのRound Trip
- 非Skinned Modelの描画Regression
- Debug / Release x64 Build

Visual Testだけで完了とせず、変換規約とState Machineは自動テスト可能な純粋ロジックへ分離すること。新しいWarningを追加せず、Warning抑制で問題を隠さない。

## PerformanceとThreading

- Animator更新は初期版ではMain Thread onlyとしてよいが、Instance間に共有可変状態を作らず、将来のJob化を妨げない。
- Asset ManagerはThread-safe、AnimatorInstanceはMain Thread only、GPU CacheはRender Thread onlyなど、境界をDoxygenへ明記する。
- Sampling ContextとPose Bufferを毎Frame作り直さない。
- State / Parameter / Joint参照は事前解決したIndexを使う。
- 同一Clip Assetは共有し、GameObjectごとに複製しない。
- Render Submission後にMain ThreadがPoseを更新しても、Render側のSnapshotが変化しないこと。
- Profileで必要性を確認するまで複雑なPose CacheやGPU Animationを導入しない。

## 禁止事項

- ozzを導入しながら独自Interpolatorを実際の再生経路に残すこと
- `ModelResource`へControllerの再生状態を保存すること
- `AnimatorComponent`へ共有Clipデータを複製すること
- RendererからScene、GameObject、AnimatorComponentを逆参照すること
- Render Queueへ寿命不明な`Matrix*`や`span`を保存すること
- Joint名を毎Frame検索すること
- Generated FlatBuffers Headerを直接編集すること
- Skeleton不一致をIndex Clampで隠すこと
- 既存AssetをMigrationなしで読めなくすること
- Animation実装のついでに無関係なRenderer / Resource全面改修を行うこと

## 各Phaseの進め方

各Phaseで必ず以下を行う。

1. 変更前に対象Symbolの使用箇所と既存テストを確認する。
2. 最小の縦切り実装を行う。
3. Schemaを変更した場合は生成スクリプトを実行する。
4. 対象Unit Testを実行する。
5. Debug x64をBuildする。
6. 描画変更時は静的ModelとSkinned Modelを実行確認する。
7. Diffを確認し、無関係な変更と生成物の混入を除外する。
8. 設計判断、残課題、測定結果を更新する。

失敗したBuildやTestを無視して次Phaseへ進まない。既存の無関係な失敗がある場合は、再現コマンドと今回の変更との非関連性を報告する。

## 最終報告

完了時は以下を簡潔に報告する。

- 実装したPhaseと未実装Phase
- 追加したAsset / Runtime / Renderer境界
- ozzを使用している具体的なJobとOffline Builder
- Schema VersionとMigration方針
- 実行したBuild / Testと結果
- Performance上のAllocation数または確認結果
- 既知の制約
- PR URL。認証やRemote不足で作成不能なら、その具体的理由とPR作成に必要な手順

既存の未コミット変更を上書きまたはRevertせず、今回の変更だけを自己ReviewしてからPull Requestを作成すること。