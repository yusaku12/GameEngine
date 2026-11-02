# Deferred Rendering 実装手順書（DirectX 12 / C++20 / HLSL）

> **目的:** Unity HDRP / Unreal Engine のような拡張可能なリアルタイム描画基盤を目標に、自作 DirectX 12 エンジンへ Deferred Rendering（遅延レンダリング）を統合する。  
> **GitHub Copilot 向け:** この文書を実装仕様として扱い、既存コードを調査してから段階的に実装すること。既存のエンジン構造を無視した大規模な一括書き換えは禁止。

---

## 0. 最重要方針

1. **最初に既存コードを調査する。** `Graphics` / `DX12` / `Device` / `Queue` / `Command` / `Fence` / `Resource` / `Descriptor` / `Pipeline` / `Shader` / `SwapChain`、Scene、Camera、Transform、Model、Material、Light、Shadow、PostEffect、ImGui の実装と命名規則を確認する。
2. 既存の Device、Queue、CommandList、Fence、Descriptor Heap、Resource State 管理、PSO 管理、Shader 読み込み、Scene 描画経路を再利用する。類似機能を重複実装しない。
3. 実装前に、現状の描画フロー・クラス構成・変更予定ファイル・不足している依存機能を短く報告し、依存関係に沿って実装する。
4. エンジンの既存 API を優先する。既存設計と本仕様が衝突する場合は、独自の仕組みを増やす前に既存設計に適合させる。
5. C++20、DirectX 12、HLSL を使用する。ソースとシェーダーはプロジェクト既定の文字コード・コンパイル設定に従い、Windows では必要に応じて `/utf-8` を維持する。
6. 失敗を握りつぶさない。HRESULT、リソース作成、シェーダー読み込み、PSO 作成、Descriptor 確保のエラーを既存 Logger で報告し、原因が分かる情報を含める。`printf` による場当たり的な出力を導入しない。
7. リソースの寿命と GPU 実行完了を厳守する。GPU が参照中のリソース、Descriptor、Upload 領域を早期解放・上書きしない。Fence を使い、安全なタイミングで破棄する。
8. まず不透明物の基本経路を完成させ、段階ごとにビルド・実行・検証する。最初から全機能を一括実装しない。
9. Unity / Unreal の内部実装を完全コピーするのではなく、同等の実用機能と拡張可能なアーキテクチャを実現する。
10. **現状のクラス名やファイル名を想像で決めつけない。** 実際のプロジェクト構成を調査し、命名規則に合わせてから追加する。

## 1. 到達目標

最終的に次のレンダリングフローを実現する。

```text
Scene / Camera
    |
    +-- Frame setup / render-size update
    |
    +-- Shadow depth pass (必要なライトのみ)
    |
    +-- Geometry Pass (不透明物)
    |      +-- GBuffer Albedo + Material flags
    |      +-- GBuffer Normal
    |      +-- GBuffer Material (Metallic / Roughness / AO など)
    |      +-- GBuffer Emission (設計に応じて別 RT または Albedo RT に格納)
    |      +-- Depth / Stencil
    |
    +-- Deferred Lighting pass
    |      +-- Directional lights
    |      +-- Point / Spot lights
    |      +-- Shadow visibility
    |      +-- IBL / environment lighting
    |
    +-- Sky / background pass（既存仕様に合わせる）
    |
    +-- Forward transparent pass
    |
    +-- Post-processing
    |      +-- HDR / exposure
    |      +-- Bloom
    |      +-- Tone mapping
    |      +-- AA (FXAA 等)
    |
    +-- Final composite / present
    |
    +-- ImGui editor overlay
```

実際の順序は既存エンジンの SceneRT、PostEffect、ImGui 合成方式に合わせて確定する。SceneRT を ImGui で表示している場合、ImGui 表示用 SRV とレンダーターゲットの Resource State を正しく管理する。

### 非目標

- 最初の段階で Unity / Unreal の全機能、全プラットフォーム、全 GPU を再現すること。
- Deferred パスだけで透過物、髪、半透明エフェクトを完全に処理すること。
- 根拠なく Render Graph、ECS、巨大な新規フレームワークを導入すること。
- 一時的に動かすために Resource State や Descriptor の同期を省略すること。

---

## 2. 事前調査と設計レポート

実装開始前に、以下を検索して実装状況を確認する。

