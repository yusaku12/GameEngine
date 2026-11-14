# ビルドとリポジトリ構成

エンジンが対応する構成は **Debug x64** と **Release x64** のみです。Visual Studio
2026 C++ ツール (v145)、Windows SDK、Autodesk FBX SDK 2020.3.9 を使用してください。
同梱された DirectXTex プロジェクトは、上流のプロジェクトファイルを編集せず、
エンジンで選択したツールセットを使ってビルドされます。
別の SDK の場所を選ぶには、`FBXSDKRoot` を MSBuild プロパティとして渡します。

Visual Studio Developer PowerShell からビルドします。

```powershell
MSBuild .\GameEngine.sln /m /p:Configuration=Debug /p:Platform=x64
MSBuild .\GameEngine.sln /m /p:Configuration=Release /p:Platform=x64
```

エンジンのコンパイル前に、プロジェクトは PhysX、ozz-animation、DirectXTex をビルドし、
FlatBuffers ヘッダーとコンパイル済みシェーダーを生成します。既存の依存ライブラリや
生成済みヘッダーは不要です。DXC と flatc は `Tools` に用意されています。
コンパイルエラーを回避することはなく、エンジンの警告は引き続きエラーとして扱われます。
PhysX と ozz-animation は内容を追跡するインクリメンタルビルドを使用します。
エンジンのコンパイル前に MSBuild が依存関係のソース／ビルド入力をハッシュし、
必要なライブラリと生成済み CMake プロジェクトを確認します。変更のない依存関係では
PowerShell や CMake を起動しません。DirectXTex は従来どおり MSBuild 独自の
インクリメンタルチェックを使用します。

## ネイティブ依存関係のインクリメンタルビルド

`Tools\Build\NativeDependencies.targets` は、
`Build\GameEngine\x64\<Configuration>\NativeDependencies` 以下に Debug/Release
それぞれの記録を保持します。これらの記録は再生成可能なビルド状態であり、
コミット済みライブラリではありません。また、コンパイル失敗を無視する仕組みでもありません。

- SHA-256 フィンガープリントは、PhysX の `physx` ツリー、ozz の `src`、`include`、
  `extern`、`build-utils`、最上位の CMake 設定、およびエンジンの依存関係スクリプト／
  ターゲットを対象とします。タイムスタンプを戻した場合でも、ファイルの追加・削除・
  内容の変更を検出します。エンジンのみの C++ 編集では依存関係の記録は無効になりません。
- 固定されたサブモジュールのリビジョン変更も既存のリビジョンチェックに反映されるよう、
  ozz の Git HEAD/refs も含めます。無効化されている ozz のテスト／ツール／サンプルの
  ビルドでは、テスト／サンプル／メディアのディレクトリは入力ではありません。
- 構成、ツールセット／SDK の選択、コンパイラ／リンカーのフラグ、CMake、ネイティブ
  ツールのバイナリを追跡します。MSBuild は選択した Visual Studio インスタンス、
  CMake 実行ファイル、VC ツールセットのバージョン、Windows SDK の選択を両スクリプトに
  渡します。`DependencyCMakeExecutable` で別の CMake を明示的に選択できます。
- フィンガープリントまたは CMake キャッシュが変わると、その依存関係の選択中の構成を
  クリーンビルドします。CMake/MSBuild 内部のタイムスタンプチェックだけでは、過去に
  戻されたソースやツールの変更を検出できないため、意図的に保守的な動作としています。
  これによって他の依存関係や別の構成が成功扱いになることはありません。
- スキップする前に、必要なリンク済みライブラリ、CMake キャッシュ、生成スタンプ、
  記録された生成済みプロジェクトがすべて存在することを確認します。出力の削除や
  ビルドツリーのクリーンを検出した場合、古い成功記録を信頼せず復旧します。
- リンク済みライブラリも最後に成功したビルドのものと照合するためハッシュ化します。
  手動で異なるフラグを使ったビルドを含め、アーカイブの置換や破損によって以前の構成の
  成功記録が誤って再利用されることはありません。
- 構成／ビルドと出力確認が成功した後にのみ成功記録を書き込みます。ビルドに失敗すると
  その構成の記録を削除し、次回実行時に再試行します。共有 CMake キャッシュの変更は
  両構成で個別に検出されます。依存関係の共有ツリーでは同時に1つだけビルドしてください。

これらのルールを有効化または変更した後の初回ビルドでは依存関係を再ビルドします。
以降、変更のないビルドではフィンガープリントと出力の確認のみを行います。
`Clean` は現在の構成の記録を消去し、`Rebuild` は記録を再作成します。
依存関係を個別に手動ビルドする場合も、既存の
`Tools\Animation\build_ozz.ps1` と `Tools\Physics\build_physx.ps1` を使用できます。
依存関係をクリーンコンパイルするには `-CleanFirst` を渡します。
`.lib` が既に存在するという理由だけで、これらの確認を無効にしないでください。

## シェーダーのインクリメンタルビルド

`Tools\Build\Shaders.targets` は、Terrain とすべてのポストプロセスパスを含む
36個の組み込みシェーダーエントリーポイントの唯一のビルド入口です。ジョブ一覧と
コンパイラオプションは `Tools\Shaders\CompileShaders.ps1` にあります。Debug と Release
ではシェーダーオプションが同一のため、同じバイトコードと記録を共有します。
すべての組み込みシェーダーは事前コンパイルとホットリロードのどちらでも `-O3` を
使用します。デバッグ情報は埋め込まれますが、最適化によりシェーダー
デバッグ時の変数やステップ実行がソースと一致しない場合があります。
Terrain の警告をエラーとして扱うオプションなど、他の設定は変更しません。

- ソリューションに変更がない場合、PowerShell や DXC を起動せず、シェーダー入力、
  DXC バイナリ、記録済み出力をハッシュします。
- 入力が変わると、スクリプトは SHA-256 記録と DXC の `-MD` が出力する実際の推移的
  インクルードを使って、各エントリーポイントを個別に確認します。ソース編集では該当する
  エントリーポイントのみ、インクルード編集ではその利用側のみを再ビルドします。
  タイムスタンプを戻しても内容の変更は隠せません。
- バイトコードの欠落や変更があった場合は、その出力だけを再ビルドします。
  エントリーごとの記録が欠落している場合は再生成します。ファイルの追加／削除は
  インクルード解決に影響する可能性があるため、保守的に全エントリーを無効化します。
  DXC やスクリプトの変更も全エントリーを無効化します。
- 公開前にバイトコードを一時ファイルへコンパイルします。コンパイラエラーが発生した
  場合はビルドを失敗させ、そのエントリーの成功記録を削除します。以前のバイトコードは
  古い生成物として残しますが、成功したビルドの代替としては扱いません。
- 記録は `Assets\Shaders\Compiled\.build` に保存されます。同じインクリメンタル動作を
  手動で行うには `Tools\Shaders\CompileShaders.bat` を使用し、全エントリーをコンパイル
  するには `-Force` を追加します。`CleanShaderCache.bat` はバイトコードと記録を削除する
  ため、次回ビルドですべてのシェーダーを再コンパイルします。通常の C++ の
  Clean/Rebuild では、変更のない構成共通のシェーダー生成物は破棄されません。

出力を共有するため、シェーダーのビルドは同時に1つだけ実行してください。
ランタイムのホットリロードとカスタムポストプロセスシェーダーのコンパイル方法は
変更されていません。状態ファイルの JSON が無効な場合はエラーとして報告します。
再生成するには `-Force` を使うか、該当する記録を削除するか、シェーダーキャッシュを
クリーンしてください。

## プリコンパイル済みヘッダーとエンジン依存関係

`Source\Pch.h` に含めるのは、安定した STL、Windows/DirectX SDK、同梱 spdlog の
ヘッダーのみです。間接的なものも含め、エンジン機能のヘッダーを含めてはいけません。
特に `Core\Core.h`、GameObject/Component、Scene、Prefab、input、time、job-system の
ヘッダーを PCH から除外してください。これらを変更した際は、エンジンの全翻訳単位ではなく
利用側のみを再ビルドするようにします。

- エンジンの `.cpp` ファイルでは `#include "Pch.h"` を先頭に置き、その後に実装対象の
  ヘッダーと、使用する API の依存関係を明示的に含めます。
- エンジンのヘッダーは宣言、インライン実装、マクロの各依存関係を自らインクルードします。
  実装で定義が不要な非所有の不完全型には、前方宣言を優先してください。無関係な `.cpp`
  のインクルードや、以前の Core 一括ヘッダーからエンジン型が提供されることを前提にしないでください。
- `Core\Core.h` は、基盤 API 全体を必要とするクライアント向けの任意利用の便利ヘッダーです。
  エンジン実装ファイルでは個別のヘッダーを使用してください。将来 API を一括ヘッダーに
  追加しても、PCH が無効にならないようにします。
- コンパイラ設定、ABI、ランタイム動作、Debug/Release の構成選択に変更はありません。
  PCH/STL/SDK の変更では引き続き PCH 利用側全体を再ビルドします。この変更は機能ヘッダー
  編集時の再ビルド範囲を縮小するもので、クリーンビルドの高速化を保証するものではありません。

## リポジトリのディレクトリ

- `Source`: 公開拡張 API を含む、エンジンとエディターの C++ ソース。
- `Assets`: 制作用アセットと HLSL ソース。Scene、Prefab、Model を保持します。
- `Schemas`: 正式な FlatBuffers スキーマ。
- `Library`: サードパーティのソースとライセンス。上流のビルドスクリプト、テスト、
  ドキュメント、サンプルデータをエンジン生成の不要物として扱わないでください。
- `Tools`: 依存関係のビルドとオフラインのアセット生成。
- `Build`: 再生成可能な依存関係の出力とエンジンの中間ファイル。
  エンジン中間ファイルは `Build\GameEngine\x64\<Configuration>` にあります。
- `Generated\FlatBuffers`: 再生成可能なヘッダー。手動で編集しないでください。
- `x64\<Configuration>`: 実行ファイル、ランタイム DLL、デバッグシンボル。
- `Config`、`Log`、`.vs`: ローカルのエディター設定、ログ、IDE キャッシュ。
  設定は保持してください。IDE が使用中のキャッシュを削除しないでください。

### 生成物のクリーンアップ範囲

`Build` 以下で有効なビルドディレクトリは `GameEngine`、`PhysX`、`ozz-animation` です。
インクリメンタルビルド用にこれらを保持し、`Generated\FlatBuffers`、
`Assets\Shaders\Compiled`、実行ファイルの各ディレクトリも残します。
`x64\<Configuration>` には `GameEngine.exe`、その `.pdb`、`libfbxsdk.dll` を保持してください。

廃止されたエンジン検証スイートが残したのは、保守対象のソースやランタイムアセットではなく、
ローカル限定の出力です。該当するのは `Build\RenderingBaseline-*`、
`Build\Terrain*Validation-*`、`Build\source-layout-before-*`、
`Build\source-layout-after-*`、`Build\Tests` です。これらの過去のディレクトリと
ルート直下のビルド／プローブログは削除済みです。削除したスイートに由来する孤立した
`*Validation.obj` ファイルもエンジン中間ディレクトリから削除済みですが、プロジェクトの
コンパイル項目に対応するオブジェクトファイルは削除しないでください。

古い組み込み出力 `Deferred_psComposite_ps.cso`、
`Occlusion_vsMain_vs.cso`、`PostProcessValidation_psCustomGain_ps.cso`、
`PostProcessValidation_psScale_ps.cso` は生成も使用もされなくなったため、削除済みです。
現在の32個の組み込み出力とカスタムシェーダーのディレクトリは保持されています。
ファイル名がソース中にリテラルとして存在しないという理由だけで、カスタムシェーダーや
アセットが未使用と判断しないでください。

エンジンソース、`Core\Core.h` のような任意利用の公開 API、制作アセット、互換性用 reader、
設定、サードパーティのソース／ライセンス、ベンダーのサンプル／テストは生成物の不要物では
ありません。現在呼び出されていない公開 API も、クライアントや将来のコンポーネントが
利用する可能性があります。削除前に参照先と本来の所有者を確認してください。
Ignored または untracked であることだけを削除の根拠にしてはいけません。対象を特定して
調査し、junction を避け、リポジトリ全体や使用中の IDE キャッシュを一括クリーンしないでください。

登録済みサブモジュールは `git submodule update --init --recursive` で初期化します。
現在のローカル PhysX、ImGui、spdlog のチェックアウトには、親リポジトリに登録されていない
必須ファイルも含まれています。また ImGui と spdlog にはローカル変更があります。
これらは意図的に保持されています。依存ソースと変更を別途用意するまでは、新規 clone だけで
ビルド可能な状態にはなりません。

## Source の構成

```text
Source\
  Main.cpp, Pch.cpp, Pch.h       プロセスのエントリーポイントとプリコンパイル済みヘッダー
  Core\                        エンジン共通の基盤
    GameObject\                Component/Behaviour の基底 API、レジストリ、オブジェクト所有権
    Scene\, Prefab\            Scene と Prefab のライフタイム／永続化
    Serialization\             共通シリアライズ基盤
    System\                    Window、ダイアログ、起動、Play/Runtime の統括
    ...                        コンテナー、入力、ログ、数学、スレッド、時間
  Components\                  GameObject にアタッチする実装のみ
    Animation\                 Animator
    Audio\                     Audio source と listener
    Rendering\                 Camera、light、model renderer、post-process volume
    Physics\
      Character\               キャラクターコントローラーと移動
      Collider\                Collider の基底型と具体的な形状
      Joint\                   Joint
      Rigidbody\               Rigidbody
    Terrain\                   Terrain コンポーネント
    Transform\                 Transform コンポーネント
    UI\                        Canvas、rect transform、image、text、button
  Animation\Runtime\           コンポーネントではないアニメーション評価処理
  Assets\                      機能別のアセットデータ、インポート、マネージャー、シリアライザー
  Audio\                       オーディオのランタイムサービス
  Editor\
    Components\Camera\         エディター専用のフリーカメラコンポーネント
    ImGui\, PostProcess\, Scene\  エディター UI、インスペクター、シーンドキュメント
  Graphics\                    機能別のレンダリングサービスと GPU リソース
    DirectX12\
      Command\                 コマンドリスト、キュー、フェンス
      Culling\                 GPU オクルージョンカリング
      Device\                  D3D12 デバイス
      Diagnostics\             GPU プロファイリングとフレームキャプチャ
      Pipeline\                グラフィックス／コンピュートパイプライン
      Presentation\            スワップチェーン
      Rendering\               D3D12 レンダラーとモデルコマンド記録
      Resource\                リソースとディスクリプター
  Physics\                     Physics ワールド、システム、クエリ、共有データ
    Internal\                  PhysX 変換と共有 physics payload のヘルパー
  Terrain\
    Data\                      Heightmap の保存と画像読み込み
    Generation\                ビルドキュー、ビルド要求、ノイズ、GPU ノイズ、侵食
    Geometry\                  メッシュ生成、チャンク、LOD、サーフェスサンプリング
    Material\                  Terrain レイヤー設定とウェイト
    Serialization\             Terrain マテリアルのシリアライズ
    Vegetation\                植生レシピと配置
  UI\                          ランタイム UI のレイアウトと操作サービス
```

具体的なランタイムコンポーネントは `Source\Components\<Feature>` に配置し、
Physics のように機能内に明確な分類がある場合はサブフォルダーを設けます。
`Component`/`Behaviour` の基底型と登録処理は `Core\GameObject` に置きます。
これらはインフラであり、アタッチ可能な機能実装ではありません。
エディター専用コンポーネントは `Editor\Components\<Feature>` に配置します。

サービス、アセットリソース、レンダリングバックエンドは `Components` に置きません。
無関係なマネージャーやユーティリティを汎用フォルダーにまとめず、`Assets`、`Graphics`、
`Editor` 配下の既存の機能分割を維持してください。明確な責務がある場合に新しい機能
フォルダーを追加し、想定上の機能のために空のフォルダーを作らないでください。

次の例のように、Source ルートからの相対パスでインクルードします。
`#include "Components\Physics\Collider\BoxColliderComponent.h"`.
互換性用ヘッダーや機能別の追加インクルード検索パスは不要です。
配置を変更しても、クラス名、コンポーネント登録名、Scene/Prefab payload、
アセットパス、生成済み FlatBuffers ヘッダーは変わりません。

ファイルの追加／移動時は `GameEngine.vcxproj` と `GameEngine.vcxproj.filters` を
同時に更新します。すべての source/header/inl ファイルは明示的に列挙され、
各 Visual Studio フィルターは `Source` を含む実際のディレクトリ構成と一致します。
対応するヘッダーと実装はまとめて配置してください。複数機能にまたがる再編成後は
Debug/Release の両方をビルドします。

# Core のスケジューリングと入力の境界

`JobSystem` は、依存関係が解決済みのジョブのみを実行キューに入れます。
依存待ちのジョブが、別の依存待ちジョブを再帰的に引き受けることはありません。
そのため、依存側より後にキューへ入れた producer が、有効な依存チェーンを進行させられます。
外部依存の完了とキャンセルはワーカーが定期的に確認します。依存関係の循環は引き続き無効です。
ジョブは自身を含むカウンターを待機したり、自身のシステムに対して `waitForAll`、
`initialize`、`finalize` を呼び出したりしてはいけません。

スケジューリング／カウンター／待機処理はスレッドセーフで、公開されるワーカー数は atomic です。
カウンターとキャプチャしたコールバック状態は、投入済みジョブすべてより長く存続させる必要が
あります。スケジューリング時の割り当て失敗は伝播します。`parallelFor` は巻き戻す前に、
投入済みのジョブをすべて join します。ジョブコールバックの失敗はログに記録され、
アカウンティングは完了します。継続処理は従来どおり本体の失敗後も実行されます。

