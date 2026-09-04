# DirectX 12向け HLSL Shader System 実装指示書

あなたはC++ / DirectX 12 / HLSL / DXCに精通したシニアゲームエンジンエンジニアとして実装してください。

現在開発している自作ゲームエンジンに、
「HLSLをDXCでコンパイルして.csoとして保存し、ゲーム実行時は.csoを読み込むShader System」
を実装してください。

さらに開発時にはHLSLの変更を自動検知してShader Hot Reloadを行えるようにしてください。

============================================================
# 1. 最重要方針
============================================================

以下を必ず守ってください。

- DirectX 12専用設計
- C++
- HLSL
- DXC (DirectX Shader Compiler) を使用する
- FXCは使用しない
- Shader Model 6.x系を使用する
- HLSL Language Versionは最新対応を前提とする
- 現在のDXCで利用可能な最新のHLSL機能を利用できる設計にする
- HLSL → DXC → DXIL → .cso
- Runtimeでは原則としてHLSLをコンパイルしない
- Runtimeでは.csoを読み込む
- Development / EditorビルドではHot Reloadを有効にする
- ReleaseビルドではHot Reloadを無効化できるようにする
- Shaderコンパイル失敗時に現在使用中のShaderを破壊しない
- Shader Hot Reloadによってゲームがクラッシュしない設計にする
- ShaderManagerとShaderCompilerを明確に分離する
- Render ThreadとShader File Watch処理を適切に分離する
- 既存のPSO / RootSignatureシステムと統合できる設計にする
- 将来的にCompute / Mesh / Amplification / Raytracing Shaderへ拡張できる設計にする

============================================================
# 2. HLSLコンパイラ
============================================================

HLSLのコンパイルにはDXCを使用してください。

FXCは使用禁止です。

基本的には以下のような構成にしてください。

HLSL
 ↓
DXC
 ↓
DXIL
 ↓
.cso
 ↓
ShaderManager
 ↓
ID3DBlob / shader bytecode
 ↓
Graphics PSO / Compute PSO / Raytracing Pipeline

DXCのCLIだけに依存する設計にせず、
将来的にIDxcCompiler3 / dxcompiler.dllを使用したインプロセスコンパイルへ
移行可能な設計にしてください。

可能であればShaderCompilerは以下の2方式を抽象化できる設計にしてください。

ShaderCompiler
 ├─ DxcCommandLineCompiler
 └─ DxcLibraryCompiler

ただし初期実装ではビルドツールとしてdxc.exeを利用しても構いません。

============================================================
# 3. HLSL Language Version
============================================================

HLSL Language Versionは最新対応を前提としてください。

現在のDXCで利用可能な最新HLSL言語仕様を使用してください。

特に、

-HV 2021

を基本設定として扱ってください。

ただし、

「未来のDXCバージョンでHLSLの新しいLanguage Versionが追加された場合に
コードを大量修正しなくても対応できる設計」

にしてください。

例えばShaderCompileSettingsに、

enum class HlslLanguageVersion
{
    Hlsl2016,
    Hlsl2017,
    Hlsl2018,
    Hlsl2021,
    Latest
};

のような抽象化を設けても構いません。

Latestを指定した場合は、
使用しているDXCがサポートする最新バージョンを利用できる設計を検討してください。

============================================================
# 4. Shader Model
============================================================

Shader Model 6.xを基本としてください。

Shader StageごとにTarget Profileを指定できるようにしてください。

例：

Vertex Shader
vs_6_x

Pixel Shader
ps_6_x

Compute Shader
cs_6_x

Geometry Shader
gs_6_x

Hull Shader
hs_6_x

Domain Shader
ds_6_x

Mesh Shader
ms_6_x

Amplification Shader
as_6_x

Ray Generation
lib_6_x

Miss
lib_6_x

Closest Hit
lib_6_x

Any Hit
lib_6_x

Intersection
lib_6_x

Callable
lib_6_x