- D3D12 Device / Command Queue / Command Allocator / Graphics Command List / Fence
- SwapChain と Back Buffer のフォーマット・Resource State
- Descriptor Heap（RTV / DSV / CBV-SRV-UAV）、Descriptor の割り当てと解放方針
- Texture / RenderTexture / FrameBuffer / RenderTarget のラッパー
- Resource Barrier と状態遷移の管理方法
- Root Signature、PSO、Shader / `.cso` ロード、ホットリロード
- Camera、Projection、View、Near/Far、Viewport、Scissor
- Transform の行列規約（row/column、転置の有無、左手系/右手系）
- Model / Mesh / Subset / Material / Texture / Skinning
- Light、Shadow Map、IBL、PostEffect、Tone Mapping、Bloom、AA
- SceneManager / GameObject / Renderable の収集・描画手順
- ImGui の Scene View / SRV 表示と Descriptor の所有者
- Resize、最小化、複数フレーム、Fence、GPU リソース破棄処理

調査結果を以下の形式で報告する。

1. 再利用する既存クラスと API
2. 新規作成が必要なクラス・ファイル
3. 既存コードとの接続箇所
4. Resource State / Descriptor / GPU 同期上のリスク
5. 段階ごとの実装計画と検証方法

調査だけで終了せず、その後は段階的に実装を進める。

---

## 3. 推奨アーキテクチャ

実際のコードベースに合う場合のみ、次の責務分割を採用する。すでに同等の責務を持つクラスがあれば再利用する。

### `DeferredRenderer`（または既存 Renderer の拡張）

- フレームの各描画パスを順序どおりに実行する。
- Scene / Camera / Renderable / Light の既存データを利用する。
- 各パスの Resource State 遷移を明示的かつ検証可能に管理する。
- G-Buffer、Lighting、Forward Transparent、Post Process を適切に呼び出す。
- リソース作成・破棄の責務を必要以上に抱え込まない。

### `GBuffer`

- G-Buffer 用 Texture、RTV、SRV、Depth/Stencil を管理する。
- 画面サイズ変更時にリソースを再作成する。
- フォーマット、サイズ、サンプル数、Descriptor、Resource State を一元管理する。
- G-Buffer の各チャンネルを ImGui で可視化できるようにする。

### `DeferredLightingPass`

- G-Buffer を読み取り、HDR の Lighting Target にライティング結果を書き込む。
- Fullscreen Triangle/Quad など、既存エンジンに適した方式で実装する。
- ライト数に応じた処理を実装し、ライトデータを既存の Light API から受け取る。
- 深度を必要とする場合は G-Buffer Depth を SRV として読み取れる構成にする。

### `Material` / `MaterialRenderer`

- 既存 Material を拡張して PBR パラメータを表現する。
- Material ごとに Geometry PSO を乱立させない。必要な Shader Variant / PSO の組み合わせを整理する。
- テクスチャが未設定の場合は既存のデフォルトテクスチャ（例：白、法線用のフラット法線）を利用する。

### その他

- `ShadowPass`、`EnvironmentLighting`、`PostProcess` は、既存実装があれば統合し、なければ必要な段階で追加する。
- クラスの責務、所有権、初期化・終了処理を明確にする。
- クラス・公開 API にはプロジェクト規約に沿った Doxygen コメントを付ける。既存コードのコメント言語に合わせ、日本語コメントを基本とする。

---

## 4. G-Buffer の設計

最初は画質と帯域のバランスを考えた構成にし、GPU 対応状況と既存フォーマットを確認して確定する。

| リソース | 推奨候補 | 格納内容 |
|---|---|---|
| Albedo | `DXGI_FORMAT_R8G8B8A8_UNORM` または SRGB 運用を明確化した互換形式 | ベースカラー RGB、必要ならマテリアルフラグを別管理 |
| Normal | `DXGI_FORMAT_R16G16B16A16_FLOAT` または適切に符号化した低帯域形式 | ビュー空間またはワールド空間法線 |
| Material | `DXGI_FORMAT_R8G8B8A8_UNORM` 等 | Metallic、Roughness、Material AO、必要ならフラグ |
| Emission（必要な場合） | `DXGI_FORMAT_R16G16B16A16_FLOAT` 等 | HDR 発光色 |
| Depth/Stencil | `DXGI_FORMAT_D32_FLOAT`、`DXGI_FORMAT_D24_UNORM_S8_UINT` 等、既存 DSV 方針に適合する形式 | 深度、必要ならステンシル |
| Lighting Target | `DXGI_FORMAT_R16G16B16A16_FLOAT` 推奨候補 | HDR ライティング結果 |