ジョブが保留中の間、`TaskGroup` は変更と重複実行を拒否します。グループ操作には外部同期が
必要です。`wait` はワーカー内を含め、実行可能なジョブの処理を手伝います。破棄時には
キュー済み作業をキャンセルし、実行中の作業を join してからグループの領域を解放します。
渡されたシステムはグループより長く存続する必要があり、タスク自身が所属グループを破棄または
待機してはいけません。並列チャンク内のタスク間でキャンセルが確認された場合、チャンク全体の
完了を待たずに反映されます。

`MainThreadDispatcher` はフレームをまたいでタスクバッファの容量を保持します。
dispatch 中に投稿されたタスクは次回の dispatch で実行されます。`dispatchPending` の
再帰呼び出しは拒否され、ログに記録されます。コールバックの例外は記録されますが、
後続コールバックは破棄されず、dispatch 状態が永続的に有効のままになることもありません。

`String` は、空でない null ソースと未使用容量への範囲指定を拒否し、以前の値を保持して
エラーをログに記録します。文字列リテラルの追加には曖昧さのないオーバーロードがあります。
容量／エンコードの失敗は Debug だけでなく Release でもログに記録されます。
UTF-16 変換は不正なサロゲート列を拒否し、割り当てに失敗した後で結果バッファへ書き込みません。

平滑化フレーム時間は起動時、記録済みサンプルのみを平均します。10サンプル取得後は、
従来どおり直近10フレームの移動平均を維持します。

# コンポーネントの削除

Inspector で任意コンポーネントのヘッダーにある **X** をクリックするか、ヘッダーを右クリックして
**Remove Component** を選択します。削除されるのはそのインスタンスのみです。
Transform を含む必須コンポーネントは削除できません。削除はコンポーネント列挙の完了後に行われ、
既存の disable/destroy ライフサイクルが呼び出されます。その後 Scene または Prefab を保存すると、
更新後のコンポーネント一覧が保存されます。

`GameObject::removeComponent(Component&)` は、追加待ちのものを含め、1つのインスタンスを削除します。
削除要求が成功した後は、その参照を使用しないでください。既存の `removeComponent<T>()` API は
引き続き、その型の全インスタンスを削除します。

# Runtime と Play

## Volumetric Fog

シーンに **Volumetric Fog** コンポーネントを1つ追加します。GameObject のワールド Y 座標が
フォグの基準高度です。Density と Height Falloff で高さによる指数密度を調整し、
Albedo、Emissive、Extinction Scale、Scattering Distribution、Start Distance、
Near Fade In Distance、View Distance、Maximum Opacity を設定できます。距離と高度は
エンジンのワールド単位です。Fog の有効化と設定は Scene／Prefab に保存されます。

Deferred HDR レンダラーで不透明物と Skybox に適用されます。Directional、Point、Spot Light は
それぞれの **Volumetric Scattering Intensity** で寄与を調整できます。Directional Light の
既存シャドウマップが使われる場合は、そのフォグ散乱にもシャドウを適用します。有効な
Volumetric Fog が複数ある場合は最初のものだけを使用します。

この実装は各ピクセルの視線をレイマーチするグローバル高さフォグです。サンプル数を増やすと
品質と GPU 負荷が上がります。Temporal Froxel Reprojection、ローカルな粒子ボリューム、
Point／Spot Light のボリュームシャドウ、Forward／透明描画への適用は未対応です。
Unreal Engine の内部実装や画質・性能との一致を保証するものではありません。

Deferred 照明、雲、フォグはフル解像度の Compute Shader で従来と同じ順序で評価します。
低解像度化や時間的な近似は行わないため、Compute 化だけでの高速化は保証されません。
HDR 照明ターゲットには `R16G16B16A16_FLOAT` の typed UAV store 対応が必要です。

## Sky Atmosphere

GameObject に **Sky Atmosphere** を追加します。そのワールド位置が惑星の中心になります。
**Directional Light** コンポーネントを1つまたは2つ追加し、**Atmosphere Sun
Light** を有効にして、それぞれ異なるインデックス 0 と 1 を割り当てます。
大気の空と太陽円盤を描画するには、カメラの clear mode を **Skybox** に設定します。
このコンポーネントは Rayleigh/Mie 散乱、Mie の異方性と吸収、オゾン吸収、高度別の
スケールハイト、惑星曲率、距離に応じた大気遠近を提供します。キロメートル単位の半径は
**World Units per km** を使って変換されます (メートル基準のワールドでは 1000)。
既定の地球半径を使う場合、ワールド Y=0 の地面に対し、大気の中心を Y=-6,360,000、
カメラを Y=0 のすぐ上に配置します。

大気はトーンマッピング前の Deferred HDR ライティング Compute Shader 内で評価されます。
空と不透明な Deferred ジオメトリに作用しますが、Forward レンダラーや透明マテリアルには
作用しません。有効な Sky Atmosphere コンポーネントのうち最初のものだけを使用し、
各太陽インデックスには一致する有効なライトのうち最初のものを使用します。
両コンポーネントは Scene と Prefab に保存されます。Atmosphere sun-light フィールド追加後も、
version 1 の Light payload を読み込めます。レンダラーは Unreal の事前計算ルックアップ
テクスチャではなく、固定ステップの単一散乱を使用するため、結果やコストは同一ではありません。

## Volumetric Cloud

地面の高さに置いた別の GameObject に **Volumetric Cloud** を追加し、カメラの clear mode を **Skybox**、
レンダラーを Deferred に設定します。GameObject の位置を基準とする水平な雲層を、
トーンマッピング前にレイマーチングします。Base Height と Thickness は km 単位で、
World Units per km は通常 1000 です。Coverage、Density、Noise Scale、Maximum Distance、
View Samples を Inspector で調整できます。設定は Scene と Prefab に保存されます。

雲の形状は手続き的な 3D ノイズで生成されるため、外部テクスチャは不要です。
Sky Atmosphere と Atmosphere Sun Light を設定すると、太陽光の散乱と雲の自己遮蔽を
近似します。不透明物体の深度で雲を遮ります。有効な雲コンポーネントが複数ある場合は
最初のものだけを使用します。水平な雲層は地上視点向けで、惑星曲率、雲から地表への影、
Sky Light の反射、透明物体との深度交差、Forward 描画には対応していません。
大気と雲の合成も近似であり、Unreal Engine と同一の見た目や性能を保証するものではありません。

## Light Shafts

Directional Light の **Light Shafts Bloom** を有効にすると、明るい HDR 画素をライト方向へ
放射状にサンプリングし、Bloom Scale、Bloom Threshold、Bloom Tint で光芒を調整できます。
**Light Shafts Occlusion** は Bloom のレイサンプルを深度バッファに応じて暗くします。設定は Light
コンポーネントとともに Scene／Prefab に保存されます。複数の Directional Light で有効にした
場合は最初のライトだけを使います。

Deferred レンダラーのトーンマッピング前に半解像度の Compute Shader で適用されます。
1出力画素あたり64回の色サンプルを行い、Bloom が有効で Occlusion も有効な場合は深度を参照する
画面空間効果です。一時画像には RGBA16F typed UAV store の対応が必要です。Occlusion 有効時、
深度の画素微分が描画範囲の外側に依存する画面端は Pixel Shader で補完します。光源が画面外に
ある場合も、従来の GPU ごとの微分結果を維持するため Pixel Shader を使用します。
このため、Compute 化による高速化は保証されません。
画面外の光源による効果は画面内の情報に限られ、Forward レンダラーや透明物体による遮蔽には
対応しません。Unreal Engine の実装や画質・性能との一致を保証するものではありません。

プロジェクトのルートから起動します。

```powershell
.\x64\Debug\GameEngine.exe
.\x64\Debug\GameEngine.exe --runtime --scene ".\Assets\Scenes\My Game.scene"
.\x64\Debug\GameEngine.exe --help
```

## デバッグプリミティブの視錐台カリング

デバッグプリミティブは、描画時のカメラ行列から作った視錐台で線分と各形状の境界を判定し、
完全に視界外の要求を頂点展開前に除外します。視錐台と交差するものは描画を維持します。
球・箱・円柱・カプセルは共通の形状頂点を再利用し、行列、寸法、色をインスタンスごとに転送します。
任意線分とグリッドは従来どおり線分頂点として描画します。物理デバッグの線分要求は一括提出します。
物理メッシュと凸メッシュは、同じフレームの Camera snapshot にジッターが加わらない場合、
形状全体の境界が視界外なら線分生成を省略します。Temporal AA が有効な Camera では、
ジッターによる表示欠落を防ぐため事前判定を行わず、描画側の視錐台判定を使用します。

## PostEffect Phase 1 の実行仕様

`DX12Renderer::setPostProcessPasses` は、フレーム間にレンダースレッド上で
`PostProcessPassDescription` snapshot の順序付きリストをコピーします。
各定義は不変のコンパイル済みピクセルシェーダー snapshot と16個の float パラメーターを所有します。
設定が無効な場合はログを記録して false を返し、現在のリストを置き換えません。
置き換えが成功すると、最後の GPU 使用完了を待って不要になった一時ターゲットを解放します。 共有シェーダーは snapshot の存続中に変更されません。バイトコードを置き換える場合は、
新しい snapshot／リストを送信してください。

現在の Deferred カラーパスは次のとおりです。

```text
G-Buffer -> HDR ライティング -> 透明ジオメトリ
         -> トーンマッピング前のカラー処理
         -> Bloom -> OCIO (トーンマッピング前を選択した場合)
         -> 手動／自動露出 + ACES 2 SDR 出力変換 (線形 HDR -> 線形 LDR)
         -> トーンマッピング後のカラー処理
         -> OCIO (トーンマッピング後を選択した場合) -> ビネット -> 色収差 -> FXAA -> フィルムグレイン
         -> sRGB エンコード -> デバッググリッド／Runtime UI／エディター UI -> 表示
```

どちらの段階も、定義がリスト内のどこにあったかにかかわらず、段階内の登録順を維持します。
任意ターゲットはすべて linear RGBA16F です。`resolutionDivisor` (1-16) は、全ターゲット寸法を
切り上げ除算して使います。縮小時のカメラ境界は一時テクセル全体を覆い、サンプリング範囲は
ソースの viewport 内に制限されます。フルサイズのパスでは元のカメラ境界を厳密に維持します。
ターゲットが同時に入力と出力として使われることはありません。
返却されたテクスチャは同じコマンドリスト内で後から再利用できますが、フレームをまたぐ再利用と
破棄は、対応する scene-submission fence の完了を待ちます。Forward、カメラの clear mode が
`Nothing` の場合、G-Buffer デバッグビューではリストを迂回します。

有効な LDR パスや組み込み LDR エフェクトがない場合、トーンマッピングと sRGB エンコードは
最終出力パスに統合されます。そのため、空またはすべて無効なリストではスクラッチテクスチャを
追加しません。露出は1回だけ適用されます。
LDR エフェクトが有効な場合、トーンマッピング後の中間画像は sRGB ではなく linear half-float です。
表示時のアルファは 1 です。`captureFrame` は任意エフェクト適用**前**のシーン HDR を保持し、
UI 描画後に最終表示をキャプチャします。Forward のキャプチャには HDR 画像がありません。

ピクセルシェーダーは `Assets/Shaders/PostProcess.hlsli` をインクルードし、
`PostProcessVertex` を使って `loadInputColor(input.position.xy)` を呼び出します。
ルートバインディングは `t0` (入力色)、`s0` (linear clamp)、`b0` (ソース／ターゲット viewport、
露出、変換フラグ)、`b1` (4つの `float4` パラメーターベクトル) です。組み込みシェーダーは
さらに `t1` (副入力色) と `b2` (その viewport) を使います。従来の単一入力シェーダーの
バインディングは変更されていません。フル解像度で viewport が同じ場合は、正確なコピーを
維持するため整数テクスチャ読み込みを使います。`ColorOutput.hlsli` は sRGB 変換を共有します。
必須出力パスはさらに `t3`／`t4` に ACES 2 の内部 LUT、`s1` に point clamp を使います。
G-Buffer の Emission デバッグ表示は出力変換を通さず、線形値を 0–1 に制限して表示します。

### OCIO GPU パスの低レベル登録

`DX12Renderer::setPostProcessOcio` は、呼び出し側でコンパイルした OCIO ピクセル
シェーダーと、OCIO が生成した 1D／2D／3D LUT の値を登録します。`nullptr` で解除できます。
設定はフレーム間にレンダースレッドから行ってください。失敗時は既存設定を保持します。

シェーダーのラッパーは入力画像に `t0`、入力用 linear clamp sampler に `s0`、通常の
PostEffect フレーム定数に `b0`、16 float のパラメーターに `b1` を使用します。LUT の
テクスチャ／sampler レジスターはコンパイル済みシェーダーの実際の割り当てと一致させて
ください。`t0` は LUT に使用できません。`s0` を共有する LUT は linear sampler に限ります。
複数 LUT が同じ sampler を共有する場合、補間方法は一致している必要があります。
LUT は最大38枚、値は有限の R または RGB float 配列で、X が最も速く変化する順序です。
レジスターはすべて space 0 を使用します。OCIO ラッパーの `b1` は `float4` 4個であり、
`float4` 12個の `b1` を宣言する標準 `PostProcess.hlsli` とそのまま共用できません。
シェーダーは `b0` のソース／ターゲット viewport が異なる場合も正しくサンプリングする必要があります。

有効な Camera PostEffect の weight と登録時の contribution で変換結果をブレンドします。
LUT の初回転送は描画コマンドに記録され、再設定・解除・終了時には使用中の GPU 完了を
待って PSO、テクスチャおよび descriptor を解放します。Shader のリビルド時にも OCIO の
PSO を再構築します。
`PostProcessCameraSnapshot::ocio` に不変の設定を渡すと、その Camera の描画では共通登録より
Camera 固有の設定を優先します。Camera 間で設定が変わる場合は前回の GPU 完了を待って
固有リソースを置き換えます。スナップショットが空の場合は共通登録を使用します。

### OCIO アセットの生成と使用

ビルド前に vcpkg の manifest モードで `x64-windows-static` の依存関係をインストールします。
OpenColorIO のバージョンはルートの `vcpkg.json` に固定されています。Editor の Camera または
Volume Profile の Post Effects で **OpenColorIO** を開き、入力・出力色空間と必要に応じて
Look、適用段階を指定して **Generate OCIO Asset** を押します。`.ocio` 設定ファイルを選び、
生成先の `.ocioasset` を保存してください。既存の成果物は **Open OCIO Asset** で選択できます。
設定選択画面では拡張子を限定しません。次の保存画面は生成先の `.ocioasset` 用です。
選択時にアセットと有効スイッチが override されます。Intensity と Camera／Volume の weight
は変換前後の画像をブレンドします。

トーンマッピング前は線形 HDR、後は sRGB エンコード前の線形 LDR を入力とします。
複数 Volume のアセット参照は優先順位順に最後の有効な override が選ばれ、異なる変換間を
補間するものではありません。Profile／Camera は保存済み成果物のパスを参照します。Runtime に
その `.ocioasset` が必要ですが、元の config、参照 LUT、DXC は不要です。config の変更後は
アセットを再生成してください。OCIO の動的 GPU uniform は静的アセットでは対応せず、
生成時にエラーになります。旧 Color Grading／LUT の設定は移行されません。

`Assets/Shaders/OCIO/NeutralGame.ocio` は SDR 向け Look のサンプルです。Source と
Destination に `Linear`、Look に `Neutral` または `Cinematic` を指定し、After Tone Mapping を
オフにして生成してください。`Neutral` は線形 HDR で彩度を少し抑え、無彩色は変えません。
`Cinematic` は影をわずかに青寄り、明るい部分を暖色寄りにし、コントラストを高めます。
トーンマッピングと sRGB エンコードはエンジン側で行うため、この設定には含まれません。
これらの Look 自体に LUT は不要です。別の config から LUT を参照する場合は、生成時に LUT も必要です。

同じ config の `SlayFilm` は写真向け LUT を使用します。Source／Destination を `Linear`、
Look を `SlayFilm` にして、**After Tone Mapping をオン**にして生成してください。LUT の
入出力は sRGB 相当と仮定し、Look 内で線形 LDR との変換を行います。元 LUT は入力色空間を
明示していないため、実際のシーンで仕上がりを確認してください。LUT は生成時に読み込まれ、
`.ocioasset` に内包されます。LUT の変更後は再生成が必要です。