Target Profileは文字列直書きではなく、
ShaderStage / ShaderModelから生成できる設計を推奨します。

============================================================
# 5. Shaderファイル構成
============================================================

以下のようなディレクトリ構成を作成してください。

Engine/
├─ Graphics/
│  ├─ Shader/
│  │  ├─ Shader.h
│  │  ├─ Shader.cpp
│  │  ├─ ShaderManager.h
│  │  ├─ ShaderManager.cpp
│  │  ├─ ShaderCompiler.h
│  │  ├─ ShaderCompiler.cpp
│  │  ├─ ShaderHotReload.h
│  │  ├─ ShaderHotReload.cpp
│  │  ├─ ShaderFileWatcher.h
│  │  ├─ ShaderFileWatcher.cpp
│  │  ├─ ShaderTypes.h
│  │  └─ ShaderCache.h
│  │
│  └─ DirectX12/
│     └─ Shader/
│
Shaders/
├─ Common/
│  ├─ Common.hlsli
│  ├─ Math.hlsli
│  └─ Lighting.hlsli
│
├─ PBR/
│  ├─ PBR.hlsl
│  └─ PBR.hlsli
│
├─ PostProcess/
│  ├─ Tonemap.hlsl
│  ├─ Bloom.hlsl
│  └─ FXAA.hlsl
│
└─ Compiled/
   ├─ PBR_VS.cso
   ├─ PBR_PS.cso
   └─ ...

Debug / Development環境では
HLSLとCompiledを分離してください。

============================================================
# 6. .cso生成
============================================================

HLSLから.csoを生成するShader Build Pipelineを作成してください。

例えば、

Tools/
└─ ShaderCompiler/
   ├─ dxc.exe
   ├─ dxcompiler.dll
   └─ ...

Scripts/
└─ Shaders/
   ├─ CompileShaders.bat
   └─ CompileShaders.ps1

Shaders/
└─ Compiled/

という構成にしてください。

可能ならPowerShellを本体にして、
BATはPowerShellを呼び出すだけの薄いラッパーにしてください。

例：

CompileShaders.bat
    ↓
CompileShaders.ps1
    ↓
DXC
    ↓
.cso

============================================================
# 7. Shader Compile設定
============================================================

Shaderごとに以下を指定できるようにしてください。

- Source File
- Entry Point
- Shader Stage
- Shader Model
- HLSL Version
- Include Directory
- Macro Definitions
- Debug / Release
- Optimization
- Debug Information
- Output File
- Compile Flags

例えば、

ShaderCompileDesc

を用意してください。

概念例：

struct ShaderCompileDesc
{
    std::filesystem::path sourcePath;
    std::string entryPoint;
    ShaderStage stage;

    ShaderModel shaderModel;

    HlslLanguageVersion languageVersion;

    std::vector<std::filesystem::path> includeDirectories;

    std::vector<std::pair<std::string, std::string>> defines;

    bool debug;
    bool optimize;
};

実際のプロジェクト規約に合わせて改善してください。

============================================================
# 8. Debug Build
============================================================

Debug / Developmentでは、

- デバッグ情報を可能な限り保持
- 最適化を無効または最小限
- Shader Debug情報を生成
- Hot Reloadを有効

にしてください。

Releaseでは、

- 最適化を有効
- 不要なDebug情報を除去
- Hot Reloadを無効
- Shaderは事前コンパイル済み.csoを使用

としてください。

============================================================
# 9. .csoの読み込み
============================================================

RuntimeではHLSLを読み込んでコンパイルしないでください。

.csoをバイナリとして読み込み、

D3D12_SHADER_BYTECODE

に設定できる形式にしてください。

Shaderクラスは概念的に、

class Shader
{
public:

    bool load(const std::filesystem::path& path);

    const void* getByteCode() const;
    size_t getByteCodeSize() const;

private:

    std::vector<std::byte> m_byteCode;
};

のような構造を基本にしてください。