### G-Buffer 設計ルール

- 上記は候補であり、無条件に全フォーマットを採用しない。帯域・メモリ・精度・必要機能を比較して最終決定する。
- 初期実装では MSAA を無理に導入せず、まずシングルサンプルで正しく動かす。MSAA 対応は G-Buffer と Deferred Lighting のサンプル数・リゾルブ設計を別途検討する。
- 法線は Geometry Pass と Lighting Pass で同じ座標系を使う。Normal Map を使う場合は TBN と接線データの扱いを確認する。
- Roughness の 0 は数値不安定を起こし得るため、BRDF 内で適切な下限値を設ける。
- sRGB と Linear の扱いを統一する。Albedo の色は線形空間でライティングし、sRGB 変換を二重適用しない。
- Material flags を格納する場合、整数形式・符号化・フィルタリングの制約を確認する。
- Depth を SRV として参照する場合、DSV/SRV の互換性、読み取り専用 DSV の要否、Resource State を正しく設計する。
- Emission は G-Buffer に格納する方法と、別パス・別 RT にする方法を比較し、最小限のメモリで実装する。
- 使用しないチャンネルを無駄に確保しない。

---

## 5. Geometry Pass

### 処理

1. G-Buffer を適切な Clear 値でクリアする。
2. Depth/Stencil をクリアする。
3. Camera の View / Projection と各 Draw Item の Transform を設定する。
4. 不透明オブジェクトを描画し、各 G-Buffer に Material 情報を書き込む。
5. Alpha Test が必要なマテリアルは専用 variant または適切な PSO で扱う。
6. 描画後、次のパスで SRV として参照するために Resource State を遷移させる。

### 頂点・ピクセルシェーダー

- 既存の Mesh 頂点形式を尊重する。
- Position、Normal、Tangent、UV、Skinning 情報が利用できる場合は正しく処理する。
- スキニングモデルは Geometry Pass で正しい最終姿勢を描画する。
- Normal は使用する座標系に応じて変換し、非一様スケール時は適切な Normal Matrix を使う。
- Tangent-space Normal Map を使う場合、Tangent の handedness と TBN を正しく扱う。
- Material の Base Color、Albedo Texture、Normal Texture、Metallic、Roughness、AO、Emission を読み取る。
- 既存のテクスチャスロット・Root Signature と競合しないようにする。
- CPU 側の CBV レイアウトと HLSL の `cbuffer` レイアウト、アラインメント、行列の転置規約を一致させる。

### 描画対象の分類

- Opaque：Geometry Pass 対象。
- Masked / Alpha Test：対応する場合は Geometry Pass で discard し、Depth と G-Buffer を一致させる。
- Transparent：基本的に Geometry Pass から除外し、後段の Forward Transparent Pass で描画する。
- Sky / Background：既存エンジンの深度・背景描画仕様と矛盾しない順序で処理する。

---

## 6. Deferred Lighting Pass

### 基本方針

- Fullscreen Triangle/Quad で G-Buffer を読み、HDR Lighting Target に最終ライティングを出力する。
- ピクセルごとに Albedo、Normal、Metallic、Roughness、Depth、Emission 等を取得する。
- Depth から View Position を再構築する方式を採用する場合、Projection 行列と深度範囲に対応した式を実装・テストする。Position RT を追加する場合はメモリ帯域とのトレードオフを明記する。
- ライティングは線形空間で行う。トーンマッピングは後段で行う。
- Background / Sky の扱いを明示し、深度が背景値のピクセルに通常の BRDF ライティングを誤適用しない。
- G-Buffer の SRV と Lighting Target の RTV が同じサブリソースを競合参照しないようにする。

### PBR / BRDF

- Metallic-Roughness ワークフローを採用する。
- Cook-Torrance 系 BRDF を基礎に、NDF、Geometry、Fresnel の項を実装する。
- 既存の PBR コードがある場合は再利用する。
- 数値安定性を確保する。`NdotL`、`NdotV`、分母、Roughness などに適切な下限値を設ける。
- エネルギー保存を意識し、Diffuse と Specular の寄与を整合させる。
- 露出・トーンマッピングとライティングの責務を混在させない。