LUT: [slay](https://www.slaycamera.com/) の
[slay Film](https://github.com/slaycamera/luts/blob/a1709044efe115640dde1dee17d4185fc059af0d/luts/slay-04-film.cube)。
原本を無変更で使用しています。ライセンスは
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) です。

これは意図的に、フル機能のレンダーグラフではなく、色のみを扱う小規模なグラフィックスパス基盤です。
深度／法線入力、コンピュートパス、履歴、カスタムシェーダーアセット／ホットリロードの追跡は
後続フェーズで扱います。Profile の編集機能は以下の Phase 2 で別途実装されています。
共通出力シェーダーは既存の ShaderManager リロード処理に参加し、ユーザー提供の snapshot は
明示的に置き換えます。

## PostEffect Phase 2 Profile と Camera の編集

Inspector で **Camera** を選択して **Post Effects** を展開します。Post Effects の設定は
Profile と Camera のローカルオーバーライドで共有され、Deferred レンダリングで実行されます。
露出は Post Effects の **Exposure** で設定します。診断／カスタムパス API も利用できます。

- **Enable Post Effects** は、このカメラで Profile レンダリングを有効にします (既定では無効)。
  **Weight** は `[0,1]` の有限な寄与率です。Phase 3 では中立値から各パラメーターをブレンドします。
  FXAA のみ例外で、元の linear LDR 画像とフィルター後の画像をブレンドします。
- **New Profile**、**Open Profile...**、**Shared Profile** で共有アセットを作成／読み込み／選択します。
  **Edit Shared Profile** は公開済みの不変 snapshot を更新し、参照するすべてのカメラに影響します。
  **Make Unique** は元アセットを変更せず、新しい未保存 GUID を作成してこのカメラに割り当てます。
- **Save Profile** は既存ファイルへアトミックに書き込みます。**Save Profile As...** は
  `.postprofile` を書き込み、そのアセットの登録パスを更新します。**Reload
  (discard edits)** は、検証が成功した場合に限り、ディスク上の値で編集内容を明示的に置き換えます。
  Inspector は未保存の編集があることを表示します。
- 各パラメーター左側のチェックボックスは **override** を意味します。未チェックの値は定義値を
  保持しつつ、直前のレイヤーの値を継承します。エフェクトの Enabled スイッチもオーバーライド
  できるため、Profile で有効なエフェクトをカメラ側から明示的に無効にできます。
- **Camera Overrides** は共有アセットの編集ではなく、独立してシリアライズされる値です。
  **Reset Overrides** は中立の既定値に戻し、オーバーライドフラグを解除します。
  解決順序は engine defaults -> enabled Profile overrides -> enabled local parameter overrides です。
  Phase 4 ではこれら Camera レイヤーの前に Volume をブレンドします。

`PostProcessProfileManager` は世代確認付きハンドルを使い、不変の
`shared_ptr<const PostProcessProfile>` snapshot を所有します。Profile 更新で古い snapshot は
変更されません。CPU 専用の `RenderView::postProcess` には解決済みの値、カメラの有効状態と
weight が入り、マネージャーハンドルや GPU リソースは含まれません。後続の編集でキュー済み
render view が変化することはありません。固定露出は EV 段、contrast/saturation は百分率、
色は **linear RGB** で表します。スカラー／色の範囲は Inspector と設定検証で共通です。

Scene/Prefab の Camera payload v7 は、enable、weight、ローカル値／フラグ、Volume layer mask、
Profile GUID と UTF-8 でエンコードした正規化済み絶対パスを保存します。既存の v1 Camera と
従来の空 payload は、中立の PostEffect 既定値で引き続き互換性があります。プロジェクトの起動に
新しい Profile アセットは不要です。GUID 不一致、古い参照、未対応バージョン、非有限／範囲外の値、
ファイルの切り詰めは黙認せず、診断情報を出します。同じ GUID の別パス登録はマネージャーが
拒否します。独立した ID を作るにはファイルをコピーせず **Make Unique** を使ってください。
エディター外でファイルを移動した場合は Profile の再割り当てが必要です。このフェーズでは
アセットデータベースや自動再配置は導入しません。

参照先 Scene/Prefab を保存する前に、新規 Profile を保存してください。Scene ファイルに保存されるのは
参照であり、共有アセットの編集内容ではありません。編集内容を永続化するには Profile を別途保存します。
初回 Profile 保存前でもメモリ内の Play 複製は動作します。不変アセットのみを共有し、ローカル
オーバーライドは独立して保持されます。既存エディターと同様、Play 中は Inspector 編集が無効です。

## PostEffect Phase 3 の組み込みエフェクト

Camera Weight が 0、カメラが無効、またはエフェクト設定が中立の場合は、描画1回・割り当て0回の
基本経路を維持します。LDR パスが不要な場合、露出と色調整は表示変換に統合されます。
設定変更によって PSO の再構築や GPU fence 待ちは発生しません。一時リソースは既存の
fence 対応プールで管理します。Forward、`Nothing` clear mode、G-Buffer デバッグビューでは
カメラエフェクトを迂回します。後処理後も UI とエディターグリッドを描画します。
アクティブ Camera を削除すると、その PostEffect snapshot はクリアされます。

- **Bloom** は川瀬式のマルチスケール方式です。初期パスはBT.709輝度でハードしきい値を適用し、
  13サンプルのKarisアンチファイアフライ重みで1/2縮小します。以後は各段で分離9タップGaussian
  ブラーをかけながら最大5段階まで切り上げ半分縮小し、正規化した2x2線形サンプルで低解像度側から
  順にアップサンプル・加算します。最後もtentフィルターで元画像へ加算します。しきい値は露出前の
  scene-referred BT.709輝度で評価し、`-1` は全ピクセルを含めます。Size Scale はGaussianの
  サンプル半径、Scatter は各低解像度レベルの加算寄与、Intensity は最終HDR加算の強度を制御します。
  各段階は Compute Shader で実行し、RGBA16F の typed UAV store に対応する GPU が必要です。
  一時画像は既存の fence 対応プールで管理し、描画回数とは別に dispatch 回数を記録します。
- **Exposure** の Mode は Off、Manual、Automatic から選びます。Manual は `exp2(EV * Weight)`、
  Automatic は測光結果から求めた EV を使います。Volume の補間中は両モードを連続的に切り替えます。
  設定値はモードとは別にオーバーライドできます。極端な EV でのオーバーフローを防ぐため、
  トーンマッピング入力は RGBA16F の有限最大値に制限されます。
- **ACES 2 SDR** は OpenColorIO 2.6.0 の公式組み込み CG config
  `cg-config-v4.0.0_aces-v2.0_ocio-v2.5` にある `Linear Rec.709 (sRGB)` から
  `sRGB - Display` の `ACES 2.0 - SDR 100 nits (Rec.709)` View への変換です。
  手動または時間適応型の露出は入力に一度だけ適用されます。最終出力は HDR10 ではなく
  8-bit SDR です。LDR エフェクトがある場合は View の表示値を sRGB デコードして線形 LDR に戻し、
  エフェクトの後に一度だけエンコードします。LDR エフェクトがない場合は View の表示値を直接出力します。
  内部 LUT はコンパイル済みシェーダーと共にエンジンへ組み込まれるため、Runtime での
  OCIO 設定ファイル、DXC、外部 LUT の読み込みは不要です。
  この出力変換の生成元は `Tools/Shaders/GenerateAcesOutput.cpp` です。OpenColorIO 2.6.0 の
  ライセンスは `Assets/Shaders/OCIO/OpenColorIO-LICENSE.txt` に記載し、ビルド先にもコピーします。
  このファイルは配布に必要なため、フォルダ整理時にも削除しないでください。
- **OpenColorIO** は生成済み変換を HDR または線形 LDR に適用します。

- **Vignette** はトーンマッピング後に linear RGB 色をブレンドします。Intensity はゼロから
  ブレンドし、smoothness は放射状減衰を制御します。roundness は viewport 正規化距離と
  アスペクト補正距離を混合します。
- **Chromatic Aberration** は Vignette 後に viewport 中心から放射状に赤／青チャンネルを
  分離します。Start Offset は効果開始半径を制御し、サンプリングはアクティブ Camera の
  viewport 内に制限されます。
- **FXAA** は知覚輝度で方向性エッジを検出し、端点検索範囲を制限します。エッジ再構成と
  サブピクセル再構成を組み合わせ、フィルター済み **linear LDR** 結果を Weight でブレンドします。
  浮動小数点演算の contraction 差があっても近いエッジを安定させるため、輝度比較には12ビットの
  小数精度を使用します。
  sRGB エンコードは最終段階のみです。
- **Film Grain** は FXAA 後の linear LDR に、フレームごとに変化する hash noise 粒子を加えます。
  Intensity は振幅を、Response は暗部へ適用する重みを制御します。
  Seed はエフェクトが有効なフレームでのみ進み、shader 定数を通じて正確に渡されます。

ACES 2 の生成済みシェーダーと LUT を再生成する場合は、依存関係を復元した後、
リポジトリルートの x64 Native Tools Command Prompt で次を実行します。

```bat
if not exist Build\Aces2 mkdir Build\Aces2
cl /nologo /std:c++20 /EHsc /MT /I"vcpkg_installed\x64-windows-static\include" /Fo"Build\Aces2\GenerateAcesOutput.obj" /Fe"Build\Aces2\GenerateAcesOutput.exe" "Tools\Shaders\GenerateAcesOutput.cpp" /link "vcpkg_installed\x64-windows-static\lib\*.lib" bcrypt.lib gdi32.lib user32.lib
Build\Aces2\GenerateAcesOutput.exe .
```

`Assets/Shaders/Aces2Output.hlsli` と `Source/Graphics/PostProcess/Aces2Luts.h` は同時に
生成されるため、片方だけ編集・更新しないでください。通常のビルドでは再生成は不要です。

すべての組み込みシェーダーは ShaderManager の依存関係／ホットリロード追跡に参加します。
再ビルドでは候補 PSO をすべて構築してから、既存パイプラインを置き換えます。
Bloom の複数入力パスで出力をいずれかのソースとエイリアスさせることはありません。
サンプリングは Direct3D の暗黙の viewport scissor (右端／下端を floor) に従うため、
小数 viewport でも一時ターゲットの未描画領域である右端／下端の行をサンプリングしません。

## PostEffect Phase 4 の Volume

GameObject に **PostProcessVolume** コンポーネントを追加し、Inspector で共有
`.postprofile` を割り当てるか作成します。Camera と同じ Profile の open/edit/save/reload/
Make Unique 操作を使えます。physics collider や physics update は不要です。
1つのオブジェクトに追加できる Volume コンポーネントは1つです。Camera の
**Enable Post Effects** が全体の有効／無効を制御し、**Volume Layer Mask** は Volume
GameObject Layer に対する独立した16進数32ビットマスクです。マスク `00000000` は Camera
Profile/Overrides を無効にせず Volume のみを除外します。

エンジンが描画する Scene と同様に、アクティブ Scene と persistent Scene のみが対象です。
無効なコンポーネント、非アクティブな階層、破棄待ちのオブジェクト／祖先は除外されます。
Profile が未割り当ての場合は寄与しません。古い Profile ハンドルは一度だけ診断を出して
スキップし、削除や Play 中の Scene 切り替え後にコンポーネント登録が残ることはありません。

### 形状と適用順

- **Global** はどこでも Weight を適用し、Transform は無視します。
- **Box** はローカル中心と正のローカル全サイズを使います。階層、回転、非一様／反転スケールを
  含むエンジンのワールド TRS に従います。減衰には角を含む向き付き Box までの
  ワールド空間距離を使います。
- **Sphere** はローカル中心と正のローカル半径を使います。ワールド半径はローカル半径に
  ワールドスケール軸の絶対値の最大値を乗じた値です。
- **Blend Distance** は形状の**外側**に設けるワールド単位の境界帯です。形状の内側／境界上では
  Volume Weight 全体が影響します。外側では
  `Weight * smoothstep(0,1,1-distance/BlendDistance)` により滑らかにゼロへ減衰します。
  Distance が 0 の場合は意図的に明確な境界になります。無効／退化したローカルワールド変換は
  診断を出して、復旧するまでスキップします。

小さい **Priority** から適用します。同じ優先度は persistent GameObject GUID 順、
Scene をまたぐ場合は Scene GUID 順で決まります。列挙順やメモリアドレスには依存しません。
適用順は次のとおりです。

```text
neutral defaults -> spatial Volumes (Priority/GUID)
                 -> Camera Profile overrides -> Camera local overrides
                 -> global Camera Weight -> built-in GPU effects
```

オーバーライドのチェックが有効なパラメーターのみが対象です。数値／色の値は Volume の影響度で
補間され、Enabled スイッチは 0 から 1 までの別個の連続寄与率で補間されます。
未チェックのスイッチは直前の寄与率を保持します。優先度の高いレイヤーで明示的に無効にすると、
直前のエフェクトは突然置き換わらずにフェードアウトします。

数値と Enabled の補間は**独立**しており、その後に Camera Weight を一度だけ適用します。
たとえば中立設定から、Enabled=true、EV=4 の露出 Profile を Volume の影響度 0.5 で適用すると、
EV=2、Enabled の寄与率は 0.5 になります。さらに Camera Weight=1 を適用した露出は EV=1 です。
Enabled が寄与率 1 で継承されている場合は、数値だけが補間されます。Bloom/Vignette の強度、
露出 EV、色調整にはそれぞれ連続した寄与率を使い、FXAA は未処理画像とフィルター済み画像を
ブレンドします。時間履歴やカメラ依存の GPU 設定を追加せず、Boolean の境界で生じる急な変化を
避けます。

### 永続化と検証

Volume payload v1 は形状情報、priority、weight、保存済み Profile GUID／UTF-8 パスを保存します。
Camera v3 では Volume mask が追加され、v0/v1/v2 は全レイヤー対象のマスクに復元されます。
Scene/Prefab の保存前に参照先 Profile を保存してください。共有 Profile の編集内容が暗黙に
保存されることはありません。無効な payload、GUID／パス不一致、保存失敗の場合は以前の
設定／出力を維持します。メモリ内の Play 複製では未保存 Profile を使え、不変アセットを共有しつつ
Camera と Volume の設定は独立して複製されます。既存 Scene に新しいアセットは不要です。

各 Camera は CPU 専用の収集用ワークスペースを再利用します。コンポーネント／アセットへの
ポインターが RenderView に渡ることはなく、後続の Profile／Transform 編集でキュー済み snapshot
が変更されることもありません。無効または weight がゼロの Camera は Volume の収集を省略します。
割り当て失敗は明示的に診断され、そのフレームの Camera エフェクトを無効にします。
Volume の変更で PSO を再構築したり GPU 待ちを強制したりすることはありません。

## PostEffect Phase 5 のカスタム HLSL Effect アセット

Camera または Volume の共有 Profile Inspector から **Custom Effect** を作成します。
HLSL ソースファイルを選び、エントリーポイントと linear color stage を設定して
**Apply & Compile** を実行します。保存した `.posteffect` にはアセット GUID、ソースパス、
エントリーポイント、stage、resolution divisor、パラメーターメタデータが格納されます。
Profile はこれらのアセットへの順序付き参照と、有効状態、パラメーター値を保存します。
参照する Profile を保存する前に Effect アセットを保存してください。Profile format v2 で
カスタム Effect が追加されました。version 1 の Profile は空の Effect リストで引き続き読み込めます。

Custom Effect は既存のフルスクリーン Root Signature と
`Assets/Shaders/PostProcess.hlsli` の契約を使います。独自の root signature、リソース、
サンプラーは宣言しません。Pixel Shader は `PostProcessVertex` を受け取り、
`float4 : SV_Target0` を返し、`loadInputColor` で入力をサンプリングします。
また `parameters[0..3]` から最大16個のスカラー定数を読み取ります。Inspector には、
アセットで宣言されたパラメーターのみが設定範囲とともに表示されます。ソースでは
`PostProcess.hlsli` をインクルードするか、次の例のように b0/b1/t0/t1/b2/s0 の
バインディングに正確に合わせます。

```hlsl
#include "PostProcess.hlsli"

float4 psTint(PostProcessVertex input) : SV_Target0
{
    const float4 source = loadInputColor(input.position.xy);
    return float4(source.rgb * parameters[0].x + parameters[0].yzw, source.a);
}
```

対応するプロジェクトサンプルは `Assets/Shaders/PostProcessExample.hlsl` です。
入力色の領域はアセットの stage に従います。**Before Tone Mapping** は線形 HDR、
**After Tone Mapping** は最終 sRGB エンコード前の線形 LDR です。
解像度の除数は 1-16 に制限されます。カスタムシェーダーの出力は Volume の影響度と
Camera Weight を合成した値で直前の画像にブレンドされるため、形状境界が連続し、
入力アルファも保持されます。Profile のエフェクトリストは定義順に適用されます。
Volume のリストは既存の Priority/GUID 順に適用し、その後に Camera Profile のリストを適用します。
Camera のローカルオーバーライドは組み込み設定のみを対象とします。

Effect の作成時または **Apply & Compile** の実行時に DXC でコンパイルします。
成功したバイトコードを新しい不変 Shader snapshot として読み込んでからアセットを更新します。
コンパイルに失敗した場合は、以前の有効な snapshot を保持します。Runtime の読み込みでは
アセットが参照する生成済み `.cso` を使い、DXC は起動しません。プロジェクトをパッケージ化／
移動する際は、生成された `Assets/Shaders/Compiled/PostProcessCustom` のバイトコードも
アセットと一緒に保持してください。再コンパイルには一意なキャッシュファイルを使うため、
descriptor 保存に失敗しても、最後に保存したアセットが参照するバイトコードは置き換わりません。

エフェクトは組み込みパスと同じ線形 RGBA16F の一時ターゲット、および固定の
b0/b1/t0/t1/b2 バインディングを使用します。カスタムシェーダーからシーン深度、
モーションベクトル、任意のテクスチャ、UAV、時間履歴にはアクセスできません。
これらは後続のエフェクト段階で扱います。コンパイル済み PSO は不変 Shader snapshot ごとに
キャッシュされ、共有フルスクリーン頂点シェーダーの変更時にはトランザクション方式で再構築します。

## PostEffect Phase 6 の深度、時間的エフェクト、自動露出

被写界深度はアクティブ Camera の線形化済み Deferred 深度バッファを使います。焦点境界を
またぐ色のにじみを抑えるため、深度 weight 付きの円盤サンプリングを行います。
時間的アンチエイリアスは Deferred projection を jitter させ、G-Buffer の motion vector を使って
Camera ごとの HDR 履歴を再投影し、深度で disocclusion を排除したうえで、現在の 3x3 色近傍に
履歴を clamp します。Motion blur は同じ velocity buffer に沿ってサンプリングし、深度の不連続を
またぐサンプルを除外します。どちらの時間的エフェクトも Deferred レンダリングが必要です。
`CameraComponent::resetTemporalHistory` で Camera cut による履歴を無効化できます。
リサイズ、カメラ切り替え、TAA 無効化でもリセットされます。Camera の near/far plane と
projection type は不変の PostEffect snapshot に保存されます。

自動露出は、Camera viewport 内の画素を重み付き縮小チェーンで集計し、現在フレームの HDR 対数平均輝度を計算します。
輝度の集約は Compute Shader で行い、16×16 画素ごとに従来の 2×2 縮小を4段まとめます。
一時画像には RGBA32F typed UAV store の対応が必要です。Compute 化による高速化は保証されません。
奇数サイズや分割 viewport でも各画素を一度ずつ評価し、縮小端の画素を過大評価しません。
この値と key、compensation、最小／最大値から EV を算出します。Phase 7 では明るくする速度と暗くする速度を
分けた、Camera ごとの時間適応が追加されます。算出値は HDR 縮小と Bloom の後、トーンマッピング時に
適用されます。

旧 PostEffect の画面空間 Fog はランタイム設定と描画処理から削除されています。後続フィールド ID を
変更せず古い Profile を読み込めるよう、FlatBuffers のフィールドは予約 wire slot として保持します。
Profile 保存時にはこれらの旧 slot に中立値を書き込みます。シーン単位の Volumetric Fog は
別の Rendering Component として実装されています。

## PostEffect Phase 7 の時間的露出履歴

自動露出は GPU 上の適応済み EV を ping-pong 1x1 履歴テクスチャに保持します。各フレームで
現在の対数平均 target に向けて、次の式で近づけます。
`1 - exp(-speed * deltaTime)` で目標に近づけます。EV が上昇する場合は明るくする速度を、
EV が低下する場合は暗くする速度を選びます。delta が 0 の場合、有効な履歴は固定されます。
初回サンプルは直ちに初期化されるため、起動時に任意の露出からフェードすることはありません。
履歴はアクティブ Camera の GameObject GUID に関連付けられ、Camera の切り替え、Camera cut、自動露出の停止、
render target の解放／リサイズ時に無効化されます。これにより readback の停滞や、別 Camera への
古い露出の漏れを防ぎます。

2つの速度は Profile/Volume/Camera で編集可能な設定で、露出上昇は毎秒 3.0、露出低下は毎秒 1.0
が既定値です。Phase 7 のフィールドを持たない古い Camera v4 と Profile v3 payload は、
これらの既定値で読み込みます。Phase 8 で追加された Bloom Size Scale がない Phase 7 の
Camera v5 と Profile v4 payload も、その既定値で読み込みます。delta time はアクティブ Camera の
simulation update から取得するため、simulation を一時停止すると露出履歴も停止します。

## Runtime と Play の動作

Runtime の起動には保存済み Scene が必要です。引数が無効、または Scene を読み込めない場合は
ログを出力し、0 以外のコードで終了します。Runtime はコンパイル済みシェーダーを読み込み、
ImGui、エディターパネル、エディターグリッドを使わずウィンドウへ直接描画します。
これは配布用ビルド／パッケージングシステムではありません。

エディターのメニューバーで **Play** を押すと、管理対象の全 Scene と persistent object を
メモリ内に複製します。ゲームプレイのコールバック、physics、アニメーションは Play 中のみ
実行されます。**Pause** は描画を継続したままゲームプレイとシミュレーション時間を停止します。
**Step** は Update を1回、固定時間の physics/FixedUpdate を1回、LateUpdate を1回進めた後、
一時停止状態に戻ります。**Stop** はランタイムオブジェクトを破棄し、元のエディター Scene、
persistent object、カメラ、選択状態を復元します。Play 中にエディターを閉じた場合も、
分離セッションを破棄します。

編集中は、アクティブオブジェクト上で有効な Animator が、ゲーム Play を開始せずに
スケールされていないフレーム時間でアニメーションをプレビューします。Animator Inspector の
Play/Pause、クリップ選択、速度設定でプレビューを制御します。アクティブ Scene と persistent
object の両方をプレビューします。ゲームの Play/Pause/Step は従来のシミュレーション時間を
維持し、ゲーム Play を一時停止してもエディターのプレビューは進行しません。

Play 中は Scene の編集／保存と Transform gizmo が無効になります。ゲームプレイで
keyboard/mouse/gamepad 入力を受け取るには **Game** view をクリックします。別パネルを選択するか
ウィンドウのフォーカスを失うと、一時入力をクリアして振動を停止します。
エディターカメラの移動は編集中のみ動作します。アニメーションプレビューはゲームプレイの
Awake/Start/Update/FixedUpdate/LateUpdate コールバックを実行せず、physics actor も有効化しません。

コンポーネントは `Component::copyConfigurationTo` を通じて設定を複製します。
既定ではバージョン付き payload のシリアライズを使います。未保存のメモリアセットを参照する
設定では、この関数をオーバーライドしてください。複製間で可変ランタイム状態を共有してはいけません。
GameObject GUID 参照は、ライフサイクルコールバック開始前に、構築後の全複製 Scene を横断して
解決されます。不変の model、material、animation、physics アセットは共有されます。
Play はグローバルアセットの編集、ゲームプレイが書き込んだファイル、static 変数を元に戻す
undo トランザクションではありません。

Camera 設定と Main Camera の優先設定は、バージョン付き FlatBuffers payload に保存されます。
従来の空 Camera payload は既定設定を維持します。

## FBX アニメーションのインポート

FBX animation stack は秒単位で 60-120 Hz にサンプリングされます。各 stack 内の全チャンネルは
同じサンプリングスケジュールを共有します。float と ozz の `time * inverseDuration` 比率へ
変換した後もキー時刻は厳密に増加します。直前のグリッドサンプルが同じ float 時刻または正規化比率に
丸められる場合でも、最後のキーは FBX の正確な終端を一度だけサンプリングします。
アニメーション変換失敗時はクリップを報告し、キー時刻が無効な場合は joint、track type、
key index、直前の時刻、duration、正規化比率も報告します。無効なクリップはモデルから黙って
除外せず、拒否します。
すべてのアニメーションレイヤーにある平行移動、回転、スケールのカーブでは、入出力微分値が
有限でない三次補間区間を、インポート後の Scene 内に限って線形補間へ変更します。
修復した各カーブについて、クリップ、ノード、コンポーネント、区間数、最初に影響する時刻を
示す警告を出力します。有限値の三次補間区間と定数／線形補間は変更しません。
元の FBX ファイルも書き換えません。キー値またはサンプリング後の変換値が有限でない場合は、
単位変換に置き換えずインポートを失敗させます。
クリップ変換では、有限でない／無効な回転、および ozz が扱える有限な半精度範囲を超える
平行移動／スケール値も拒否します。Runtime 評価で有限でない、または特異な skinning palette を
公開することはありません。代わりに最後に有効だったポーズを保持してエラーを記録します。
以前にインポートしたアニメーションデータを修復するには、エンジン更新後に FBX を再インポート
してください。保存済みクリップは自動修復されません。

再サンプリングの前に、FBX SDK のフィルターを使って Euler 回転カーブをアンラップし、
各ノードの回転順序を考慮します。これにより、359度から0度への折り返しが意図しない一周として
補間されることを防ぎます。欠落した軸にはチャンネルの既定値を使い、Quaternion 補間のノードには
Euler フィルターを適用しません。アンラップが必要な場合、SDK は user/break tangent を
auto tangent として再計算し、インポーターは影響を受けたノードを記録します。
これは連続する最短の Euler 経路を使うため、隣接キー間で180度を超える意図的な回転には
中間キーが必要です。接線の修復と同様、この修正を反映するには保存済みアニメーションデータを
再インポートしてください。

## アニメーションごとのコンポーネント設定

Animator の **Animation Clips** リストから行を選択し、そのクリップの **Animation Name**、
**Speed**、**Playback Mode**、**Apply Root Motion** を編集します。クリップを切り替えても
個別の設定が保持されます。新規クリップの速度は1、Root Motion は無効です。
速度0では時刻が停止し、負の速度では逆再生します。有限でない速度と空の名前は拒否されます。
名前はコンポーネント内だけで使う表示名であり、ファイル名や共有アセット名は変更しません。
設定は名前ではなく Clip GUID で識別するため、表示名の重複は許可されます。

クリップ設定を保持するには **Scene/Prefab** を保存してください。Play 用の複製は設定をコピーしますが、
可変設定を共有しません。同じ Model を使う別々のオブジェクトに、異なる名前、速度、
Root Motion の有効状態を設定できます。**Root Motion Joint** はコンポーネント全体で共通の
Skeleton 設定です。

**Playback Mode** では、端点で停止する **Once**、繰り返す **Loop**、
前後に往復する **Ping Pong** を選択します。モードはコンポーネントごとに設定され、
共有 Clip アセットや Controller の状態は変更されません。モード変更時は現在の表示ポーズと
再生／一時停止状態を維持し、サイクル数をリセットして Ping Pong の外向き区間を使います。
古い payload は各 Clip アセットのモードを引き継ぎます。

**Seek** スライダーは正規化位置をただちにプレビューし、再生を一時停止します。
`seek()` API は再生／一時停止状態を維持しますが、Inspector はスクラブ成功後に明示的に停止します。
**Stop** は最初のアニメーションポーズに戻って一時停止し (Skeleton の bind pose には戻りません)、
**Rewind** は再生／一時停止状態を維持したまま同じポーズに戻ります。これらの操作で
GameObject に Root Motion の移動や回転を適用することはありません。**Play** は現在位置から再開し、
Ping Pong の移動方向も維持します。再生が終了した Once は、速度の符号に応じた端点から再開します。
速度が0の場合は時刻を進めません。

**Previous Frame**／**Next Frame** は元データに保存されたサンプリング間隔を1つ進み、
一時停止してポーズをただちに反映します。間隔は Clip 内の等間隔な ozz 時刻から復元され、
末尾だけ短い間隔も考慮されます。端点のみのクリップなど、保存キーから一定のサンプルレートを
判断できない場合は 1/30 秒を使います。ソースファイルは読み込みません。フレーム移動は再生速度と
Ping Pong の方向を無視し、折り返さず両端で停止します。また Root Motion で GameObject を動かしません。

各クリップには **Root Translation X / Y / Z** と **Root Rotation** のフラグも保存されます。
水平移動では X/Z を有効、Y を無効にします。回転だけを使う場合は3軸の平行移動をすべて無効にします。
これらは GameObject のワールド変換前の Model ローカル軸であり、ワールド空間の移動制約ではありません。
無効にした成分も破棄せず、アニメーションポーズに残します。Loop や逆再生を含め、Root Motion の
抽出とポーズからの除去を計算する前に軌跡をフィルターします。フラグを変更しても GameObject は
移動せず、新しい抽出設定でポーズを再評価します。**Apply Root Motion** は抽出全体の
有効／無効を切り替えます。各フラグはクリップごとの設定で、Scene/Prefab payload v5 に保存され、
Play 用複製にもコピーされます。v1-v4 データでは既存動作を維持するため4つのフラグをすべて有効にします。

**Initial Animation** はゲームの Play 開始時に使うクリップを選びます。**Selected Animation** は
保存済み／エディター上の選択を使い、新しく設定した Animator では Model の先頭クリップを使います。
**Play Automatically** は再生を開始するか、最初のポーズで一時停止するかを制御します。
起動時は、エディターのスクラブやプレビューの Pause/Play にかかわらず、常に正規化時刻0から始まります。
これらの設定を変えても現在のエディタープレビューには影響しません。明示的に指定した初期クリップを
削除すると **Selected Animation** に戻ります。Scene/Prefab payload v5 と Play 用複製はこれらの設定を
保持します。初期クリップがない、または互換性がない場合はログに記録し、別のクリップへ黙って置き換えません。

AnimatorComponent が扱うのはクリップ再生のみです。Controller の割り当て、状態再生、
クロスフェード、パラメーター／トリガー、およびそれらの Inspector／シリアライズ処理は
このコンポーネントから削除されています。State/event の Runtime とアセット基盤は別の
コンポーネント向けに残っていますが、AnimatorComponent はイベントを dispatch しません。
Payload v5 は v1/v2 の速度／Root Motion フラグを選択クリップへ移行して読み込みます。
旧 Controller の割り当ては復元せず警告を出します。移行するにはクリップを選択し、
Scene/Prefab を保存してください。廃止された FlatBuffers slot はバイナリ互換性のために予約されており、
Controller 機能としては使用されません。

## 汎用 State Machine

**Window > State Machine** を開き、**New** または **Open** を選択します。
GameObject の **State Machine** コンポーネントに割り当てる前に、グラフを `.statemachine`
アセットとして保存してください。Runtime ではグラフアセットを共有し、不変に保ちます。
各コンポーネントのインスタンスは現在の状態、パラメーター値、イベントキューを個別に所有します。
コンポーネント payload が保存するのはアセット GUID／パスと任意の開始状態上書きであり、
グラフ本体や一時的な Runtime 状態ではありません。

キャンバスのコンテキストメニューから **Add State** を選び、ノードをドラッグして配置します。
中ボタンでパン、ホイールでズームできます。状態を選び **Link from this state** を選択してから
遷移先ノードをクリックすると遷移を作成できます。既定状態を設定し、Float/Int/Bool/Trigger
パラメーターを追加したうえで、Inspector から遷移イベントと条件を設定します。
1つの遷移にある条件は AND で評価します。条件に一致した outgoing 遷移のうち、
数値 priority が最も小さいものを選びます。同値ならアセット内の順序で決まり、1回の update で
実行する遷移は最大1つです。イベントも条件もない遷移は無条件です。
入力イベントは一致する遷移が実行されるまでキューに残り、Trigger パラメーターは選択された遷移が
消費した場合にのみリセットされます。

状態には **On Enter** と **On Exit** アクションを設定できます。**Emit Event** は名前付きの出力を
キューに入れ、ゲームプレイコンポーネントは `StateMachineComponent::consumeEvent` で取得できます。
外部コードは `sendEvent` で遷移入力イベントをキューに入れるか、ID を指定して型付きパラメーターを
更新できます。**Play Animation Clip** は保存済み `.anim` 参照を読み込み、同じ GameObject の
Animator に再生を依頼します。Animator には互換性のある Skeleton が必要です。このアクションでは
クロスフェードせずクリップを切り替えます。独自の C++ アクションは
`StateMachineActionRegistry::registerType` で登録できます。アセットには安定した型名と payload bytes
のみを保存し、実行可能ポインターは保存しません。必要に応じて payload validator と
エディター callback を登録してください。未知の独自型はアセット読み込み時に保持され、
実行しようとした際に報告されます。

保存前にエディターが ID、状態／パラメーター参照、条件型、アクションデータ、既定状態を検証します。
編集後はグラフを保存してください。未保存グラフは Scene や Prefab から参照できません。

## アニメーションの削除

Animator の **Animation Clips** リストでアニメーションを選択し、行を右クリックして
**Delete Animation** を選択します。残す場合は **Cancel** を選ぶかメニューを閉じます。
従来の削除ボタンは使用しません。

削除時には Model のクリップ参照と埋め込みアニメーショントラックの両方を取り除き、
再生状態をクリアする前に `.model`／`.mdl` アセットをアトミックに保存します。
保存に失敗した場合はアニメーションと再生状態を変更せず、エラーを報告します。
インポート直後または未保存の Model の場合は、先に Model Renderer の **Save Model As...** で
`.model`／`.mdl` を保存してください。元のインポートファイルは書き換えません。
Model アセットは共有されるため、更新したクリップ一覧は今後の読み込みや、同じ Model を使う
他のオブジェクトにも適用されます。別ファイルの `.anim` は削除しません。
Animator の選択解除状態を保持するには、編集後に Scene/Prefab を保存してください。

## Animator の root motion

選択したアニメーションで **Apply Root Motion** を有効にすると、Model 空間の XYZ 平行移動と
完全な回転を抽出します。**Root Motion Joint** は名前が完全一致する joint を選択します。
既定の **First Root** は Skeleton の joint 0 を選びます。FBX には静的な Scene/group root が
含まれることが多いため、その場合は実際にアニメーションする root／hips joint を選んでください。
これは humanoid retargeting ではありません。抽出の有効／無効は選択クリップの
コンポーネント内設定で制御し、クリップ既存の `applyRootMotion` metadata は追加の有効条件ではありません。

抽出を有効にした時点の評価済みポーズを基準に、選択 joint の剛体変換を測定します。
復元／シーク後の時刻やブレンド済みポーズも対象です。クリップと Skeleton の rest pose が異なる場合も、
有効化時には作成者が設定した初期位置と向きを維持します。抽出 joint を変更した場合も、
現在表示中のポーズを維持します。移動を評価済みポーズ全体から取り除くため、GameObject を
移動してもアニメーションが二重に適用されません。アニメーションによるスケールはポーズに残ります。
選択 joint の変換は有限かつ非特異な TRS でなければなりません。shear を含む変換は近似せず拒否し、
ログに記録します。剛体逆行列では affine 行列の最終列を厳密に保持し、animated-bounds 検証は
緩和しません。

Loop ではクリップ端点をまたぐ剛体軌跡を延長し、向きの変化、1回の update 中の複数周回、
逆再生にも対応します。PingPong は軌跡を逆にたどり、Once は端点で停止します。
Seek、状態変更、rebind、一時停止、Root Motion 有効化では、クリップの初期オフセットを
移動量として適用しません。クロスフェードでは無関係な絶対クリップ位置の差ではなく、
状態ごとの移動量をフレーム平均の遷移 weight でブレンドします (fade 終了をまたぐフレームも含みます)。
回転には最短経路の quaternion 補間を使います。精度は ozz の圧縮キーと推定サンプリング補間に依存します。

Play/Step 中の移動は、すべての Animator worker の join 後、physics／late update より前に、
ゲーム更新スレッドで適用されます。平行移動は現在のワールド回転とスケールに従い、
親変換を通じてローカル空間へ戻します。回転はオブジェクトのワールド座標系で合成し、
オブジェクトのスケールを変えずにエンジンの親合成規約でローカル回転へ変換します。
オブジェクトまたは親の変換が特異な場合はログに記録して拒否します。
ポーズ snapshot を利用できない場合、Root Motion のアニメーション時刻を進めず、
移動量も失いません。

有効な CharacterController がある場合、Transform を瞬間移動させる代わりに衝突解決済みの移動を
適用します。作成時から GameObject 原点は**足元**を表し、1 mm 固定しきい値未満の移動も
切り捨てません。Transform／親の位置が明示的に変更された場合、次の衝突解決移動の前に
controller の足元を同期します。GameObject にはアニメーション回転を適用しますが、カプセルは
ワールド上方向を維持します。Root Motion は重力を加えず、他の移動スクリプトの移動量は加算されます。
Physics が一時停止中または利用できない場合、controller の移動は拒否され、Transform への
フォールバック移動は行いません。Dynamic Rigidbody は明示的に非対応です。Kinematic Rigidbody は
既存の physics 同期に従って Transform に追従します。有効な controller と Rigidbody は併用できません。
有効な controller がない場合は Transform を使い、static collider は通常の physics 同期に従います。

編集時のプレビューでは、ポーズから Root Motion を取り除きますが、Scene オブジェクトは移動しません。

Animator payload v5 と Play 用複製はクリップごとの設定と選択 joint を保持します。
v1 payload も読み込め、既定値は First Root です。Runtime の移動量は一時データであり、
保存も読み込み後の再生もされません。

## Foot IK（ozz-animation と PhysX）

Model Renderer、Animator の順に追加して Skeleton とクリップを設定し、**Foot IK** を追加します。
**Left Foot / Right Foot** で Hip、Knee、Ankle を選択してください。脚は
Hip → Knee → Ankle の祖先チェーンであり、左右の Hip のサブツリーは独立している必要があります。
片脚だけの使用も可能です。空または未完成の設定は保存できますが、未完成・不正なリグは
Inspector とログで通知し、通常のアニメーション姿勢を表示します。

- ボーンを選択した後、**Set axes from rest pose** で左右それぞれのローカル軸を設定できます。
  Y-up、+Z-forward のニュートラルな Rest Pose を基準に、膝の曲げ方向と足の上・前方向を
  逆変換して求めます。膝が直線なら +Z を優先する曲げ方向に使います。
  左右でボーンの軸が反転しているリグにも対応し、斜めの軸は `(custom)` と数値で表示します。
  推定できないリグはログ付きで拒否し、軸を変更しません。既存設定を自動で上書きすることはありません。
  Rest Pose が寝ている・特殊な向きの場合は手動設定してください。Play 中だけの変更は
  Stop 時に戻るので、編集モードで設定して Scene/Prefab を保存してください。
- **Knee hinge axis** は膝のローカル回転軸です。ozz の規約では正の回転で膝の角度が開きます。
  **Knee pole axis** は Rest Pose の膝ローカル空間で曲げ方向を指定します。
  リグ解決時に Hip の親の Rest Pose 空間へ変換し、再生中はその親に追随させます。
  膝自身の回転に Pole を追随させると、脚を曲げた際にターゲット方向と平行になり、
  曲げ面が反転するためです。親のない Hip はモデル空間を基準にします。
  軸はモデル依存です。既定の -X / +Z をそのまま全モデルに使わないでください。
- **Foot up axis / Foot forward axis** は足のローカル上方向と前方向です。
  互いに直交する軸を設定します。`IKTwoBoneJob` で脚を補正した後、
  `IKAimJob` で上方向を地面法線へ向け、アニメーションの足の進行方向を維持します。
  前方向は、未補正の足の上方向をワールド上方向へ合わせる最短回転で復元してから、
  ワールド上方向を地面法線へ合わせる回転で運びます。単純な地面への投影や、
  足の上方向から地面法線への直接回転は、足が立つ姿勢や急斜面で向きが反転するためです。
  補正率はこの二段階の回転それぞれへ適用してから、最終的な上・前方向を `IKAimJob` へ
  渡します。合成した180°付近の足首回転を一度だけブレンドすると、接地の減衰中に
  回転経路が反転するためです。
- レイはワールド +Y を上方向として、未補正の足首の **Ray height** 上から
  **Ray depth** 下まで照会します。自分自身の GameObject と Trigger は除外し、
  **Ground layer mask / Include dynamic ground** を適用します。
- **Sole height** はワールド単位での足首から足裏までの距離です。斜面では法線方向の
  クリアランスを確保しながら足首をレイ上に保ちます。**Max ground slope** より急な斜面、
  裏面、**Max foot adjustment** を超える地面は接地として扱いません。
- **Automatic foot contact** は既定で有効です。未補正の足首が接地ターゲットより
  **Contact fade distance**（既定 0.15 ワールド単位）以上高いと補正を外し、
  近づくにつれて滑らかに強くします。足裏の上方向とワールド上方向・平滑化した地面法線の角度がともに 60°以下なら
  向きによる減衰はなく、60～90°で減衰し、足裏が地面を向かない遊脚は補正しません。
  脚・足首・骨盤の接地ウェイトに共通して適用し、空中の足を地面へ引き寄せたり
  大きく反転させたりするのを防ぎます。OFF では従来どおりレイの有効範囲全体で補正します。
  ただし有効な地面に対して足首が接地ターゲットを下回る場合は、位置の押し上げを別途適用します。
  接地フェード・目標位置の平滑化・骨盤の下降で足首が地面へ沈まないようにし、
  この押し上げでは足首の向きを強制変更しません。`IK weight / Leg weight` による
  明示的な減衰・無効化は押し上げにも反映し、到達不能な姿勢は脚の長さの範囲に制限されます。
- **IK weight / Leg weight** は補正率です。ジャンプやクリップ固有の接地フェーズはゲーム側から
  `setLegWeight(0, weight)`（左）／`setLegWeight(1, weight)`（右）で補正率を下げてください。
  `setWeight` は全体の補正率を変更します。これらは接地の平滑化状態を維持し、
  設定全体を差し替える `setSettings` と異なり、リグの再解決を発生させません。
  自動判定は距離と足裏方向による近似であり、クリップの接地イベント解析や
  足のワールド位置固定は行いません。**Soften** は伸び切る直前の脚の緩和率です。
- **Visual pelvis correction** は最も下がる足に合わせて姿勢全体を下げ、
  **Max pelvis drop** で制限します。GameObject、CharacterController、Collider は動かしません。
  到達不能なターゲットへ脚を伸縮して合わせることもありません。
- **Response speed** は補正と接地解除の指数平滑化速度です。0 なら即時反映します。
  接地しなくなった足は補正率を減衰させて元の姿勢に戻します。
  設定変更・Skeleton 変更・無効化時に平滑化状態をリセットします。

IK は全 Animator worker の同期と全 Root Motion 適用の後にゲーム更新スレッドで実行します。
未補正姿勢を保持した専用バッファを使い、補正後に全子孫の `LocalToModelJob` を再実行して
immutable skinning snapshot を公開します。worker は未補正の skinning 変換を検証してから
Root Motion 差分を確定し、Snapshot の最終公開だけを IK 後へ遅延します。通常再生・一時停止に
補正が累積しません。編集中のアニメーションプレビューの評価経路にも適用しますが、
照会対象は PhysX ワールドに登録済みの Collider のみです。編集時のプレビューのために
ゲームプレイの物理 Lifecycle を起動することはありません。Play Pause は表示を凍結します。
ワールド変換は正の均等スケールで shear がないことが必要です。非対応の変換はログ付きで拒否します。
Scene/Prefab と Play 複製は専用 payload v1 で設定だけを保持し、接地状態は保存しません。
自動接地設定は後方互換の追加フィールドで、旧 payload も既定の自動判定が有効になります。

# Light と Directional Shadow

GameObject を作成し、Inspector で **Light** を追加します。Directional、Point、Spot のいずれかを
選択して、色と強度を設定します。Point と Spot はオブジェクトのワールド位置と Range を使います。
Directional と Spot はローカル **+Z** 方向へ光を放つため、照射方向は Transform の回転で調整します。
Spot angle は内側／外側の円錐角全体を指定します。アクティブ Scene と persistent Scene の有効な
ライトは、編集中や一時停止中も描画されます。最大32灯まで対応し、超過分はログに記録されます。
Deferred の不透明面では、ライトが16灯以上の場合、Compute Shader が 8×8 画素ごとに
可視面のワールド空間境界を集約し、Point/Spot の到達範囲と交差しないライトを除外します。
Directional は常に対象とし、Spot は保守的に Range の範囲で判定します。透明描画と
ボリュームフォグは従来どおり全ライトを評価します。効果は配置や GPU に依存します。

**Cast Shadow** が有効な最初の Directional light が、2048x2048 の動的 shadow map を所有します。
シーンのスケールに合わせて Shadow Distance、Shadow Bias、Normal Bias を設定してください。
カメラ相対 projection は texel 単位で安定化され、3x3 PCF でサンプリングされます。
個々の shadow caster は Model Renderer の **Cast Shadows** で制御します。Opaque と AlphaTest
マテリアルは影を落としますが、Transparent マテリアルは落としません。Point/Spot の影、
cascade、baked lighting には対応していません。

Scene のライトが従来のハードコードされた照射方向に置き換わります。有効なライトがない Scene は、
基本 ambient 寄与 (0.15) と emissive マテリアルのみで照らされます。既存 Scene で直接光が必要な
場合は Directional light を追加してください。Light 設定は Scene/Prefab に保存され、
分離された Play にも複製されます。

# スクリーン空間 Runtime UI

1. ルート GameObject に **Canvas** を追加し、Reference Size と Sort Order を設定します。
2. 子 GameObject に **Rect Transform** を作成します。Anchor Min/Max は親の矩形内を補間し、
   Offset Min/Max は Canvas 単位で各辺を指定します。Y 軸は下向きです。Anchor が同じ場合は
   固定サイズの矩形になり、Anchor が `(0,0)` と `(1,1)` で offset がゼロなら親全体に広がります。
3. Rect Transform の後に **UI Image**、**UI Text**、または **UI Button** を追加します。
   **UI Image** では **Browse...** をクリックし、Windows のファイルダイアログから WIC 対応画像
   (PNG/JPEG/BMP/GIF/TIFF、Unicode パスを含む) を選択します。現在の画像が初期選択され、
   キャンセル時は変更されません。**Image Path** は直接編集できます。**Clear** は Tint を変更せず
   画像パスを削除し、単色の矩形に戻します。選択したパスはプロジェクトにインポート／コピー
   せず、元の場所を参照します。Text は UTF-8 で、インストール済みフォントファミリーを使用し、
   Unicode shaping、折り返し、クリッピング、Left/Center/Right 配置に対応します。
   Button は通常、hover、pressed の色を持ちます。Image と Button を同じオブジェクトに置く場合、
   不透明な画像を操作色の上に描くのではなく、button 状態に応じて画像の tint を変えます。

Canvas は縦横比を維持して一様に拡大縮小し、Game の画像／クライアント領域の中央に配置します。
描画と hit testing は Canvas 内にクリップされます。親 RectTransform で入れ子レイアウトを構成でき、
入れ子 Canvas は独立した reference size と order を持ちます。Sort Order が大きいものと、
後から描画される兄弟が手前になります。UI はシーンジオメトリ上、エディター ImGui または
Runtime の presentation の前に描画されます。画像と text layout はキャッシュされ、
古いエントリーは UI の120フレーム後に解放されます。layout、style、リソースパスは
Scene/Prefab と Play に保存／複製されます。

Windows UI backend は D3D11On12 を介して Direct2D/DirectWrite を使用します。
一部の Windows SDK では、D3D11 共有 metadata の確認時に native DX12 resource に対する
debug layer diagnostic 916 が出ます。これは相互運用 metadata の問い合わせであり、
render target の状態／ライフタイム違反ではありません。Native image-effect pipeline では、
意図的に未バインドとしている任意の depth/stencil view に対して diagnostic 680 が出る場合もあります。
エンジンはどちらの diagnostic も抑制しません。

ボタンの **Click Event** 名を設定し、ゲームコードからハンドラーを登録します。

```cpp
Engine::RuntimeUI::instance().registerClickEvent("StartGame",
    [](Engine::GameObject& runtimeButton)
    {
        runtimeButton.setName("Started");
    });
```

名前はそれぞれ一度だけ登録し、所有するゲームシステムの破棄時に登録解除してください。
ハンドラーに渡されるのはランタイムオブジェクトであり、編集中の元オブジェクトではありません。
クロージャーから編集中のオブジェクトや Stop 時に破棄されるオブジェクトへのポインターを保持しないでください。
ハンドラーは GameUpdate で実行されます。メインスレッド限定の session/editor 操作は click callback 内で
直接実行せず、`MainThreadDispatcher::post` で投稿します。コールバック自体はシリアライズ／複製されません。
登録されていない空でないイベントをクリックするとログに記録されます。クリックとして認識されるには、
同じ有効かつ操作可能な button の内側で press と release の両方を行う必要があります。
無効な最前面 button は背後の button をブロックします。Game view／ウィンドウのフォーカス喪失、
Pause、Stop ではポインター捕捉を解除します。編集中と一時停止中のフレームでは click を dispatch しません。
キーボード／ゲームパッド操作、world-space UI、汎用レイアウトウィジェットには対応していません。

# WAV オーディオ

emitter GameObject に **Audio Source** を追加し、**WAV Path**、Volume、Pitch、Loop、
Play On Awake を設定します。**Browse...** をクリックすると Windows ファイルダイアログで
WAV ファイルを選択できます (日本語／Unicode パスに対応)。現在のファイル、または未設定時は
Assets フォルダーが初期位置です。選択時にはオーディオデバイスを開いたり再生を開始したりせず、
WAV データを検証してキャッシュします。キャンセルまたは無効な WAV の選択では、既存クリップと
再生状態を維持し、エラーをログに記録します。**Clear** は再生設定をリセットせずにクリップを削除し、
voice を停止します。**WAV Path** の直接編集も可能です。ファイルはプロジェクトにコピーせず、
元の場所を参照します。保存設定のパスは UTF-8 で、相対パスはアプリケーションの作業ディレクトリを
基準に解決します。WAV は mono または stereo PCM (8/16/24/32-bit) と 32-bit IEEE float、
8-192 kHz、最大512 MiB に対応します。再生前に RIFF chunk と WAVEFORMATEXTENSIBLE metadata を
検証します。圧縮オーディオ形式とストリーミングには対応していません。

mono クリップで **3D Spatial** を有効にし、アクティブ／persistent Scene 全体で有効な
**Audio Listener** をちょうど1つ追加します (通常はカメラ上)。Listener のローカル +Z/+Y が
forward/up を定義します。X3DAudio がスピーカーの定位と doppler を適用します。
Min/Max Distance は減衰と最大距離を超えた場合の無音化を制御します。Listener がない場合、
spatial voice は明示的にログへ記録されてミュートされます。複数ある場合は最初の Listener を使い、
設定エラーをログに記録します。

既定の XAudio2 出力デバイスは初回再生時に開きます。`--help`、Scene の読み込み、無音の
エディター操作では開きません。出力／voice の失敗はログに記録されます。デバイスコールバックは、
無音のキュー済み voice を無期限に残さず、影響を受けたゲームプレイのミキシングを停止します。
その後に明示的に再生すると出力を再度開けます。
ゲームコードから `AudioSourceComponent::play`、`pause`、`stop`、`isPlaying` を使用できます。
`isPlaying` は一時停止中も含め、バッファがキューに残っていることを示します。
`AudioSystem::playbackSeconds` はループ再生分を含む、消費済みサンプル時間を返します。
クリップ、Loop、Spatial モードを変更すると voice を停止します。再開するには play を呼び出してください。

編集中に voice が再生されることはありません。Play On Awake はランタイムコンポーネントの有効化時に
再生を開始します。Pause はすべてのミキシングを停止し、サンプルカーソルを保持します。
Step で source が初期化されることはありますが、一時停止中にオーディオが進むことはありません。
Resume で再生を続行します。Stop、コンポーネントの無効化／削除、shutdown では voice を破棄します。
Play の複製は不変のキャッシュ済み WAV データを共有しますが、可変 voice は共有しません。

# GPU オクルージョンカリング

Renderer Statistics の **GPU Occlusion Culling** を有効にすると、同じフレーム内の
Hi-Z 可視判定と count buffer による `ExecuteIndirect` を Opaque/AlphaTest のカラー描画に適用します。
既存の depth pre-pass と shadow pass は、引き続き投入済みジオメトリをすべて描画します。
Transparent 描画は従来どおり直接実行され、順序も維持されます。

標準 Z の depth target を R32_FLOAT としてサンプリングします。2のべき乗に拡張した
R32_FLOAT mip chain には、未描画ピクセルの深度1を含む**最大深度**を格納します。
安全な bounds を持つ各 instance は GPU 上で world AABB を投影し、画面矩形を2 pixel 拡張して、
最も近い深度をカメラ方向へ bias します。instance を除外するには、選択した mip で矩形と重なる
すべての Hi-Z cell がその矩形より手前にある必要があります。未検証のアニメーション bounds、
退化した bounds、有限でない投影、near plane との交差は可視のままです。
過去の depth を保持する `CameraClearMode::Nothing` ではカリングを無効にします。

共有 draw batch 内でも instance ごとに判定します。可視 instance の128-byte 定数データを、
その batch 用に予約された範囲へ詰め直します。各 batch には `D3D12_DRAW_INDEXED_ARGUMENTS` と
32-bit command count からなる24-byte packet を設定します。空の batch は count 0 となり、
predicate 付き Draw や instance 数0の Draw を行うのではなく、カラー Draw 自体を実行しません。
候補 batch ごとの CPU state 設定と `ExecuteIndirect` の送信は引き続き発生します。
これは CPU 側で Scene をフィルターする方式でも、複数マテリアルをまとめた単一の indirect submission
でもありません。CPU object 準備、depth、shadow のコストは残ります。

リソースと descriptor はフレーム単位で管理し、fence 完了後にのみ再利用します。
Hi-Z mip の遷移では、書き込み先を更新する間もソース mip を読み取り可能な状態に保ちます。
カラー command list の実行前に、生成した instance を shader-read 状態へ、packet を
indirect-argument 状態へ遷移させます。CPU readback は非同期の統計取得だけに使い、
現在フレームの可視判定には使いません。詰め直しは連続する64-instance tile をまたぐ
group-wide prefix scan で、ソート済みキューの順序を維持します。wave size や atomic の実行順には
依存しません。これにより、実運用の `EQUAL` depth 比較を含め、同一平面上で重なる instance の
カラー描画順を保持します。

カメラカリングでは、jitter と変換済みカメラを含む透視／正射影に対して、
`CameraFrustum` のワールド空間 clip plane を使います。
`CameraComponent::getFrustum`、`RenderView::frustum`、`DX12Renderer::setFrustum`、
`RenderQueue::submit` は `DirectX::BoundingFrustum` の代わりにこの型を使います。
構築には `CameraUtils::buildWorldFrustum` を使用してください。従来の数学用 `Frustum` alias は、
透視射影専用の DirectX collision query 向けに引き続き利用できます。
カメラ行列が無効な場合はエラーを記録し、clip-plane カリングを無効にします。

Depth clear、model pass、Hi-Z、overlay は `CameraUtils::toPixelViewport` を共有します。
この変換は浮動小数点の丸めがあっても、正規化 viewport の端を render target 内に収めます。
Model の scissor rectangle は clear と同じ floor/ceil 規約を使います。
カリング入力の検証では、右端／下端に対する小さな浮動小数点丸め超過のみを許容します。
範囲外または有限でない入力は明示的に失敗させます。

統計は直接送信、indirect 送信、完了した GPU color draw を区別します。
**Frustum-visible Objects** はオクルージョン判定の統計ではありません。
**Occluded Color Instances** は一意な GameObject 数ではなく、submesh instance 数を数えます。
完了済み slot の統計は現在フレームより遅れることがあり、その slot でカリングが無効なら0を返します。
カリング有効／無効時の GPU frame time を比較してください。小規模な Scene では Hi-Z の構築と
詰め直しのコストが削減分を上回る場合があります。

## 現在のポーズに基づくアニメーション境界

アニメーションモデルでは、bounds 判定と GPU 描画に同じ不変の
`SkinningPaletteSnapshot` を使います。前フレームのポーズ、クリップ全体の推定値、
bind-pose bounds 周辺の固定 padding で可視性を決めることはありません。
GPU mesh 作成時に、各 bone の影響を受けるローカル位置の最小／最大値と rigid vertex を
キャッシュします。weight が非負なら、shader の正規化線形ブレンドは、これらの影響範囲を
変換した領域の和集合内に収まります。各フレームでは bone matrix の後に、正確な
mesh-node/world matrix を適用して範囲の各頂点を変換します。これにより mesh 全体を CPU skinning
せずに、Model 空間の skinned mesh、mesh-local vertex、ブレンド済みポーズ、非一様／負の scale、
weight のない vertex に対応します。

Bounds の演算には double 精度を使い、桁落ちと正規化を含む値の大きさに応じた float 誤差 margin と、
外側へ丸めた extent を適用します。shader の weight しきい値に近い vertex は rigid と skinned
両方の範囲に含めます。Bone index は shader と同じく255で clamp し、未使用 palette slot も
GPU 描画と同じ単位行列の動作を維持します。Animator の bind-pose 補正逆行列は affine 行列の
最終列 `(0, 0, 0, 1)` を厳密に保持します。汎用の行列逆変換では同次成分が1から丸められる場合が
あるためです。Clip と Controller の初期化は有限でない、特異、または affine でない bind transform
を明示的に拒否します。Bounds 検証は厳密なままです。無効な palette の診断には最終列も含め、
許容誤差で失敗を隠したり描画時 palette を変更したりしません。

Renderer は現在の mesh bounds を統合して Model 全体の frustum 判定を行い、その後、各 mesh の
ポーズ bounds を frustum 判定、Transparent のソート、Opaque/AlphaTest の Hi-Z に使います。
画面外の shadow caster も引き続き shadow を送信します。Skinning の draw batching は変更せず、
アニメーション更新も停止しません。Scene/Prefab やアセット schema の移行は不要です。

無効な weight、利用できない影響範囲 cache、有限でない／射影行列、無効な palette 数、
float overflow の危険はログに記録され、アニメーション bounds カリングを無効にします。
`RenderItem::animationBoundsValid` の既定値は false です。そのため bind-pose bounds だけを
指定する呼び出し側では、従来どおり安全のためアニメーションカリングを迂回します。
Near plane と交差する場合も Hi-Z の可視扱いを維持します。

## スキニング結果の再利用

Bone Weight を含むメッシュは、フレーム内の同じ頂点バッファと姿勢パレットの組み合わせごとに
Compute Shader で一度スキニングし、深度・シャドウ・色・透明描画で結果を共有します。
現在と前回の位置、法線、接線、従法線を保持するため、モーションベクトルも従来の姿勢履歴を
使用します。Bone Weight のないメッシュは Compute 処理を行いません。アニメーションのない
モデルの bind pose も同じ仕組みで共有し、異なる姿勢や前回履歴は共有しません。

結果バッファは `COMMON` 状態で作成し、Compute 書き込み用に `UNORDERED_ACCESS`、
描画用に頂点バッファ状態へ遷移します。フレームスロットごとに保持し、GPU の完了後に再利用します。メッシュ数と
頂点数に応じた GPU メモリと Compute 実行のコストが増えるため、パス数の少ない場面での
高速化は保証されません。

# Terrain Phase 1/2/3/4/5/6/7: 生成、マテリアル、PhysX、チャンク、LOD、バックグラウンドビルド

Hierarchy のコンテキストメニューから **Create Terrain** を選択するか、既存の GameObject に
**Terrain** コンポーネントを追加します。新しいコンポーネントには平坦な 65x65 グリッドが
含まれます。Terrain レイヤーを明示的に有効にするまでは既定マテリアルを使い、既存の model
pipeline 経由でエディターと Play/Runtime の両方に描画されます。Model Renderer は不要です。

Terrain Inspector では次の項目を設定できます。

- **Size X / Size Z**: 原点を中心とするローカル空間での幅と奥行き。
- **Height Scale / Height Offset**: `localY = offset + sample * scale`。
- **UV Repeat X / Z** と **Cast Shadows**。
- **Chunk Cells (16..1024)** と **Apply Chunk Settings**: 適用済みサーフェスを高さデータや
  mesh 寸法を変えずに再分割します。
- **Enable Terrain LOD**、**Maximum LOD (1..4)**、**LOD Distance**、**Apply LOD Settings**:
  距離に応じたチャンク簡略化を明示的に有効化します。
- **Apply / Rebuild**: サンプルを変えずに編集した寸法を適用します。
- **Import Heightmap...**: PNG、TIFF、BMP から高さデータを読み込み、編集済み寸法で再構築します。
  8/16-bit grayscale PNG の精度を保持します。
- **Create Flat**: 指定した X/Z サンプル数と一定の正規化値で高さを置き換えます。
  インポート済み高さデータも置き換えられます。
- **Generate Noise**: seed 付き Perlin noise または FBM で高さを置き換え、編集済み mesh 寸法を適用します。
  以下のプロシージャル生成設定を参照してください。
- **Generate Noise (GPU)**: Compute Shader による Perlin/FBM を明示的に使用し、正準高さデータを
  非同期で読み戻します。既定は CPU です。
- **Background** の段階／時間と **Cancel Terrain Generation**: 生成とすべての Apply ボタンは
  作業をキューに入れ、完了結果が安全なフレーム境界で適用されるまで現在の Terrain を維持します。

Height sample は sRGB 色ではなく、`[0, 1]` の線形数値データです。インポートでは gamma
metadata を無視して red channel を取得し (alpha は無視)、画像の先頭行をローカル `-Z` に、
そこから `+Z` 方向へ増加するよう対応付けます。各 pixel が1頂点になります。画像を暗黙に
リサイズすることはありません。非正方形グリッドにも対応します。各軸の寸法は2..1025 samples、
物理サイズは0.01..100000、height scale は0..100000、height offset は-100000..100000、
UV repeat は0.001..10000 です。入力が無効な場合はログに記録して以前の Terrain を維持し、
Inspector 操作では状態メッセージも表示します。

Scene/Prefab payload には正規化サンプル、寸法、UV repeat、shadow flag を埋め込みます。
通常どおり保存してください。個別の model asset を保存する必要はなく、インポート後に元画像も
不要です。再読み込み、複製、Play clone では独立した生成ジオメトリを作成します。
Inspector で未適用の項目は保存されません。Transform、親 Transform、layer、コンポーネントの
有効／無効、frustum culling、既存 render path の occlusion 動作には標準エンジン機構を使います。

CPU API は `Source\Terrain\Data\Heightmap.h` と
`Source\Terrain\Geometry\TerrainMeshBuilder.h` にあります。データの適用には
`TerrainComponent::setTerrain` を使います。生成結果には32-bit index、+Y 向きの三角形、
面積で重み付けした平滑法線、正規直交の tangent frame、UV、mesh/model bounds が含まれます。
再構築時は以前の model handle を破棄します。キュー済みの不変 CPU snapshot と GPU fence により、
送信済みジオメトリは安全に保持されます。

Streaming は対象外です。Phase 8 では明示的に実行する CPU hydraulic erosion、Phase 9 では GPU noise、
Phase 10 では後述する静的植生配置を追加します。Phase 7 では Inspector の生成／インポート／Apply を
非同期化します。既存の同期 C++ API と Scene/Prefab 読み込みは互換性のため同期のままです。
いずれの API でも毎フレーム再生成しないでください。

## プロシージャル生成 (Phase 2)

Terrain Inspector で **Procedural Heightmap** を編集し、**Generate Noise** をクリックします。
生成は明示的な操作であり、コントロールのドラッグ中に連続実行されることはありません。
既存のインポート／平坦／生成済み高さを置き換えます。失敗時は診断情報と状態を表示し、
以前の Terrain と最後に正常適用された recipe を保持します。

| コントロール | 意味／対応する値 |
| --- | --- |
| Noise Mode | Perlin (1 octave) または FBM (Perlin octave の重み付き合成) |
| Seed | 0 と 4294967295 を含む、任意の unsigned 32-bit 値 |
| Noise Samples X / Z | 長方形グリッドを含む、各軸2..1025 samples |
| Frequency | グリッド全体の各軸に配置する基本 noise cell 数、0.001..1024 |
| Noise Offset X / Z | 基本 noise 空間での平行移動、-100000..100000 |
| Octaves | 1..12 (FBM のみ) |
| Persistence | 後続 octave の振幅倍率、0..1 (FBM のみ) |
| Lacunarity | 後続 octave の座標／周波数倍率、1..4 (FBM のみ) |

既定値は FBM、seed 0、65x65 samples、frequency 4、offset 0、6 octaves、
persistence 0.5、lacunarity 2 です。Perlin では FBM 専用コントロールを無効にしますが、
その設定値は保持します。どちらの mode でも、保存されるすべての field が有効でなければなりません。
既存の **Height Scale / Height Offset** は、生成した正規化サンプルを local Y に変換します。
Mesh の幅／奥行きは物理サイズに影響しますが、サンプリングする noise domain は変わりません。
解像度を変更しても、共通する座標での値は維持されます。

基本のサンプル座標は
`(x / (width - 1) * frequency + offsetX, z / (height - 1) * frequency + offsetZ)` です。
各 octave では同じ seed を維持し、両座標に lacunarity、振幅に persistence を乗算します。
Offset は octave のスケーリング前に適用します。負の座標は切り捨てではなく数学的 floor を使います。
Perlin noise は hash 化した格子座標、固定された8つの単位勾配、5次補間を使い、
変更可能な permutation table やグローバル乱数生成器には依存しません。
FBM は重み付き合計を octave 振幅の合計で除算します。符号付き noise は保守的な固定勾配上限を用い、
`0.5 + 0.5 / sqrt(2) * signedNoise` で変換します。出力は有限かつ `[0, 1]` ですが、
マップごとにこの範囲いっぱいまで引き伸ばしません。これにより、各グリッドの最小／最大値に応じて
Terrain のコントラストを変えるのではなく、隣接 domain 間で相対的な高さを保ちます。
FBM の octave が1つ、または persistence が0の場合、Perlin と完全に同じ結果になります。

最も高い octave 周波数に対して十分な sample 数を使ってください。サンプルが不足すると
細部が alias し、全サンプルが格子交点に一致すると一定値0.5のマップになります。
Generator が解像度、周波数、octave 数を黙って変更することはありません。
最大設定値は演算と割り当ての安全上限であり、毎フレーム、12 octaves で数百万頂点の Terrain を
生成する推奨値ではありません。バックグラウンド生成については後述の Phase 7 を参照してください。

`Source\Terrain\Generation\TerrainNoise.h` の `generateTerrainNoise` は Model を登録せずに
CPU 高さデータを生成します。`TerrainComponent::generateNoise` は高さマップと mesh 設定を生成し、
トランザクション方式で適用します。同じビルド内では、生成順や他のエンジン乱数処理にかかわらず、
同じ recipe から同じサンプルが得られます。

Terrain payload version 2 では、**最後に正常適用された noise recipe** を追加しました。
Scene/Prefab/Play の複製では実際の高さサンプルと recipe の両方を保持します。
保存済みサンプルが正であり、読み込み時に暗黙の再生成は行いません。
画像のインポート、平坦グリッドの作成、寸法の再構築後も、現在の高さと一致しない場合を含め、
次回生成用に最後の noise recipe を保持します。Inspector で未適用の recipe 編集は保存されません。
Version 1 の terrain payload も元のサンプルを変更せず読み込み、既定 recipe を設定します。
次回保存時は version 4 になります。version 2 以降の recipe が無効／欠落している場合は、
既存 Terrain を置き換えずに拒否します。

## Terrain マテリアルレイヤー (Phase 3)

**Terrain Material Layers** を開き、**Enable Layered Material** を有効にして各レイヤーを編集し、
**Apply Terrain Material** をクリックします。適用するまでは編集値は下書きであり、
Scene/Prefab の保存には適用済みの値のみが含まれます。**Reset Layer Draft** は soil/grass/rock/snow
の各レイヤーを無効にした既定値へ戻しますが、サーフェスはただちに変更しません。
レイヤーを有効にするまでは、既存 Terrain は白色／既定マテリアルのままです。

不透明な PBR レイヤーを1..4個、順序付きで設定できます。Layer 0 は未適用領域を埋めます。
後続レイヤーは coverage に応じて、それまでの全レイヤーに重ねられます。

`coverage = strength * heightBand(sample) * slopeBand(localSlopeDegrees)`

最小／最大の範囲内では band は1です。範囲外では設定した blend width にわたって smoothstep で
0まで滑らかに減衰します。幅が0の場合は境界を含む段差になります。重ねるレイヤーは以前の weight に
`1 - coverage` を乗じ、自身の weight に `coverage` を受け取ります。範囲に隙間や重なりがあっても、
weight は非負で合計1になります。coverage が1の高位レイヤーが全面を覆います。
一部だけ重ねるには strength を下げます。Base layer は残り全域を埋めるため、範囲と strength は使いません。

- **Height** は元の正規化 heightmap sample であり、ワールド Y やマップごとの最小／最大値への
  再マッピングではありません。Height offset/scale を変えても、この正規化値による band 判定は変わりません。
- **Slope** は生成した平滑頂点法線と local +Y の角度 (度単位) です。物理寸法／height scale の変更時に
  再計算します。Object／親の変換は完成したサーフェスを移動しますが、ローカルでの自動レイヤー判定は変えません。
- Coverage は heightmap の各頂点で計算し、三角形内で補間します。テクスチャと PBR 値は pixel 単位で
  ブレンドされます。狭い遷移には十分な heightmap 解像度を使用してください。Splatmap やブラシ塗りには対応しません。

各レイヤーには、線形 RGB tint、metallic／roughness factor `[0,1]`、normal scale `[0,2]`、
UV repeat `[0.001,10000]`、任意の **BaseColor**、**Normal**、**Metallic-Roughness**
テクスチャを設定できます。Layer UV repeat は terrain mesh の UV Repeat X/Z に乗算され、
サンプリングは wrap します。BaseColor は sRGB からデコードし、Normal と Metallic-Roughness は
線形データとして扱います。Metallic には青チャンネル、roughness には緑チャンネルを使い、
それぞれのレイヤー factor を乗算します。テクスチャがない場合は白の BaseColor、
平坦な tangent-space normal、中立の白 MR を使うため、スカラー factor はテクスチャなしでも機能します。
テクスチャ読み込みに失敗した場合は警告を記録し、標準マテリアルと同じ既定値を使います。
Terrain layer は不透明であり、BaseColor alpha は使用しません。Normal vector は共通の tangent frame
でブレンドして再正規化し、terrain の object transform に従います。

Height interval は `[0,1]`、slope interval は `[0,90]` の順序付き範囲とします。
Height blend width は `[0,1]`、slope blend width は `[0,90]`、strength は `[0,1]` です。
無効なレイヤーを含め、保存されるすべての slot は有効かつ有限でなければなりません。
テクスチャ参照には GUID と path hint を使います。テクスチャを選ぶと参照が下書きに設定され、
**Clear** は削除を下書きに反映します。テクスチャは外部アセットのままなので、利用可能な状態を保ってください。
Component の hint は既存の component 永続化方式 (解決済みパス) に従います。
単体／埋め込み material asset の hint は、それを所有するファイルからの相対パスです。

`TerrainComponent::setMaterialSettings` は設定をトランザクション方式で適用し、
`getMaterialSettings` は適用済み設定を返します。再構築／インポート／noise 生成では適用済み
サーフェスを維持し、blend weight を再計算します。Material は MaterialManager が共有する不変 CPU asset であり、
新しい MaterialComponent ではありません。各 Terrain は生成した model/material handle を所有します。
Scene/Prefab/Play の複製は独立した handle を持ちますが、テクスチャ参照は共有します。
キュー済みの不変なジオメトリ／マテリアル snapshot と GPU fence は、再生成や component 破棄後も、
最初の GPU upload 前を含めて有効です。Forward と Deferred は同じサーフェスサンプリング処理を使います。

Terrain payload version 3 で全レイヤー設定を追加しました。レイヤーデータのない version 1 と2は、
従来の見た目を維持するためレイヤー無効で読み込みます。Version 3 のマテリアルデータが無効な場合、
失敗時に既存 Terrain を保持します。Material/model asset version 3 も任意の Terrain 拡張に対応し、
version 1 と2も引き続き読み込めます。Terrain component の保存 version は後述の version 4 に
更新されますが、material/model asset version は変更されません。標準マテリアルの property layout と
override block も変更されません。

## サーフェスクエリと PhysX 衝突判定 (Phase 4)

**Terrain** と同じ GameObject の **Add Component** から **TerrainCollider** を追加します。
このコンポーネントは任意であり、サーフェスクエリや描画に必要ではありません。
既存の PhysX mesh backend を使い、生成済み三角形をメモリ内で cook します。
外部 `.physics` ファイルは不要です。Scene/Prefab/Play の複製は共通の collider 設定を保持し、
有効化時に埋め込み済み terrain sample から衝突形状を再生成します。

- Terrain Collider は **static** かつ non-trigger であり、同じ GameObject に **Rigidbody** を
  追加できません。エンジン標準の TRS 階層でサポートされる親変換を含め、正の非ゼロ world scale と
  回転に対応します。反転、ゼロ scale、shear を含む衝突変換には対応せず、エラーを記録します。
  衝突を有効にする前に、対応している変換を適用してください。
- 共通の collider friction/restitution、layer、collision mask、query filter、Rigidbody collision event は
  既存の physics 経路を使います。**Center** は collider のみに適用される任意の local offset です。
  物理形状を描画サーフェスと高さクエリ API に一致させるには0のままにしてください。
- インポート、noise 生成、Apply/Rebuild はジオメトリと同時に有効な衝突形状を更新します。
  cook/backend 更新に失敗した場合、以前の Terrain を保持します。同期 C++ 再構築は physics simulation
  の各 step の間に実行してください。Phase 7 の Inspector 要求はバックグラウンドで cook し、
  frame barrier で適用します。毎フレーム再構築しないでください。Object transform の変更は他の
  static collider と同様、次の physics simulation step で同期されます。
- Collider の有効状態は Terrain の表示状態と独立しています。Collider の無効化／削除、
  または object の非アクティブ化で physics shape を取り除きます。Terrain を削除すると衝突形状も
  切り離され、Terrain を再追加すると次の fixed update で再接続されます。
- 既存の **CharacterController** による移動は terrain triangle と衝突し、slope/step 設定を使って
  接地状態を報告します。Controller 自体は重力や高さへの吸着を追加しません。
  ゲーム側で `move` に下向きの移動を与えるか、後述の任意の移動コンポーネントを使用してください。
  再生成時は controller がキャッシュした三角形を無効化しますが、地面が上昇しても body を瞬間移動させません。
- 基本的なプレイヤーキャラクターを作るには、Player GameObject に **CharacterController** と
  **Character Controller Movement** を追加し、アクティブな **Main Camera** を Player の子にします。
  移動コンポーネントは fixed update 中に WASD を読み取り、ワールド X/Z 方向へ毎秒5 unit 移動し、
  移動方向へ滑らかに向きを変えながら重力を適用します。右マウスボタンを押しながらマウスを動かすと
  三人称カメラを回転できます。カメラは Player の後方で一定距離を保ち、collider に対して
  sphere cast を行い、めり込みを避けます。プレイ中は **Game** view にフォーカスしてください。
  Player に **Rigidbody** を追加しないでください。CharacterController はすでに PhysX actor を所有しており、
  エンジンは同一 object 上での併用を拒否します。

公開 CPU API は PhysX に依存しません。

```cpp
const auto local = terrain->sampleLocalSurface(localX, localZ);
const auto world = terrain->sampleWorldSurface(worldPosition);
if (world)
{
    const float groundY = world->position.y;
    const Engine::Vector3 groundNormal = world->normal;
    // ゲームプレイでこのサーフェスを利用し、CharacterController の衝突処理を置き換えないでください。
}
```

`TerrainSurface` は位置と、平滑化した描画法線ではなく**幾何学的な面法線**を保持します。
Local XZ のクエリ範囲にはグリッド境界を含みます。高さは双線形補間ではなく、
実際の `a,c,b; b,c,d` mesh triangle に一致します。World query は入力 Y を無視し、
world XZ における**最も高い**垂直交点と上向き法線を返します。逆変換可能な affine transform に対応し、
メモリ割り当てを行わないグリッド走査を使います。投影された Terrain の範囲外では
`std::nullopt` を返します。共有 edge では隣接するどちらかの面法線が使われる場合があります。
無効／有限でない query 入力はログに記録します。Query は適用済み Terrain data を使い、
Collider Center、有効状態、filter とは独立しています。

## 常駐チャンク管理 (Phase 5)

Terrain は引き続き **1つの GameObject、正準 heightmap 1つ、生成 model handle 1つ**で構成されますが、
Model はチャンクごとに1つの mesh を含みます。TerrainCollider は同じ actor 上に、
各チャンクに対応する static PhysX triangle shape を1つずつ作成します。
Query と collision event は非表示の子 object ではなく、元の TerrainCollider／GameObject を識別します。

**Chunk Cells (16..1024)** を設定して **Apply Chunk Settings** をクリックします。
新規 Terrain の既定値は **各辺64 cells** (1チャンクあたり最大65x65 vertices) です。
257x257 heightmap は4x4チャンクになります。Cell 数は sample 数やワールドサイズではありません。
対応範囲内の任意の整数を指定でき、2のべき乗である必要はありません。+X/+Z 側の端数チャンクは
残りの全 cell を保持します。チャンクの一辺がグリッドより大きい場合は mesh を1つ作ります。
最大対応サイズの1025x1025 heightmap を cell 数16で分割すると、mesh／shape は最大4096個になります。
画像インポートの上限は変わりません。

**Apply Chunk Settings** を実行するまでは、チャンクサイズの編集は下書きです。
このボタンは適用済み Terrain の寸法を使い、未適用の Size/Height/UV 編集は使いません。
**Apply / Rebuild**、インポート、平坦化、noise 生成、マテリアル変更では適用済みチャンク設定を維持します。
全チャンクは常駐し、分割によって距離単位の読み込み／解放や部分的な高さ編集が追加されることはありません。
Phase 6 では任意の LOD と早期チャンクカリングを追加し、Phase 7 では Inspector の再構築を
バックグラウンドで行います。Terrain 全体の bounds は維持されます。

法線、tangent frame、自動レイヤー weight は**グリッド全体**から計算してから分割します。
境界頂点では位置、法線、tangent、bitangent、UV、color を同一に複製します。
UV は Terrain 全体の座標を維持し、チャンクごとにテクスチャが繰り返し開始されることはありません。
各 cell は1つのチャンクだけに属します。共有 edge では頂点を重複させますが、三角形は重複しません。
LOD 0 と衝突形状は元の対角線を維持します。高さクエリは Terrain 全体の正準 sample を使うため、
結果は分割サイズに依存しません。

レンダラー初期化後の地形構築では、独立した Compute キューで全グリッドの頂点、
三角形インデックス、平滑法線、tangent frame、自動レイヤー weight を生成し、
CPU に読み戻した完全 mesh をチャンク分割・PhysX cook・LOD に使用します。
位置と UV の軸および高さのスケーリングは CPU で準備し、既存の高さクエリと衝突形状の
浮動小数点位置を一致させます。GPU と CPU の法線・weight は演算精度の違いにより
完全には一致しない場合があります。急峻なレイヤー境界（傾斜ブレンドが1度以下）や
非正規化浮動小数点値を含む場合は、境界での誤判定を避けるため weight を CPU で補正します。
レンダラー初期化前のシーン読み込みなどでは従来の CPU
生成を使います。GPU の提出・同期に失敗した場合はエラーとして構築を中止し、適用済み地形を保持します。
読み戻しと一時的な完全 mesh のメモリ・処理コストがあるため、高速化は保証されません。
分割によって heightmap の上限が増えたり、out-of-core storage が使えるようになったりはしません。
分割変更では完全な Model、cook 済みチャンク asset、backend shape を staging してから、
稼働中のリソースを置き換えます。割り当て／cook／backend の失敗時は適用済み Terrain と衝突形状を維持します。
キュー済み model/material snapshot は submission fence が完了するまで古いチャンクを保持します。
ジオメトリ変更時は Character Controller の cache を無効にします。Contact/trigger の計数は
shape pair ごとに追跡しつつ、body が隣り合う複数チャンクに接触した場合も GameObject 単位の
event stream を dispatch します。Collider の設定と有効状態は全 shape に適用されます。
Debug collider/AABB 描画と physics shape 統計にはすべてのチャンクが含まれます。

公開 API:

```cpp
if (terrain->setChunkSettings({ 32 }))
{
    for (const auto& chunk : terrain->getChunks())
    {
        // 行優先の列／行、sampleX/sampleZ、cellsX/cellsZ、meshIndex、bounds。
        // 位置と bounds は Terrain 全体のローカル空間です。
    }
}
```

`getChunkSettings()` は適用済み設定を返します。最初の生成前は `getChunks()` は空です。
成功した再生成後は span／参照が無効になります。`getModel()` は引き続き、
行優先のチャンク mesh を含む生成 Model 全体を返します。`TerrainCollider::getChunkAssets()` は
同じ順序で不変の cook 済み asset を公開します。旧 `getPhysicsAsset()` が返すのは**最初のチャンクのみ**です。
生成済み model handle を外部から unload しないでください。

Terrain payload **version 4** は、埋め込み高さ、寸法、noise recipe、マテリアル設定とともに
チャンク cell 数を保存します。Scene/Prefab/Play は同じ分割を独立したジオメトリで復元し、
cook 済み衝突形状を再生成します。個別チャンクファイルや外部 physics ファイルは不要です。
Version 1..3 は従来の分割と見た目を維持するため1チャンク (`各辺1024 cells`) として読み込み、
次回保存時は現行 version 5 になります。Version 4 のチャンク設定が無効な場合は、
既存 Terrain を置き換えず拒否します。

## 距離 LOD とチャンクカリング (Phase 6)

LOD は Terrain payload version 1..4 の読み込み時を含め、**既定で無効**です。
**Enable Terrain LOD** をチェックし、下記の設定を編集してから **Apply LOD Settings** をクリックします。
適用には既存の適用済み heightmap、寸法、マテリアル、チャンク構成を使い、関係のない Inspector 下書きは
反映しません。再構築／インポート／noise／マテリアル／チャンク操作では、適用済み LOD 設定を維持します。

| コントロール | 意味 |
| --- | --- |
| Maximum LOD | 1..4。Level 0 はフル解像度、level N の内側は stride `2^N` |
| LOD Distance | 最初の切り替え距離 (world unit)、有限値0.1..1000000、既定値50 |

アクティブな各 render view は、各自のカメラ位置から、チャンク中心ではなく変換後の
チャンク AABB 上の**最も近い点**までの距離で LOD を選びます。親変換、回転、非一様 scale も考慮します。
距離が `D` 未満なら level 0、`[D,2D)` なら level 1、`[2D,4D)` なら level 2 とし、
利用可能な level と Maximum LOD の範囲に収めます。境界値ちょうどでは粗い level を選びます。
幅／奥行きが小さい、または端数のチャンクでは利用可能な level が少ない場合があります。
候補 level は三角形数を確実に減らす場合のみ採用します。幅または奥行きが2 cells 以下のチャンクは level 0 のままです。

すべての level で同じフル解像度 vertex buffer と属性を再利用します。内側は間引いてサンプリングし、
フル解像度の外周とは三角形の transition strip で接続します。**元の単位幅の境界 edge はすべて保持**するため、
隣接チャンクが個別に選択した level でも、skirt、隣接 level に依存する stitching、
毎フレームの mesh 再構築なしで正確に接続できます。共有境界の高さ、法線、tangent、UV、
レイヤー weight は変わりません。簡略化で内側のサーフェスは変化し、距離境界で急に切り替わると
pop が生じることがあります。この段階では geomorphing、hysteresis、screen-error による選択は行いません。

LOD が無効な場合も含め、マテリアル／draw の準備**前**にチャンク bounds をカメラ frustum と照合します。
視錐台の外にあるチャンクはカメラ向け color/depth item を送信しません。
選択された color、depth pre-pass、既存 Hi-Z draw は同じ index range を使います。
Shadow は**別の light frustum と level 0**を使います。カメラ外のチャンクも可視領域に影を落とせます。
カメラ LOD を変更しても shadow geometry は簡略化されません。Cast Shadows と camera layer は
通常のエンジン動作に従います。Frustum が無効な場合は保守的にカリングを迂回します。

正準高さクエリと PhysX は常にフル解像度三角形を使います。LOD 生成後に TerrainCollider を追加／再有効化しても、
追加された代替 index が重複する衝突ジオメトリとして cook されることはありません。
LOD range／設定はキュー済み model snapshot と対になる不変 snapshot です。
再生成、カメラ変更、所有者の破棄によってキュー済み draw の参照先が変わることはありません。

Renderer の Model 統計には可視／カリング済み／shadow チャンク数と、選択 LOD／フル解像度での
カメラ描画三角形数が表示されます。これは**GPU occlusion より前**の CPU 送信数であり、
GPU 上で可視な三角形数ではありません。`RendererStatistics::terrainLodChunks` から
選択 level の内訳を取得できます。LOD が削減するのは三角形数であり、vertex buffer の常駐量や
チャンク draw 数ではありません。代替 index range はメモリを増やします。
早期カリングは draw 送信を減らしますが、フル解像度 shadow のジオメトリコストは意図的に維持します。

```cpp
if (!terrain->setLodSettings({ true, 4, 50.0f }))
{
    // エラーがログに記録され、適用済みの Terrain と衝突形状は以前の状態を維持します。
}
```

`getLodSettings()` は適用済み設定を返します。生成前は `getLodSnapshot()` は null で、
生成後は `getModel()` と一致する不変チャンク index range を公開します。
`setLodSettings` は同期の authoring rebuild で、TerrainCollider がある場合はフル解像度での cook も行います。
Inspector の Apply は同じ CPU 準備をバックグラウンドにキューします。どちらも毎フレームの
カメラ更新を目的としていません。距離に応じた選択とカリングでは、毎フレーム cook／mesh 割り当てを
行わず、component state も変更しません。

Payload **version 5** は埋め込み sample、チャンク寸法、マテリアルとともに適用済み LOD 設定を保存します。
Scene/Prefab/Play は独立した生成リソースを復元します。Version 5 の LOD 設定が欠落／無効な場合は
トランザクション方式で拒否します。Version 1..4 は LOD 無効で読み込み、既存のチャンク移行を維持します。

## Terrain のバックグラウンド生成 (Phase 7)

Terrain Inspector の **Generate Noise**、**Create Flat**、**Import Heightmap**、
**Apply / Rebuild**、**Apply Terrain Material**、**Apply Chunk Settings**、**Apply LOD Settings** は、
バックグラウンド要求を送信するようになりました。追加のチェックボックスは不要です。
モーダルのファイル選択ダイアログは引き続きメインスレッドで動作しますが、画像 metadata の読み取り／
デコードと、時間のかかる CPU 準備処理は選択後に専用 Terrain worker で実行します。
Material texture reference の登録は owner thread で行い、テクスチャのデコード／upload には
既存の renderer texture 経路を使います。

Worker の処理手順:

1. 取得済みの高さをコピーし、CPU Perlin/FBM (または Phase 9 の明示的な GPU noise と readback)、
   平坦 sample の生成、WIC デコードのいずれかを行います。
2. 明示的な要求があった場合のみ hydraulic erosion (Phase 8) を適用し、グリッド全体の頂点、
   平滑法線、tangent、UV を生成します。
3. グリッド全体のマテリアル layer weight を計算してから、チャンクに分割します。
4. TerrainCollider がある場合、フル解像度の PhysX チャンクデータを cook します。
5. LOD index を追加し、適用済み植生 recipe (Phase 10) を散布して、CPU 準備結果を公開します。

要求は値 snapshot を所有します。Worker は component、Scene、resource registry、
renderer 所有リソース、PhysX scene／actor／shape に**アクセスしません**。
Phase 9 の GPU noise は独立した queue/buffer/fence のみを使います。
Cooking はローカルな parameter/output buffer を使い、不変の byte 列だけを生成します。
Terrain 間で遅延起動のバックグラウンド thread を1つ共有します。
協調動作する frame JobSystem とは独立しているため、frame/job waiter が数秒かかる Terrain build を
誤って実行することはありません。この queue 内の作業は直列で、Terrain／chunk／request ごとに
thread を作成しません。

**Background** 表示には Queued、Height samples、GPU noise、GPU readback、Hydraulic erosion、
Mesh、Material weights、Chunks、PhysX cooking、LOD、Vegetation、Ready to apply、Applied、Cancelled、
Failed が表示されます。これらは処理段階であり、等しい時間の進捗率ではありません。
完了後に build 全体と最終適用の所要時間を表示します。**Ready to apply** は新しい Terrain が
稼働中であることを意味しません。処理中も描画、衝突、サーフェスクエリ、model handle、
適用済み設定、保存済み sample は以前の Terrain を使います。要求を受理した未生成 component も、
自動の同期 fallback を実行したり不完全なデータを描画したりしません。

ゲーム／描画／physics の処理をすべて join した後、`Window::run` は
`TerrainGenerationQueue::applyCompleted()` を呼び出します。Worker の完了を待つことはなく、
1フレームで適用を試みる準備済み Terrain 結果は**最大1件**です。新しい heightmap、ジオメトリ、
マテリアル、チャンク／LOD 設定、noise recipe をまとめて確定する前に、resource 登録と
PhysX shape 置換を staging します。無効入力、デコード／cook の失敗、backend の失敗時は、
以前の適用済み Terrain を維持して診断を出力します。Build 中に TerrainCollider の有無が変わった場合は
結果を明示的に拒否します。現在の構成で再度要求してください。メインスレッドで予期せず recook することはありません。

**各 component では最後に受理した要求が優先されます。** 新しい要求は待機せず古い要求をキャンセルします。
キャンセル済みの待機入力は除去され、実行中の build は結果を破棄します。
Erosion は droplet/step ごと、noise は sample row ごと、cooking は chunk ごとに確認し、
残りの処理は CPU stage 間でキャンセルを確認します。WIC decode と個々の CPU stage／cook 済み chunk は
強制中断しません。送信済み GPU dispatch は worker の compute fence 完了後に buffer を再利用します。
GPU 作業を強制中断するのではなく、その出力を破棄します。**Cancel Terrain Generation** は
すでに Ready の出力も破棄します。無効な要求は有効な処理を置き換えません。同じ component に対する
同期編集／読み込み／複製が成功すると、保留中の非同期結果は無効になります。
破棄／削除時は callback が owner にアクセスする前にキャンセルし、ライフサイクル上の無効化時も
実行中のゲームプレイ build をキャンセルします。同じ GameObject GUID の置換 component に、
古い component の結果が適用されることはありません。

各 Apply ボタンは適用済み heightmap と**その他の適用済み設定**に、そのボタンの下書きを加えて取得します。
Mesh 生成／インポート／平坦化／noise 生成では mesh 寸法の下書きも取得します。
要求は以前の保留中 Apply と暗黙に統合されません。複数設定を同時に変更する場合は、
希望する完全な構成を `requestRebuild` で送信してください。送信後に下書きを編集しても取得済み build は
変わらず、完了時にも新しい Inspector 下書きを上書きしません。Scene/Prefab/Play snapshot には
適用済みデータのみを含みます。保留／キャンセル済み要求、進捗、未適用の下書きは保存、複製、
再実行されません。Phase 7 は payload **5** を維持し、Phase 8 では正準 sample とともに
最後に適用された erosion recipe を保存するため **6** を使用します。

既存の即時返却 API (`setTerrain`、`generateNoise`、`setMaterialSettings`、
`setChunkSettings`、`setLodSettings`) は同期の成功／失敗動作を維持します。
新しい owner-thread API が返すのは完了ではなく**要求の受理結果**です。

```cpp
if (terrain->requestNoise(recipe, meshSettings))
{
    // 現在の Terrain を使い続けます。後続の owner-thread update で:
    const auto status = terrain->getGenerationStatus();
    // status.stage == TerrainGenerationStage::Applied の場合のみ結果が有効です。
}
```

追加 API は `requestTerrain`、`requestFlat`、`requestHeightmap`、`requestRebuild`、
`cancelGeneration` です。既存の component API と同様、Scene、描画、その他の owner 操作と
直列化する必要があります。独自／headless loop では、ゲーム／描画／physics の作業をすべて join した後に
`TerrainGenerationQueue::applyCompleted()` を呼び出し、`PhysicsSystem::shutdown()` **より前**に
`shutdown()` を呼び出してください。Shutdown は worker をキャンセルして join しますが、
component の破棄や通常のキャンセルでは join しません。通常のエンジン loop と shutdown には
すでにこの処理が組み込まれています。

**制限:** この段階でフレームから取り除くのは時間のかかる CPU 準備処理であり、最終適用の全コストではありません。
PhysX shape の作成／接続と、renderer が最初に行う GPU buffer/texture upload は、
既存の安全なタイミングで同期実行されます。そのため大きな結果では適用／upload 時に一度だけ
急な負荷が発生することがあります。16 ms/frame を保証する streaming システムではありません。
生成中は以前の Terrain と独立した置換用データをメモリに保持するため、ピーク RAM が増加します。
チャンクは常駐し、段階的な GPU upload、距離に応じた streaming、部分的な Terrain 編集には対応しません。

## 水力侵食 (Phase 8)

Terrain Inspector で **Hydraulic Erosion** を開き、**Apply Hydraulic Erosion** をクリックします。
この操作は**適用済み** heightmap の snapshot を明示的に侵食します。未適用の下書きではなく、
他の**適用済み** mesh/material/chunk/LOD 設定を使います。専用 Terrain worker は seed 指定の
CPU droplet 処理を行い、mesh 全体、法線、tangent、layer weight、chunk、フル解像度 PhysX 衝突形状、
LOD range を再構築します。通常のフレーム境界トランザクションが成功するまで、以前の Terrain は
有効なままです。進捗に **Hydraulic erosion** stage が追加されます。キャンセルと最後に受理した要求を
優先する動作は変わりません。

シミュレーションでは高さ／勾配に双線形補間を使い、慣性に応じて1 cell 単位で進みます。
水量／速度に応じた土砂容量と蒸発も扱います。下り坂で高さが低下すると速度が増し、上り坂では減少します。
侵食には最寄り頂点を中心とする半径重み付き円盤を使い (マップ端では範囲を切り詰めて weight を正規化)、
除去量は step の下り高低差と利用可能な sample 高さで制限します。堆積土砂は双線形に分配し、
各 sample の正規化高さの残り容量を上限とします。最小／最大値に基づく再正規化は行いません。
平坦なマップは平坦なままで、正規化 sample は常に `[0,1]` の範囲内です。

**単位と境界:** 水平方向の距離／半径は grid cell 単位、垂直方向の高さは正規化 sample 値です。
Mesh 寸法、height scale、object transform、長方形 cell のワールド aspect ratio は
シミュレーションに影響しません。これは制作用モデルであり、物理的な水流ソルバーではありません。
Grid 解像度を上げると空間スケールが変化します。端は開放境界です。Grid 外へ出た droplet は、
運んでいる土砂を外部へ排出します。寿命、蒸発、または方向ベクトルが0で停止した droplet は
その場に堆積します。局所的な堆積容量を超える土砂も排出されます。
このため高さの総量は減少することがありますが、何もないところから増えることはありません。
`TerrainErosionBudget` は float 出力丸め前の侵食／堆積／排出合計を報告し、
侵食量は堆積量と排出量の合計に一致します。

| パラメーター | 既定値 | 許容範囲 |
|---|---:|---|
| Seed | 0 | uint32 |
| Droplets | 5000 | 1..1000000 |
| Droplet Lifetime | 64 | 1..256 |
| Erosion Radius | 3 | 1..8 cells |
| Inertia | 0.05 | 0..1 |
| Sediment Capacity | 4 | 0.01..100 |
| Minimum Slope | 0.001 | 0.00001..1 |
| Deposition / Erosion Rate | 0.3 / 0.3 | 0..1 |
| Evaporation | 0.01 | 0..1 per step |
| Gravity | 4 | 0..100 |
| Initial Water | 1 | 0.01..10 |
| Initial Speed | 1 | 0..10 |

同じビルド内では、global RNG の状態にかかわらず、同じ sample と recipe から同じ sample が得られます。
Droplet 数は解像度に応じて**自動調整されません**。処理コストは droplet 数、寿命、ブラシ面積に応じて増え、
極端な設定では時間がかかります。Droplet/step ごとに協調的なキャンセル確認を行いますが、
後続の mesh/cooking/upload と最終適用には Phase 7 の制限が引き続き適用されます。
**Reset Erosion Draft** は未適用のコントロールのみをリセットします。

```cpp
TerrainErosionSettings erosion;
erosion.seed = 723;
erosion.droplets = 5000;
if (terrain->requestErosion(erosion))
{
    // 要求は受理済みですが、未適用です。通常の owner-thread update で getGenerationStatus() を確認します。
}
```

`TerrainComponent::applyErosion` は同期処理の成功／失敗をトランザクション方式で返します。
`erodeTerrain(source, settings, destination, cancelled, budget)` は単体で使える CPU 処理で、
source と destination の alias に対応します。入力が無効、またはキャンセルされた場合は
destination と budget を維持します。Component API は無効な要求では保留中の処理を維持し、
新しい処理の置換／受理が成功した場合に限り古い処理をキャンセルします。

**永続化と累積編集:** Payload **6** は実際に侵食された sample と最後に正常適用された recipe を保存します。
Version **1..5** は sample を変更せず読み込み、erosion control を既定値にします。
Scene/Prefab/Play の複製で侵食処理を再実行することはありません。通常の Rebuild、material/chunk/LOD の変更、
save/load、noise／画像／平坦化による置換で自動的に侵食することもありません。
侵食を適用するたびに成功した変更が累積します。別の処理が保留中に送信した要求は、すべて現在の
適用済み sample を取得します。保留中の結果に連鎖適用するのではなく、既存の要求を置き換えます。
保留中要求と Inspector の新しい下書き編集は保存されません。
この段階では侵食の undo/history、雨量 mask、Terrain 間の水流、リアルタイム水描画、GPU 侵食には
対応しません。Phase 9 で高速化するのは noise 生成のみです。

## GPU Terrain Noise (Phase 9)

Terrain Inspector で **Generate Noise** の隣にある **Generate Noise (GPU)** をクリックします。
どちらの操作も同じ Perlin/FBM recipe と mesh 寸法の下書きを取得します。
通常のボタンと `generateNoise`／`requestNoise` は CPU 処理のままです。
GPU 実行は明示的に選択する操作であり、永続的な backend 設定や自動 fallback ではありません。

```cpp
if (terrain->requestGpuNoise(recipe, meshSettings))
{
    // 要求は受理済みです。status.stage == Applied になるまで以前の Terrain を使い続けます。
}
```

`TerrainNoise.hlsl` は8x8 thread の Compute group を実行し、グリッド外の thread を除外して、
各 sample の正規化 float 値を structured UAV に書き込みます。既存の noise parameter 全範囲、
すなわち長方形の2..1025 grid、Perlin/FBM、uint32 seed、負の offset、12 octaves、
大きな frequency/lacunarity に対応します。CPU 側で行うのは、分離可能な各軸の座標分解のみで、
double 精度を使います (`O((width + height) * octaves)`)。符号付き64-bit の格子 cell と
小数 offset を個別に upload するため、大きな offset／高 octave でも float 座標が潰れません。
Hash、勾配、5次補間、octave の累積、正規化は shader 内で実行します。
Hash 演算は32-bit pair と正確な下位64-bit 乗算を使い、任意の GPU double／Int64 機能には依存しません。

GPU の補間／累積には CPU の double ではなく float を使います。同じ shader/device/build では、
同じ GPU recipe から正確に同じ sample が得られますが、異なる device/build 間で bit 単位の一致は
保証しません。極端な設定や最大 grid を含む全検証 sample は、CPU noise との差が
**正規化高さ単位で2e-6以内**である必要があります。これは検証済みの許容誤差であり、
CPU/GPU の bit 単位一致を保証するものではありません。ワールド高さの差は Height Scale に比例します。
サーフェスを厳密に維持する必要がある場合は、別 backend で recipe を再生成せず、保存済み sample を使ってください。

Renderer は独自の D3D12 device 上に `TerrainGpuNoise` backend を1つ初期化します。
描画 queue/resource とは独立した **COMPUTE queue**、command allocator/list、不変 PSO、
upload/UAV/readback buffer、fence を所有します。専用 Terrain worker が dispatch し、fence を待って
高さを読み戻します。Owner-thread での要求受理と `applyCompleted` は compute fence を待ちません。
要求の完了に renderer frame は不要です。単体 backend の同時呼び出しは内部で直列化されます。
直接呼び出す `generate` method は CPU を block するため、frame callback 内での使用を想定していません。

Readback 後、既存の worker pipeline が**同じ高さ sample**から mesh 法線／tangent、
material weight、chunk、フル解像度 PhysX cook、LOD を構築します。
その後フレーム境界の transaction で Terrain を置き換えます。保存データ、高さ query、衝突形状、
描画結果がそれぞれ独立に再生成され、食い違うことはありません。Payload は **6** のままです。
正準 sample と既存の noise/erosion authoring recipe を保存し、queue、GPU handle、実行方式は保存しません。
Scene/Prefab/Play/再構築時に GPU 生成を再実行しません。

最後に受理した要求を優先する動作、無効入力時の既存データ保持、キャンセル、進捗、
新しい Inspector 下書きの保持、適用済みデータのみの保存は Phase 7 の仕様を維持します。
GPU 処理の失敗はログに記録して以前の Terrain を保持し、**CPU へ黙って fallback することはありません**。
**GPU noise** と **GPU readback** は処理 stage であり、進捗率ではありません。
表示される build 時間には CPU 座標準備、GPU 送信／待機／readback、その後の CPU Terrain 処理を含み、
GPU timestamp のみの時間ではありません。送信済み GPU 作業は強制キャンセルしません。
所有者が破棄された場合も compute fence の完了まで buffer を保持します。
Queue shutdown は renderer/device shutdown より前に worker を join し、その後 backend を drain/release します。
`DX12Renderer::finalize` がこの順序を保証します。独自／headless caller も同じ順序にし、
backend の initialize/shutdown を同時に呼び出さないでください。

通常の shader build は固定 DXC と Shader Model 6.0 で `TerrainNoise_csMain_cs.cso` をコンパイルします。
配布物にも含めてください。必須の rendering shader と同様、CSO の欠落／無効時は初期化に失敗したことを
明示します。Terrain PSO は処理中不変であり、この段階では hot reload しません。
Shader の変更を反映するには再ビルドして再起動してください。

**制限:** ここで GPU 化するのは Perlin/FBM による高さ生成のみです。画像デコード、
mesh/layer/chunk/LOD 構築、PhysX cooking、Phase 8 の順序依存 water-droplet erosion は CPU 処理のままです。
GPU 常駐 Terrain mesh、GPU erosion、距離 streaming、完全な GPU 駆動 Terrain pipeline ではありません。
GPU 作業は同じ hardware 上の描画と競合します。独立 queue でも同時実行、速度向上、特定 frame budget は
保証されません。Readback と旧／置換 Terrain によってメモリ使用量が増えます。
PhysX shape の最終接続と Renderer の初回 upload では、依然として適用時に負荷が発生する場合があります。

## 自動植生配置 (Phase 10)

**範囲:** `TerrainComponent` が所有する、明示的に有効化する seed 指定の静的 Model 散布です。
独立した4レイヤーを使います。植物ごとの GameObject や PhysX shape、風による変形、streaming、
植生専用の距離 LOD は作成しません。既存の静的 Model 向け frustum／layer／shadow カリング、
対応する opaque/alpha-test の GPU instancing、Hi-Z occlusion を再利用します。
Transparent material は Renderer の通常のソート済み非 instanced 経路を使います。

1. 既存の Model tool で静的 `.model`／`.mdl` asset を準備して保存します。上方向を +Y とし、
   原点を植物の根元に置きます。Model の mesh、node、埋め込み／既定 material、
   alpha-test material はそのまま保持されます。
2. **Terrain Vegetation** を展開して **Enable Vegetation** を有効にし、
   4つの **Vegetation Layer** のいずれかを開いて有効化し、**Select Model...** を選択します。
3. Seed、density、最小間隔、最大 instance 数、正規化 height/slope 範囲、一様 scale 範囲、
   根元 offset を設定します。Yaw は全周からランダムに選びます。
   **Align To Surface Normal** を有効にすると local +Y を幾何学的な三角形法線に正確に合わせます。
   無効の場合は植物を垂直に保ちます。Root offset は選択した上方向に沿って適用します。
   Shadow は layer ごとに設定でき、Terrain surface の Cast Shadows 設定とは独立しています。
4. **Apply Vegetation** をクリックします。置換全体が成功するまで以前の Terrain と植生を有効なまま保ちます。
   **Reset Vegetation Draft** は未適用の編集のみをリセットします。既存のキャンセル／最後に受理した要求を
   優先する動作と、新しい Inspector 下書きの保持は植生にも適用されます。

**配置規則:** 寸法、間隔、scale は Terrain の local 空間です。Height filter は height scale が0でも
元の正規化済み三角形補間 sample を使います。Slope には実際の local 幾何法線を使います。
Height/slope 範囲の境界値も含みます。接地位置は双線形補間ではなく、mesh の正確な
`a,c,b; b,c,d` 対角線に従います。Object／親の平行移動、回転、非一様 scale、反転は描画時に合成され、
seed や instance 配置を変更しません。

有効な各 layer について、worker は `ceil(sizeX * sizeZ * density)` 個の一様乱数 XZ 候補を
決定的な順序で評価します。Height/slope の条件外、または受理済み中心との距離が Minimum Spacing
未満の候補は除外されます。間隔は root offset 適用**前**の Terrain surface anchor で確認し、
layer ごとに独立して判定します。layer 間や Model bounding box 間では判定しません。
したがって `density` が決めるのは**候補数の上限**であり、最終的な配置数ではありません。
Filter／間隔条件により配置数は減少します。`maxInstances` に達すると配置を終了します。
同じ seed／規則の layer は意図的に同じ配置になります。分布を変えるには異なる seed を使用してください。

範囲は、local XZ 面積あたり density **0..100**、spacing **0.1..10000**、
layer ごとの最大 instance 数 **1..25000** (Terrain 全体で **100000**)、
正規化 height **0..1**、slope **0..90度**、scale **0.001..1000**、
root offset **-1000..1000** です。各 layer の候補数は最大 **100000** です。
上限を超える場合、黙って切り詰めたり無制限の処理を続けたりせず、明示的に失敗させます。
無効な layer の数値 field も検証します。Density 0 は有効で、instance を生成しません。
この上限は安全のための制約であり、可視モデル100000個が frame budget に収まる保証ではありません。

**再生成と永続化:** 通常の高さインポート、flat/noise/GPU 生成、侵食、mesh/material/chunk/LOD 再構築では、
現在の**適用済み**植生 recipe を取得し、更新後の正準 sample に対して配置を再計算します。
Chunk/LOD の変更は分布に影響しません。`setVegetationSettings` は同期処理です。
`requestVegetation` は専用 Terrain worker で準備します。CPU 配置処理は Scene object や asset manager に
アクセスしません。Model/material の読み込みと最終登録は owner thread で行います。
Model が見つからない、または skinned model の場合は明示的に失敗し、以前の mesh、植生、PhysX を
まとめて保持します。Asset の読み込み、physics shape の接続、GPU 初回 upload では、
引き続きフレーム負荷が生じることがあります。

Payload **7** は正準 height sample とともに、適用済み recipe と Model path を保存します。
Version **1..6** は植生を無効として移行します。Scene、Prefab、Play／複製では保存済み sample と recipe から
同じ local transform を再構築します。保留中の作業や新しい Inspector 下書きは保存されません。
植生で使う Model asset path は Model Renderer と同じ、作業ディレクトリ基準／絶対パスの参照規約に従います。
Model asset と material/texture の依存アセットも配布してください。Model/material は manager が共有する
resource であり、個々の植生 layer が所有したり unload したりしません。
キュー済み instance/model/material snapshot は再生成や Scene 破棄後も不変かつ有効です。
Model／既定 material の変更を反映するには植生を再適用してください。