ただし実際のエンジン設計に合わせて、
std::vector<uint8_t>など適切な形式を選択してください。

============================================================
# 10. ShaderManager
============================================================

ShaderManagerを実装してください。

責務：

- Shader登録
- Shader検索
- .cso読み込み
- Shader Cache
- Shader Compile情報管理
- Hot Reload管理
- Shader変更通知
- PSO再生成通知

例えば、

ShaderID

を利用できるようにしてください。

例：

enum class ShaderID
{
    PBRVS,
    PBRPS,
    TonemapVS,
    TonemapPS,
    BloomCS,
    ...
};

ただしShaderIDの設計は既存ShaderManagerの設計を優先してください。

ShaderIDとShaderDescを紐付け、

ShaderID
 ↓
ShaderDesc
 ↓
.cso
 ↓
Shader Object

という関係にしてください。

============================================================
# 11. Shader Cache
============================================================

Shaderのロード時に毎回ファイルを解析するような設計は避けてください。

以下をキャッシュしてください。

- ShaderID
- Source Path
- Compiled Path
- Entry Point
- Shader Stage
- Shader Model
- HLSL Version
- Macro
- File Timestamp
- Dependency Files
- Compile Hash
- Bytecode

可能ならShader Compile Metadataを

ShaderCache.json

または

ShaderCache.bin

などに保存できるようにしてください。

ただしRuntimeではJSON解析を必須にしないでください。

============================================================
# 12. Include依存関係
============================================================

HLSLには、

#include

があります。

例えば、

PBR.hlsl
 ↓
Common.hlsli
 ↓
Lighting.hlsli

のような依存関係があります。

そのためHot Reloadでは、
PBR.hlslだけではなく、

PBR.hlsli
Common.hlsli
Lighting.hlsli

などのIncludeファイル変更も検知してください。

Shader Dependency Graphを管理してください。

概念：

PBR.hlsl
 ├─ Common.hlsli
 ├─ Math.hlsli
 └─ Lighting.hlsli

Common.hlsli
 └─ Math.hlsli

Math.hlsliが変更された場合、

PBR.hlsl

を再コンパイルする必要があります。

============================================================
# 13. Shader Hot Reload
============================================================

Shader Hot Reloadを実装してください。

開発中に、

Shaders/PBR/PBR.hlsl

を編集して保存した場合、

1. FileWatcherが変更を検知
2. 変更Shaderを特定
3. Include依存関係を確認
4. Shaderを再コンパイル
5. コンパイル成功
6. 新しい.csoを生成
7. Shader ByteCodeを更新
8. 関連PSOを再生成
9. GPU安全性を確認
10. 新しいPSOへ切り替え

という流れにしてください。

============================================================
# 14. コンパイル失敗時の挙動
============================================================

非常に重要です。

HLSLにコンパイルエラーがあった場合、

現在正常に動作しているShaderを破壊してはいけません。

例えば、

Old Shader
   ↓
Hot Reload
   ↓
Compile Error
   ↓
Old Shaderを維持

としてください。

エラー内容はLoggerへ出力してください。

例：

[Shader][Error]
Failed to compile:
Shaders/PBR/PBR.hlsl

PBR.hlsl:42:15:
error: unknown identifier 'xxx'

というように、

- Shader Path
- Entry Point
- Error
- Warning
- DXC Output

を分かりやすく表示してください。

============================================================
# 15. Hot Reloadのスレッド設計
============================================================

FileWatcherがRender Threadを直接操作してはいけません。

推奨：

FileWatcher Thread
       ↓
Shader Change Queue
       ↓
ShaderManager
       ↓
Compile Worker Thread
       ↓
Compile Result Queue
       ↓
Render Thread
       ↓
PSO Rebuild
       ↓
Swap

という構造にしてください。

特に、

ID3D12PipelineState

や

ID3D12RootSignature

の差し替えタイミングには注意してください。

GPUが古いPSOを使用中に破棄しないようにしてください。