### ライト種別

**Directional Light**
- ライト方向、色、強度を扱う。
- シーン全体の照明に利用する。

**Point Light**
- 位置、色、強度、範囲を扱う。
- 範囲外のピクセルでは寄与をゼロにする。
- 距離減衰は物理的に妥当なモデルを用い、任意の係数を場当たり的に追加しない。

**Spot Light**
- 位置、方向、色、強度、範囲、内外コーン角を扱う。
- コーン外の減衰と距離減衰を安定して計算する。

### ライト数と性能

- 初期段階では少数ライトを正しく描画できる実装を優先する。
- ライト配列を CBV に詰め込む場合はサイズ上限とアラインメントを確認する。
- ライト数が増えたときの負荷を計測し、必要になった段階で Light Volume、タイルド／クラスター化ライティング等を検討する。
- ライトごとに全画面を無条件に何度も描画する設計を最終形として固定しない。
- ライトデータの CPU/HLSL 構造体のレイアウトと容量制限を明確にする。

---

## 7. Shadow Mapping との統合

- 既存 Shadow Map があれば再利用する。
- ない場合は Directional Light 向け Shadow Map を最初の対象にする。
- Shadow Depth Pass で深度を生成し、Lighting Pass で比較サンプリングする。
- ライト空間変換、Depth Bias、Slope-Scaled Depth Bias、Shadow Map の解像度、Sampler Comparison 設定を検証する。
- カスケードシャドウ（CSM）は、単一 Directional Shadow Map が安定してから段階的に追加する。
- Point / Spot Light のシャドウは後続機能として分離し、最初の実装を複雑にしすぎない。
- シャドウのないライトは安全に処理し、未設定 Descriptor を参照しない。
- シャドウの Resource State、Depth Texture の SRV、Descriptor の寿命を正しく管理する。

---

## 8. IBL / 環境光

段階的に実装する。

1. まず簡易 Ambient / Environment Lighting で暗部が完全な黒にならない基礎を作る。
2. 次に環境キューブマップの Diffuse Irradiance を実装する。
3. 必要に応じて Prefiltered Environment Map と BRDF LUT による Specular IBL を実装する。
4. HDR 環境マップの読み込み・変換・Mip 生成手順を明示する。
5. Environment Lighting の強度と Post Process の Exposure を独立して調整可能にする。

IBL を「一様な Ambient 色を加えるだけ」の状態で完成扱いにしない。最低限の実装と、物理ベースの環境反射への拡張段階を区別する。

---

## 9. Forward Transparent Pass

Deferred Rendering の後段で透明物を処理する。

- Transparent Material は Geometry Pass の G-Buffer に通常書き込まない。
- HDR Lighting Target に対して Alpha Blending を行う。
- 深度テストは有効にし、一般的な半透明オブジェクトは適切にソートする。
- Blend State、Depth Write の有無、両面描画の必要性を Material 設定と整合させる。
- 透明物に必要な直接光・シャドウ・IBL を、実装範囲を明記して Forward Shader で処理する。
- パーティクル、トレイル、髪など既存の透明描画機能を壊さない。
- 透過物の正確な順序付けは OIT 等を導入しない限り限界があることを認識し、最初は深度とソートを正しく実装する。

---

## 10. HDR と Post Processing

- Deferred Lighting の出力先を HDR 対応の Linear Render Target にする。
- Bloom、Exposure、Tone Mapping、AA など既存 PostEffect と接続する。
- 既存のポストエフェクトがある場合、重複した別システムを作らず統合する。
- Tone Mapping は通常、Bloom 合成など必要な HDR 処理の後段に置く。
- sRGB 出力変換を二重に適用しない。
- FXAA 等の AA は Tone Mapping の前後どちらに配置するか、入力色域と既存実装を確認して決める。
- 露出値、Bloom 強度などを既存の設定・Editor UI から変更できるようにする。
- 最終合成先と Back Buffer の Resource State を正しく遷移させる。

---

## 11. DirectX 12 の Resource / Descriptor / 同期要件

ここは最優先でレビューする。

### Resource State

- 各パスの前後で、リソースが現在の State と利用目的に合っていることを保証する。
- RTV と SRV の利用を切り替えるときは必要な Resource Barrier を発行する。
- Depth を DSV と SRV の両方で使う場合、実際の Resource Format と View Format の互換性を確認する。
- Enhanced Barriers を既存エンジンが採用している場合はその規約に従う。従来型 Barrier と独自状態管理を無秩序に混在させない。
- 状態を推測だけで書き換えず、エンジンの Resource State 管理方式に統合する。

### Descriptor

- RTV / DSV / shader-visible SRV 等の Heap 種別と用途を厳密に区別する。
- GPU Descriptor Handle と CPU Descriptor Handle を混同しない。
- Descriptor Heap の容量、割り当て、解放、コピー、フレーム跨ぎの寿命を管理する。
- Shader-visible Heap の差し替えや Root Table 設定を既存 Descriptor Manager の方式に合わせる。
- 未初期化・無効な Descriptor を Shader から参照させない。
- Texture の再生成後、古い Descriptor Handle を利用しない。
- ImGui が利用する SRV Descriptor の所有権と寿命を既存の規約に合わせる。

### Fence / リソース寿命

- Resize や再生成前に、旧リソースを GPU が参照していないことを保証する。
- 毎フレーム不用意に GPU 完了待ちを入れない。必要な場面で既存 Fence / Frame Resource の方式を使う。
- Upload Buffer や定数バッファの書き換えが GPU の読み取りと競合しないようにする。
- コマンドリストの Reset / Close / Execute の順序を守る。
- コマンドリストが Close 済みか、同じ Allocator を GPU 実行中に Reset していないか確認する。

### Resize / 最小化

- 画面サイズが 0 のときに Render Target を作成・描画しない。
- SwapChain の Resize と Scene Render Target の Resize を整合させる。
- 全関連 RTV / SRV / DSV を更新し、古い View を残さない。
- Viewport / Scissor / Camera Aspect Ratio を更新する。
- Resize を繰り返しても Descriptor が枯渇しないようにする。

---

## 12. Shader / Root Signature / PSO

- 既存 Root Signature Manager と PSO 管理方式を再利用する。
- Geometry Pass、Deferred Lighting、Shadow、Forward Transparent、Post Process の必要な PSO を責務ごとに管理する。
- Render Target の数・フォーマット、Depth Format、Blend State、Rasterizer State、Depth/Stencil State を PSO と一致させる。
- Geometry Pass の MRT 数と HLSL の `SV_TargetN` を一致させる。
- HLSL の `register(t#)`, `register(b#)`, `register(s#)` を Root Signature と一致させる。
- Descriptor Table の範囲と Shader の参照範囲を一致させる。
- `.cso` の読み込みエラー、Root Signature 不一致、PSO 作成失敗をログに出し、起動失敗時に原因を特定できるようにする。
- ホットリロードが既存実装にある場合は、新しい Shader / PSO もその管理方式に統合する。
- ホットリロード時は GPU が利用中の古い PSO / Root Signature / Shader 関連リソースを安全に保持し、完了前に破棄しない。
- Shader の定数バッファは 256-byte アラインメント等、D3D12 の要件を確認する。

---

## 13. Editor / ImGui デバッグ機能

既存 Editor の Window Menu / Inspector の設計に合わせ、必要なデバッグ UI を追加する。

### G-Buffer Debug View

少なくとも以下を選択して表示できるようにする。

- Final Lighting
- Albedo
- Normal
- Metallic
- Roughness
- Material AO
- Emission（別 RT の場合）
- Depth
- Shadow Map

要件：

- G-Buffer の各 Texture を SRV として表示する。
- Normal は見やすいように適切に 0～1 範囲へ変換する。
- Depth は視認しやすい表示用変換を行い、実際の深度値と表示値を混同しない。
- Metallic / Roughness 等の単一チャンネルをグレースケール表示できるようにする。
- 無効な機能・未作成リソースを選択したときにクラッシュしない。
- ImGui 表示中に Descriptor Heap / Resource State / リソース寿命の不整合を発生させない。

### Render Settings

可能な範囲で以下を変更可能にする。

- Deferred Rendering の有効・無効（Forward 経路が存在する場合のみ）
- Exposure、Tone Mapping の設定
- Bloom の有効・無効と強度
- ライトのデバッグ表示
- G-Buffer Debug View の選択

設定値には適切な範囲制限を設け、UI から不正値が入らないようにする。

---

## 14. 実装フェーズ（順番を守る）

### Phase 0：調査と設計