FenceによるGPU完了確認を利用し、
安全に旧PSOを破棄してください。

============================================================
# 16. PSO Hot Reload
============================================================

Shader Hot ReloadではShader ByteCodeだけを交換して終了ではありません。

Graphics PSOの場合、

VS
PS
GS
HS
DS

などのShader変更があった場合、
関連PSOを再生成してください。

例えば、

PBR_VS
PBR_PS
 ↓
PBR Graphics PSO

という関係を管理してください。

PSOManagerとShaderManagerが密結合にならないよう、

ShaderChangedEvent

や

ShaderReloadResult

などを利用して通知してください。

例：

ShaderManager
    ↓
ShaderReloadedEvent
    ↓
PSOManager
    ↓
RebuildPSO
    ↓
New PSO

============================================================
# 17. Root Signature
============================================================

Root SignatureはShaderとは分離して管理してください。

ただし将来的には、

HLSL Root Signature

または

Shader Reflection

を利用してRoot Signatureを自動生成できるような拡張余地を残してください。

初期実装では既存RootSignatureManagerを使用してください。

既存エンジンにRootSignatureManagerが存在する場合、
新しいShaderManagerから重複実装しないでください。

============================================================
# 18. Shader Reflection
============================================================

DXC / DXIL Shader Reflectionを利用できるようにしてください。

将来的に以下を取得できる設計にしてください。

- Constant Buffer
- SRV
- UAV
- Sampler
- StructuredBuffer
- Texture
- Resource Register
- Variable
- Input Semantic
- Output Semantic
- Thread Group Size

ただし初期実装でReflectionが不要なら、
ShaderReflectionクラスを作成して拡張可能な状態にしてください。

============================================================
# 19. Shader Metadata
============================================================

Shader Metadataを持てるようにしてください。

例：

ShaderDesc
{
    source = "Shaders/PBR/PBR.hlsl";
    entryPoint = "VSMain";
    stage = Vertex;
    profile = "vs_6_9";
}

ただしShader Model 6.9が実行環境で利用可能とは限らないため、
Runtime起動時にD3D12_FEATURE_SHADER_MODELを使用して
GPUがサポートしているShader Modelを確認してください。

必要に応じて、

6.9
 ↓
6.8
 ↓
6.7
 ↓
...

のようなFallbackを設計できるようにしてください。

============================================================
# 20. 最新Shader Modelへの対応
============================================================

Shader Modelをコード全体にハードコードしないでください。

例えば、

#define SHADER_MODEL "6_9"

のような固定設計は禁止です。

ShaderModel enum / utilityを作成し、

ShaderModel::SM_6_0
ShaderModel::SM_6_1
...
ShaderModel::SM_6_9

などを扱えるようにしてください。

将来SM 6.10などが登場しても変更箇所を最小化してください。

ただし、

「最新だから必ず6.9を使う」

という実装にはしないでください。

実際にGPU / Driver / D3D12 Deviceが対応しているShader Modelを
CheckFeatureSupportで確認してください。

============================================================
# 21. Shader Build Manifest
============================================================

可能であればShader Build Manifestを作成してください。

例：

Shaders/
└─ ShaderManifest.json

概念：

{
    "shaders": [
        {
            "name": "PBRVS",
            "source": "PBR/PBR.hlsl",
            "entry": "VSMain",
            "stage": "vertex",
            "profile": "vs_6_9"
        },
        {
            "name": "PBRPS",
            "source": "PBR/PBR.hlsl",
            "entry": "PSMain",
            "stage": "pixel",
            "profile": "ps_6_9"
        }
    ]
}

ただしRuntimeでJSONを必須にしないでください。

Build時にManifestから.csoを生成できる設計を優先してください。

============================================================
# 22. Build Pipeline
============================================================

Visual Studioからビルドした場合でもShaderを自動コンパイルできるようにしてください。

理想：

C++ Build
 ↓
Shader Build
 ↓