- 既存描画フローを図にする。
- 再利用クラスと不足機能を一覧化する。
- Resource State / Descriptor / Resize / ImGui の設計を確定する。
- 実装対象ファイルと依存関係を報告する。

**完了条件:** 現行エンジンを壊さない統合方法が説明できる。

### Phase 1：G-Buffer の最小構成

- GBuffer クラスまたは既存 FrameBuffer の拡張を実装する。
- Albedo、Normal、Material、Depth を作成する。
- Resize と Resource State を実装する。
- G-Buffer をクリアし、デバッグ UI で各 RT を表示する。
- まず単純なメッシュを Geometry Pass で描画する。

**完了条件:** 各 G-Buffer が正しい値を持ち、Resize 後も正常表示できる。

### Phase 2：Geometry Pass の本格統合

- 既存 Model / Mesh / Material / Transform / Camera を接続する。
- 不透明モデルを複数描画する。
- Base Color Texture、Normal Map、Metallic、Roughness を対応させる。
- Alpha Test と Skinning の対応状況を確認し、既存機能を維持する。

**完了条件:** 複数の不透明モデルが正しい深度とマテリアル情報を G-Buffer に出力する。

### Phase 3：Deferred Lighting / PBR

- Fullscreen Lighting Pass を作る。
- Depth から位置を復元するか Position RT を持つかを比較し、採用理由を記録する。
- Directional Light を実装する。
- Cook-Torrance 系 BRDF を導入する。
- HDR Lighting Target と背景ピクセル処理を実装する。

**完了条件:** Albedo / Normal / Metallic / Roughness に応じてライティングが変わり、明るさが不自然に破綻しない。

### Phase 4：Point / Spot Light

- Point Light と Spot Light を追加する。
- 距離・角度減衰を実装する。
- Light 数と GPU 時間を計測する。

**完了条件:** 複数ライトが正しく作用し、ライト範囲外で不要な寄与がない。

### Phase 5：Shadow Mapping

- 既存 Shadow Map を接続するか、Directional Shadow Map を実装する。
- Deferred Lighting にシャドウ判定を統合する。
- Bias と Shadow Acne / Peter Panning を調整する。
- 安定後に CSM を検討する。

**完了条件:** 不透明モデルにライト方向と整合するシャドウが投影される。

### Phase 6：Environment Lighting / IBL

- 環境光の基礎を実装する。
- Irradiance と Specular IBL を段階的に追加する。
- Material の Roughness と環境反射が連動することを確認する。

**完了条件:** 直接光が弱い場所でも環境からの光・反射を表現できる。

### Phase 7：Forward Transparent

- 透明オブジェクトの描画リストを分離する。
- Blend / Depth State とソートを実装する。
- 既存のパーティクルやトレイルを検証する。

**完了条件:** 透明物が不透明物の後に合成され、既存エフェクトが壊れない。

### Phase 8：Post Processing / Editor 統合

- HDR、Bloom、Exposure、Tone Mapping、AA を既存 PostEffect に接続する。
- G-Buffer Debug View と Render Settings を統合する。
- Scene View / ImGui の表示を検証する。

**完了条件:** 最終画像と各中間バッファを切り替えられ、描画設定を編集できる。

### Phase 9：最適化と堅牢性

- GPU Timestamp Query 等、既存方式で各パスの GPU 時間を計測する。
- Render Target の帯域・メモリ使用量を確認する。
- 不要な全画面パス、Descriptor 更新、リソース再作成を減らす。
- 必要性と計測結果に基づいて Tile/Clustered Lighting、Render Graph、MSAA 等を検討する。
- Debug Layer の警告・エラーを解消する。

**完了条件:** 長時間実行、Resize、シーン切り替え、Shader 再読み込みでクラッシュや GPU Validation エラーが発生しない。

---

## 15. 検証項目

各 Phase の完了時に、少なくとも以下を確認する。

### 画質

- [ ] Albedo の色が正しい。
- [ ] Normal Map が正しく作用する。
- [ ] Metallic / Roughness の違いが目視で確認できる。
- [ ] Directional / Point / Spot Light が正しく作用する。
- [ ] Depth の不連続や背景ピクセルの異常な発光がない。
- [ ] Shadow Acne / Peter Panning が許容範囲にある。
- [ ] IBL の強度が適切である。
- [ ] Transparent Material の合成が破綻しない。
- [ ] HDR / Tone Mapping / Bloom の二重適用がない。