HLSL Compile
 ↓
.cso生成
 ↓
C++ Build

ただしShader変更時にC++全体を再ビルドする必要はありません。

Shaderだけ変更した場合、

HLSL
 ↓
DXC
 ↓
.cso

だけが実行されるようにしてください。

可能ならVisual Studio / MSBuildとの統合も行ってください。

============================================================
# 23. Hot Reload対象
============================================================

以下をHot Reload対象にしてください。

.hlsl
.hlsli

ただし、

Generated File
Compiled File
Cache File

は監視対象から除外してください。

============================================================
# 24. FileWatcher
============================================================

FileWatcherはWindows環境に最適化してください。

候補：

ReadDirectoryChangesW

を利用してください。

ポーリング方式ではなく、
Windowsのファイル変更通知を利用してください。

以下を検出してください。

- FILE_ACTION_ADDED
- FILE_ACTION_REMOVED
- FILE_ACTION_MODIFIED
- FILE_ACTION_RENAMED_OLD_NAME
- FILE_ACTION_RENAMED_NEW_NAME

短時間に複数回変更イベントが来る場合があるため、
Debounceを実装してください。

例：

Shader保存
 ↓
複数File Event
 ↓
100～300ms程度待機
 ↓
最終状態を確認
 ↓
1回だけCompile

具体的な値は設定可能にしてください。

============================================================
# 25. Shader Compile Queue
============================================================

Shader CompileをJob System / Thread Poolで実行できるようにしてください。

Render ThreadでDXCコンパイルを直接実行して
フレームを停止させないでください。

例えば、

ShaderCompileQueue

ShaderCompileWorker

ShaderCompileResult

を作成してください。

============================================================
# 26. Runtime / Editorモード
============================================================

Shader Systemにモードを持たせてください。

ShaderMode:

Runtime
Editor
Development

Runtime:
- .csoのみ
- Hot Reload OFF
- HLSL Compile OFF

Editor:
- .cso使用
- Hot Reload ON
- 必要に応じてHLSL再コンパイル

Development:
- .cso使用
- Hot Reload ON
- Debug Shader Compile

Release:
- .cso使用
- Hot Reload OFF
- HLSLを配布しなくても動作可能

============================================================
# 27. CSOのTimestamp / Hash
============================================================

.csoが古い場合の検出を実装してください。

以下のいずれかを使用してください。

- Source Timestamp
- Include Timestamp
- SHA-256
- Compile Hash

推奨：

Shader Source
+ Include Files
+ Compile Options
+ DXC Version
+ HLSL Version
+ Shader Model
+ Macro

からCompile Hashを作成してください。

Hashが一致する場合は再コンパイル不要としてください。

============================================================
# 28. DXC Version
============================================================

Shader CacheにはDXC Compiler Versionも保存してください。

例えば、

DXC Version
HLSL Version
Shader Model

が変更された場合、
古い.csoを再利用せず再コンパイルしてください。

============================================================
# 29. エラー処理
============================================================

Shader関連エラーは例外だけに依存しないでください。

ShaderCompileResultを返してください。

例：

struct ShaderCompileResult
{
    bool success;

    std::vector<std::byte> byteCode;

    std::string errorMessage;
    std::string warningMessage;

    std::filesystem::path sourcePath;

    ShaderCompileDiagnostics diagnostics;
};

成功 / 失敗 / Warningを明確に分離してください。

============================================================
# 30. Logger統合
============================================================

既存Logger / spdlogが存在する場合は利用してください。

Shader専用ログカテゴリを作成してください。

例：

[Shader]
[ShaderCompiler]
[ShaderHotReload]
[ShaderCache]

ログ例：

[ShaderCompiler] Compiling PBR.hlsl
[ShaderCompiler] EntryPoint: VSMain
[ShaderCompiler] Target: vs_6_9
[ShaderCompiler] HLSL: 2021