### DirectX 12 / 安定性

- [ ] Debug Layer で重大な警告・エラーがない。
- [ ] Resource State の不整合がない。
- [ ] GPU Descriptor Handle と CPU Descriptor Handle の取り違えがない。
- [ ] Descriptor Heap の枯渇・不正アクセスがない。
- [ ] Fence 完了前に GPU 使用中リソースを破棄していない。
- [ ] Resize / 最小化 / 復帰を繰り返しても正常に描画できる。
- [ ] シーン切り替え後に古い Render Target / Descriptor を参照しない。
- [ ] Shader / PSO 作成失敗時に原因がログに出る。
- [ ] ImGui の Scene View と G-Buffer View が正常に表示される。
- [ ] Debug View を切り替えても GPU Validation エラーが発生しない。

### パフォーマンス

- [ ] G-Buffer のメモリ使用量を記録する。
- [ ] Geometry / Lighting / Shadow / Post Process の GPU 時間を記録する。
- [ ] ライト数を増やした際の負荷を確認する。
- [ ] 不要な CPU-GPU 同期や毎フレームのリソース再作成がない。
- [ ] 無効化したオプションのパスが不要に実行されない。

---

## 16. Copilot の作業ルール

1. 1 Phase ずつ実装する。複数 Phase を一括で変更しない。
2. 各 Phase の前に、変更予定ファイルと目的を提示する。
3. 各 Phase の後に、変更ファイル、設計判断、ビルド結果、未対応項目、手動検証手順を報告する。
4. ビルドが失敗した場合は、最初に発生した実際のコンパイル／リンクエラーから修正し、別機能を追加しない。
5. 実行・ビルドできない環境では、実行したと偽らず、未検証の範囲とユーザー側で行う確認方法を明記する。
6. 既存 API の意味を調べずに新しいラッパーや Manager を追加しない。
7. 大規模なリネームや無関係な整形を避ける。
8. TODO だけの空実装、常に固定色を返す仮実装、未初期化 Descriptor の利用を完了扱いにしない。
9. HLSL と C++ の構造体、Root Signature、PSO、Render Target Format の対応を必ず検証する。
10. 重要な設計判断（座標系、Depth の復元、G-Buffer Format、Emission の格納方式、Resource State、ライト上限）を文書化する。
11. 既存の Forward Rendering 経路がある場合は、Deferred Rendering の導入後も切り替え・比較できるようにする。ただし既存経路がない場合、比較目的だけで巨大な Forward Renderer を新規実装しない。
12. 各段階でエンジンがビルド可能な状態を維持する。機能の途中段階では、未完成のパスを明示的に無効化する。

---

## 17. 最終納品物

実際のプロジェクト構成に合わせ、必要なファイルだけを作成・変更する。

- Deferred Renderer または既存 Renderer の拡張
- G-Buffer リソース管理
- Geometry Pass 用 HLSL と PSO
- Deferred Lighting 用 HLSL と PSO
- PBR Material の必要な拡張
- Light データと Deferred Lighting の接続
- Shadow / IBL / Forward Transparent の段階的統合
- PostEffect との接続
- ImGui の G-Buffer Debug View / Render Settings
- 必要な設定・ドキュメント・簡単なテストシーン
- 更新した実装手順と、既知の制限・今後の拡張項目

ファイル名は既存プロジェクトの命名規則を調査して決定する。上記の名称を機械的にそのまま採用しない。

## 18. 実装完了の定義

以下をすべて満たすまで「Deferred Rendering 完成」と報告しない。

- 不透明モデルが Geometry Pass で G-Buffer に描画される。
- G-Buffer の各要素をデバッグ表示できる。
- PBR Deferred Lighting が G-Buffer を読み、ライトに応じた HDR の結果を生成する。
- Material の Base Color / Normal / Metallic / Roughness が適切に反映される。
- 透明物が別パスで正しく合成される。
- Resize、シーン切り替え、Descriptor、Resource State、Fence の扱いが安全である。
- 既存 PostEffect と ImGui の Scene View に統合される。
- ビルド結果と、実施済み／未実施の検証項目が明確に報告される。

Shadow、IBL 等を後続 Phase に残した場合は、「Deferred Rendering 基礎完成」と「拡張機能未完了」を区別して報告する。