[ShaderCompiler] Compilation succeeded
[ShaderCompiler] Output: PBR_VS.cso

[ShaderHotReload] Detected change: PBR.hlsli
[ShaderHotReload] Recompiling dependent shader: PBR.hlsl

============================================================
# 31. ImGui Debug UI
============================================================

既存ImGui Editorが存在する場合、
Shader Debug Windowを作成してください。

表示内容：

Shader List

Shader Name
Stage
Entry Point
Target
Source
CSO
Status
Last Compile
Last Reload
Compile Error

など。

例えば、

Shader Manager
---------------------------------
PBR VS     [OK] [Reload]
PBR PS     [OK] [Reload]
Tonemap PS [OK] [Reload]

[Reload All]
[Recompile All]
[Clear Cache]

のようなUIを作成してください。

個別Shaderの手動Reloadも可能にしてください。

============================================================
# 32. Shader Status
============================================================

Shader状態をenumで管理してください。

例：

enum class ShaderStatus
{
    Unloaded,
    Loading,
    Loaded,
    Compiling,
    CompileFailed,
    Reloading,
    ReloadFailed
};

============================================================
# 33. Shader Handle
============================================================

Shaderへの参照は可能なら、

ShaderID

または

ShaderHandle

を利用してください。

ShaderManager内部のコンテナ変更によって
外部参照が壊れないようにしてください。

============================================================
# 34. PSOとの関係
============================================================

以下の依存関係を明確にしてください。

Shader
 ↓
ShaderByteCode
 ↓
PSO
 ↓
Renderer

Shader変更時：

HLSL
 ↓
DXC
 ↓
CSO
 ↓
ShaderManager
 ↓
ShaderChanged
 ↓
PSOManager
 ↓
PSO Rebuild
 ↓
Renderer

============================================================
# 35. GPU安全性
============================================================

Hot Reloadで最も重要なポイントです。

古いPSOやShader関連リソースを
GPUが使用中に破棄しないでください。

既存Fenceシステムを利用してください。

概念：

Old PSO
 ↓
GPU使用中
 ↓
New PSO生成
 ↓
New PSOへ切り替え
 ↓
Fence待ち
 ↓
Old PSO Release

としてください。

============================================================
# 36. Shader Compile Cache
============================================================

同じShaderを何度もコンパイルしないようにしてください。

例えば、

Shaders/.cache/

を作成して、

ShaderHash
CompilerVersion
CompileOptions

を利用してCacheを管理してください。

ただし最終配布物には
不要なCacheを含めないでください。

============================================================
# 37. .cso Naming
============================================================

以下のような命名規則を採用してください。

<ShaderName>_<EntryPoint>_<Stage>.cso

例：

PBR_VSMain_VS.cso
PBR_PSMain_PS.cso
Bloom_CSMain_CS.cso

または既存プロジェクトの命名規則がある場合はそちらを優先してください。

============================================================
# 38. Compile Script
============================================================

最低限以下を作成してください。

Scripts/
└─ Shaders/
   ├─ CompileShaders.bat
   ├─ CompileShaders.ps1
   └─ CleanShaderCache.bat

CompileShaders.batでは、

- DXC存在確認
- Shader Directory確認
- Output Directory確認
- CompileShaders.ps1実行
- エラー時ERRORLEVELを返す

を実装してください。

============================================================
# 39. CI / Build
============================================================

可能であればGitHub ActionsなどのCI環境でもShader Compileが可能な設計にしてください。

Shader Compile Errorがある場合は
CI Buildを失敗させてください。

============================================================
# 40. Doxygen
============================================================

EngineコードにはDoxygenコメントを付けてください。

例：

/**
 * @brief HLSL Shaderを管理するクラス。
 *
 * Shaderのロード、キャッシュ、Hot Reload、
 * Shader依存関係の管理を担当する。
 */
class ShaderManager
{
};

public / protected / complex private functionには
必要に応じてDoxygenコメントを付けてください。

============================================================
# 41. スレッドセーフ
============================================================

ShaderManagerはマルチスレッドアクセスを考慮してください。

特に、

FileWatcher
Compile Worker
Render Thread
ImGui Thread

からアクセスされる可能性があります。

std::mutex
std::shared_mutex
Concurrent Queue

など適切な方式を使用してください。

ただし不要なロックをRender Threadへ持ち込まないでください。

============================================================
# 42. Hot Reloadの安全な切り替え
============================================================

Shader ReloadはAtomic Swapまたは
Render Thread上で安全にSwapできる構造にしてください。

Compile Worker Threadから
直接Graphics PSOを変更してはいけません。

Compile Worker
 ↓
CompileResult
 ↓
Main/Render Thread
 ↓
Create PSO
 ↓
Swap
 ↓
Retire Old PSO

としてください。

============================================================
# 43. Shader Compile Options
============================================================

Debug:

- -HV 2021
- Debug Info
- Optimization OFF

Release:

- -HV 2021
- Optimization ON
- Debug Info OFF

など、適切なDXCオプションを使用してください。

ただし具体的なDXCオプションは
現在利用しているDXCの仕様を確認し、
非推奨・廃止されたオプションを使用しないでください。

============================================================
# 44. 最新DXC対応
============================================================

DXCはプロジェクトに固定バージョンを明示できるようにしてください。

「開発PCのWindows SDKに入っているdxc.exeを暗黙に使用」
だけに依存しないでください。

理想：

ThirdParty/
└─ DirectXShaderCompiler/
   ├─ bin/
   │  └─ dxc.exe
   ├─ lib/
   └─ include/

または

Tools/
└─ DXC/

としてください。

ただしリポジトリサイズなどを考慮し、
Git LFSやRelease Artifactなどを利用できる構造でも構いません。

重要なのは、

「どのDXCでShaderをコンパイルしたか再現可能」

であることです。

============================================================
# 45. Shader Compiler Detection
============================================================

DXCの起動時に、

- DXC Path
- DXC Version
- HLSL Language Version
- Supported Shader Model

を検出してLoggerへ出してください。

例：

DXC
Version: x.x.x.x
HLSL: 2021
Highest Requested Shader Model: 6.9

============================================================
# 46. Runtime Shader Model Capability
============================================================

DirectX 12 Device生成後、

D3D12_FEATURE_SHADER_MODEL

を使用してGPUが対応しているShader Modelを取得してください。

Shader Model 6.9などを要求しているShaderが
GPUで使用できない場合は、
明確なエラーを出してください。

将来的なFallbackにも対応できるようにしてください。

============================================================
# 47. Shader Build Tool
============================================================

Shaderコンパイルを専用ツールとして切り出してください。

例：

Tools/
└─ ShaderCompiler/
   ├─ ShaderCompiler.exe
   └─ ...

最終的には、

ShaderCompiler.exe
    --input Shaders/
    --output Shaders/Compiled/
    --manifest ShaderManifest.json

のように実行できる形を目指してください。

ただし最初はBAT / PowerShellベースでも構いません。

============================================================
# 48. 初期実装対象
============================================================

まず以下だけを完成させてください。

1.
DXC導入

2.
HLSL → .cso Compile

3.
.cso Runtime Load

4.
ShaderManager

5.
ShaderID

6.
ShaderCompileDesc

7.
ShaderCompiler

8.
FileWatcher

9.
Shader Hot Reload

10.
PSO Rebuild Notification

11.
ImGui Shader Debug Window

12.
Compile Script

13.
Doxygen

============================================================
# 49. テスト
============================================================

以下のテストを作成してください。

Test 1:
HLSLを正常コンパイル

Test 2:
.csoを正常ロード

Test 3:
Shader Compile Error

Test 4:
Compile Error後に旧Shaderが維持される

Test 5:
HLSL変更検知

Test 6:
HLSL Hot Reload

Test 7:
.hlsli変更による依存Shader Reload

Test 8:
複数Shader同時変更

Test 9:
短時間に大量のFile Event

Test 10:
PSO再生成

Test 11:
GPU使用中のOld PSOを安全に破棄

Test 12:
Shader Cache Hit

Test 13:
Shader Cache Miss

Test 14:
DXC Version変更時の再コンパイル

Test 15:
Shader Model非対応GPU

============================================================
# 50. 実装時の重要ルール
============================================================

既存コードを壊さないでください。

既存の、

- ShaderManager
- RootSignatureManager
- PSOManager
- DescriptorHeapManager
- Device
- Queue
- Fence
- Logger
- ImGui

が存在する場合は、
それらを確認して統合してください。

同じ機能を二重実装しないでください。

既存コードを確認してから実装してください。

============================================================
# 51. 実装手順
============================================================

以下の順番で実装してください。

STEP 1
現在のShader関連コードを調査

STEP 2
既存ShaderManager / PSOManager / RootSignatureManagerを確認

STEP 3
現在のHLSL配置を確認

STEP 4
DXC導入方法を決定

STEP 5
ShaderTypes作成

STEP 6
ShaderCompileDesc作成

STEP 7
ShaderCompiler実装

STEP 8
HLSL → CSO Compile Script作成

STEP 9
Shaderクラス実装

STEP 10
ShaderManager実装

STEP 11
CSO Runtime Load実装

STEP 12
Shader Cache実装

STEP 13
FileWatcher実装

STEP 14
Shader Dependency Tracking実装

STEP 15
Hot Reload実装

STEP 16
PSO Reload Notification実装

STEP 17
GPU-safe PSO Swap実装

STEP 18
ImGui Shader Debug UI実装

STEP 19
Tests実装

STEP 20
Build確認

============================================================
# 52. 完了条件
============================================================

以下をすべて満たしたら実装完了としてください。

[ ] HLSLをDXCでコンパイルできる

[ ] .csoが生成される

[ ] Runtimeで.csoをロードできる

[ ] RuntimeでHLSLコンパイルを行わない

[ ] HLSL 2021を使用できる

[ ] Shader Model 6.xに対応

[ ] 将来のShader Model追加に対応しやすい

[ ] DXC Versionを管理できる

[ ] Shader Cacheが動作する

[ ] HLSL変更を検知できる

[ ] HLSL Hot Reloadが動作する

[ ] HLSL Include変更を検知できる

[ ] Compile Error時に旧Shaderを維持する

[ ] Compile WorkerがRender Threadをブロックしない

[ ] PSOを安全に再生成できる

[ ] GPU使用中のリソースを破棄しない

[ ] ImGuiからShader状態を確認できる

[ ] 手動Reloadできる

[ ] Reload Allできる

[ ] Release BuildでHot Reloadを無効化できる

[ ] Doxygenコメントが付いている

[ ] エラー処理が適切

[ ] Thread Safe

[ ] Build Scriptが存在する

[ ] CIでShader Compile Errorを検出できる

[ ] 既存エンジンのShader / PSO / RootSignature構成と統合されている

============================================================
# 53. 最後に
============================================================

実装開始前に必ず既存プロジェクトを調査してください。

特に以下を確認してください。

- ShaderManager
- Shader
- PSOManager
- PSOCreator
- RootSignatureManager
- Device
- CommandQueue
- Fence
- Logger
- ImGui
- HLSL directory
- Visual Studio project
- CMakeがある場合はCMake構成

既存設計と矛盾する場合は、
既存コードを尊重しながら最も保守性の高い設計にしてください。

実装後は、

1. 変更したファイル一覧
2. 新規作成したファイル一覧
3. Shader Pipelineの構成
4. DXC設定
5. Hot Reloadの仕組み
6. PSO再生成の仕組み
7. GPU-safe Resource Lifetime
8. Build方法
9. 使用方法
10. テスト結果
11. 残っている課題

を日本語で報告してください。