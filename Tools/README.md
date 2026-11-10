# Build and repository layout

The engine supports **Debug x64** and **Release x64** only. Use Visual Studio
2026 C++ tools (v145), the Windows SDK, and Autodesk FBX SDK 2020.3.9.
The vendored DirectXTex project is built with the engine's selected toolset,
without editing its upstream project file.
`FBXSDKRoot` may be passed as an MSBuild property to select another SDK location.

Build from a Visual Studio Developer PowerShell:

```powershell
MSBuild .\GameEngine.sln /m /p:Configuration=Debug /p:Platform=x64
MSBuild .\GameEngine.sln /m /p:Configuration=Release /p:Platform=x64
```

The project builds PhysX, ozz-animation and DirectXTex and generates FlatBuffers
headers and compiled shaders before compiling the engine. No existing dependency
libraries or generated headers are required. DXC and flatc are supplied in
`Tools`. Compilation errors are not bypassed; engine warnings remain errors.
PhysX and ozz-animation use content-tracked incremental builds. Before compiling
the engine, MSBuild hashes dependency source/build inputs and checks the required
libraries and generated CMake projects. Unchanged dependencies do not launch
PowerShell or CMake. DirectXTex retains its own MSBuild incremental checks.

## Incremental native dependencies

`Tools\Build\NativeDependencies.targets` maintains separate Debug/Release
records under `Build\GameEngine\x64\<Configuration>\NativeDependencies`.
The records are regenerable build state, not checked-in libraries or a way to
ignore compilation failures.

- SHA-256 fingerprints cover PhysX's `physx` tree and ozz's `src`, `include`,
  `extern`, `build-utils` and top-level CMake configuration, plus the engine's
  dependency scripts/targets. File addition, deletion and content changes are
  detected, even when timestamps are restored. Engine-only C++ edits do not
  invalidate dependency records.
- ozz Git HEAD/refs are included so changing the pinned submodule revision still
  reaches the existing revision check. Test/sample/media directories are not
  inputs to the disabled ozz test/tool/sample builds.
- Configuration, toolset/SDK selection, compiler/linker flags, CMake and native
  tool binaries are tracked. MSBuild passes its selected Visual Studio instance,
  CMake executable, VC toolset version and Windows SDK selection to both scripts.
  `DependencyCMakeExecutable` can select an alternate CMake explicitly.
- A changed fingerprint or CMake cache causes a clean build of that dependency's
  selected configuration. This is deliberately conservative: CMake/MSBuild's
  inner timestamp checks alone cannot detect backdated source or tool changes.
  Other dependencies and the other configuration are not marked successful by it.
- Every required linked library, CMake cache, generation stamp and recorded
  generated project must exist before skipping. Deleting an output or cleaning
  a build tree triggers recovery instead of trusting an old success record.
- Linked libraries are also hashed against the last successful build. Replacing
  or corrupting an archive, including a manual build with different flags, cannot
  silently reuse the previous configuration's success record.
- A success record is written only after configure/build and output checks
  succeed. A failed build removes that configuration's record, so the next
  invocation retries. Shared CMake cache changes are detected independently by
  both configurations. Use one build at a time for the shared dependency trees.

The first build after enabling/changing these rules rebuilds the dependencies;
subsequent unchanged builds only perform the fingerprint/output checks.
`Clean` clears the current configuration's records, and `Rebuild` establishes
them again. To rebuild an individual dependency manually, the existing
`Tools\Animation\build_ozz.ps1` and `Tools\Physics\build_physx.ps1` still work;
pass `-CleanFirst` when a clean dependency compilation is required. Do not
disable these checks merely because a `.lib` already exists.

## Incremental shaders

`Tools\Build\Shaders.targets` is the single build entry point for all 32 built-in
shader entry points, including Terrain and every post-process pass. The job list
and compiler options live in `Tools\Shaders\CompileShaders.ps1`; Debug and Release
share the same bytecode and records because their shader options are identical.
The existing optimization/debug options are preserved, including Terrain's
warnings-as-errors option.

- Unchanged solution builds hash shader inputs, DXC binaries and recorded
  outputs without starting PowerShell or DXC.
- When inputs change, the script checks each entry point separately using
  SHA-256 records and the actual transitive includes emitted by DXC's `-MD`.
  Editing a source rebuilds only its entry points; editing an include rebuilds
  only its consumers. Restoring timestamps does not hide content changes.
- Missing or modified bytecode rebuilds only that output. Missing per-entry
  records are regenerated. File additions/deletions conservatively invalidate
  all entries because they can change include resolution. DXC/script changes
  also invalidate all entries.
- Bytecode is compiled to a temporary file before publication. A compiler error
  fails the build, removes that entry's success record, and preserves its last
  bytecode only as an old artifact, never as a successful build fallback.
- Records live under `Assets\Shaders\Compiled\.build`. Use
  `Tools\Shaders\CompileShaders.bat` for the same incremental behavior manually,
  or add `-Force` to compile every entry. `CleanShaderCache.bat` removes bytecode
  and records, so the next build recompiles all shaders. Normal C++ Clean/Rebuild
  does not discard unchanged, configuration-independent shader artifacts.

Run one shader build at a time; the outputs are shared. Runtime hot reload and
custom post-process shader compilation are unchanged. Invalid state-file JSON
is reported as an error; use `-Force`, remove the affected record or clean the
shader cache to regenerate it.

## Precompiled headers and engine dependencies

`Source\Pch.h` contains stable STL, Windows/DirectX SDK and vendored spdlog
headers only. It must not include engine feature headers, even indirectly.
In particular, keep `Core\Core.h`, GameObject/Component, Scene, Prefab, input,
time and job-system headers out of the PCH; changing them should rebuild their
consumers, not every engine translation unit.

- Keep `#include "Pch.h"` first in engine `.cpp` files, followed by the
  implementation's header and explicit dependencies for the APIs used.
- Engine headers include their own declaration, inline implementation and macro
  dependencies. Prefer forward declarations for non-owning incomplete types
  where an implementation does not require the definition. Do not rely on an
  unrelated `.cpp` include or the former Core umbrella to supply engine types.
- `Core\Core.h` remains an opt-in convenience header for clients that want the
  whole foundation API. Engine implementation files use individual headers;
  adding a future API to the umbrella must not invalidate the PCH.
- Compiler settings, ABI, runtime behavior and Debug/Release configuration
  choices are unchanged. PCH/STL/SDK changes still legitimately rebuild all PCH
  consumers; this change reduces feature-header edit fanout, not a guarantee of
  faster clean builds.

## Repository directories

- `Source`: engine and editor C++ source, including public extension APIs.
- `Assets`: authoring assets and HLSL source; preserve scenes, prefabs and models.
- `Schemas`: authoritative FlatBuffers schemas.
- `Library`: third-party source and licenses. Do not treat upstream build scripts,
  tests, documentation or sample data as engine-generated waste.
- `Tools`: dependency builds and offline asset generation.
- `Build`: regenerable dependency output and engine intermediate files.
  Engine intermediates live in `Build\GameEngine\x64\<Configuration>`.
- `Generated\FlatBuffers`: regenerable headers; never edit by hand.
- `x64\<Configuration>`: executable, runtime DLL and debugging symbols.
- `Config`, `Log`, `.vs`: local editor preferences, logs and IDE caches.
  Preserve preferences; do not remove IDE caches while the IDE is using them.

### Generated-output cleanup boundaries

The active build directories under `Build` are `GameEngine`, `PhysX` and
`ozz-animation`. Preserve these for incremental builds, along with
`Generated\FlatBuffers`, `Assets\Shaders\Compiled` and the executable directories.
Keep `GameEngine.exe`, its `.pdb`, and `libfbxsdk.dll` in `x64\<Configuration>`.

The retired engine validation suites left local-only output, not maintained
source or runtime assets: `Build\RenderingBaseline-*`,
`Build\Terrain*Validation-*`, `Build\source-layout-before-*`,
`Build\source-layout-after-*` and `Build\Tests`. These historical directories
and their root-level build/probe logs have been removed. The deleted suites'
orphaned `*Validation.obj` files were also removed from engine intermediates;
do not remove objects that still correspond to project compilation items.

The obsolete built-in outputs `Deferred_psComposite_ps.cso`,
`Occlusion_vsMain_vs.cso`, `PostProcessValidation_psCustomGain_ps.cso` and
`PostProcessValidation_psScale_ps.cso` are no longer produced or consumed and
have been removed. The 32 current built-in outputs and custom shader directories
are retained. Do not infer that a custom shader or asset is unused merely because
its filename is not a literal in the source.

Engine source files, opt-in public APIs such as `Core\Core.h`, authoring assets,
compatibility readers, preferences, third-party source/licenses and vendor
samples/tests are not generated waste. A currently uncalled public API may be
used by clients or future components. Verify references and intended ownership
before deleting it. Ignored or untracked status alone never authorizes deletion;
resolve and inspect specific targets, avoid junctions, and never broadly clean
the repository or an active IDE cache.

Initialize registered submodules with `git submodule update --init --recursive`.
The current local PhysX, ImGui and spdlog checkouts also contain required files
that are not registered in the parent repository; ImGui and spdlog have local
changes. They are deliberately preserved. A fresh clone is not self-contained
until those dependency sources and changes are supplied separately.

## Source organization

```text
Source\
  Main.cpp, Pch.cpp, Pch.h       Process entry point and precompiled header
  Core\                        Shared engine foundations
    GameObject\                Component/Behaviour base APIs, registry and object ownership
    Scene\, Prefab\            Scene and prefab lifetime/persistence
    Serialization\             Shared serialization infrastructure
    System\                    Window, dialogs, startup and Play/Runtime orchestration
    ...                        Containers, input, logging, math, threading, time
  Components\                  GameObject-attached implementations only
    Animation\                 Animator
    Audio\                     Audio source and listener
    Rendering\                 Camera, light, model renderer and post-process volume
    Physics\
      Character\               Character controller and movement
      Collider\                Collider base and concrete shapes
      Joint\                   Joint
      Rigidbody\               Rigidbody
    Terrain\                   Terrain component
    Transform\                 Transform component
    UI\                        Canvas, rect transform, image, text and button
  Animation\Runtime\           Animation evaluation, not components
  Assets\                      Asset data, import, managers and asset serializers by feature
  Audio\                       Audio runtime services
  Editor\
    Components\Camera\         Editor-only free-camera component
    ImGui\, PostProcess\, Scene\  Editor UI, inspectors and scene documents
  Graphics\                    Rendering services and GPU resources by feature
    DirectX12\
      Command\                 Command lists, queues and fences
      Culling\                 GPU occlusion culling
      Device\                  D3D12 device
      Diagnostics\             GPU profiling and frame capture
      Pipeline\                Graphics/compute pipelines
      Presentation\            Swap chain
      Rendering\               D3D12 renderer and model command recording
      Resource\                Resources and descriptors
  Physics\                     Physics world, system, queries and shared data
    Internal\                  PhysX conversions and shared physics payload helpers
  Terrain\
    Data\                      Heightmap storage and image loading
    Generation\                Build queue, build requests, noise, GPU noise and erosion
    Geometry\                  Mesh building, chunks, LOD and surface sampling
    Material\                  Terrain layer settings and weights
    Serialization\             Terrain material serialization
    Vegetation\                Vegetation recipes and placement
  UI\                          Runtime UI layout and interaction services
```

Keep concrete runtime components in `Source\Components\<Feature>`, with further
subfolders when that feature has distinct families (as in Physics). Keep the
`Component`/`Behaviour` base types and registration in `Core\GameObject`; they
are infrastructure, not attachable feature implementations. Editor-only
components belong in `Editor\Components\<Feature>`.

Services, asset resources and rendering backends do not
belong in `Components`. Preserve the existing feature split under `Assets`,
`Graphics` and `Editor` rather than collecting unrelated managers or utilities
in a catch-all folder. Add a new feature folder when it has a distinct
responsibility; do not create empty folders for hypothetical features.

Use Source-root-relative include paths, for example
`#include "Components\Physics\Collider\BoxColliderComponent.h"`.
No compatibility headers or extra per-feature include search paths are needed.
Changing placement does not change class names, component registration names,
Scene/Prefab payloads, asset paths or generated FlatBuffers headers.

When adding or moving a file, update `GameEngine.vcxproj` and
`GameEngine.vcxproj.filters` together. Every source/header/inl file is explicitly
listed, and each Visual Studio filter matches its physical directory, including
the `Source` prefix. Keep paired headers and implementations together.
Build Debug/Release after a cross-feature reorganization.

# Core scheduling and input boundaries

`JobSystem` queues only ready dependencies for execution. A blocked dependent
does not recursively take ownership of another blocked job, so producers queued
after dependents can unblock a valid dependency chain. External dependency
completion and cancellation are checked periodically by workers. Dependency
cycles are still invalid; a job must not wait for a counter containing itself
or call `waitForAll`, `initialize` or `finalize` on its own system.

Scheduling/counters/waits are thread-safe; the published worker count is atomic.
Counter and captured callback state must outlive all submitted jobs. Scheduling
allocation failures propagate; `parallelFor` joins any jobs already submitted
before unwinding. Job callback failures are logged and still complete accounting.
Continuations retain the existing behavior of running after a failed body.

`TaskGroup` rejects mutation and overlapping execution while jobs are pending.
Group operations require external synchronization. Its `wait` helps execute
ready jobs, including inside a worker; destruction cancels queued work and joins
running work before releasing group storage. The supplied system must outlive
the group, and tasks must not destroy or wait on their own group. Cancellation
between tasks in a parallel chunk is observed without waiting for the whole chunk.

`MainThreadDispatcher` retains task-buffer capacity across frames. Tasks posted
during dispatch run on the next dispatch; recursively calling `dispatchPending`
is rejected and logged. Callback exceptions are logged without dropping later
callbacks or leaving dispatch permanently active.

`String` rejects nonempty null sources and ranges into its unused capacity,
preserving the previous value and logging the error. Appending a string literal
has an unambiguous overload. Capacity/encoding failures are logged in Release
as well as Debug. UTF-16 conversion rejects malformed surrogate sequences and
does not write to a result buffer after an allocation failure.

Smoothed frame time averages only recorded samples during startup; after ten
samples it retains the existing ten-frame rolling average.

# Component removal

In the Inspector, click the **X** on an optional component header, or right-click
the header and choose **Remove Component**. This removes only that instance.
Required components (including Transform) cannot be removed. Removal completes
after component enumeration, invoking the existing disable/destroy lifecycle.
Saving the scene or prefab afterwards persists its updated component list.

`GameObject::removeComponent(Component&)` removes one instance, including a
queued addition. Do not use its reference after a successful removal request.
The existing `removeComponent<T>()` API still removes all instances of that type.

# Runtime and Play

Launch from the project root:

```powershell
.\x64\Debug\GameEngine.exe
.\x64\Debug\GameEngine.exe --runtime --scene ".\Assets\Scenes\My Game.scene"
.\x64\Debug\GameEngine.exe --help
```

## PostEffect Phase 1 execution contract

`DX12Renderer::setPostProcessPasses` copies an ordered list of
`PostProcessPassDescription` snapshots between frames on the render thread.
Each definition owns an immutable compiled pixel-shader snapshot and 16 float
parameters. Invalid configurations log and return false without replacing the
current list. Successful replacement waits for the last GPU use and releases
obsolete temporary targets. Shared shaders are nonmutable for their entire
snapshot lifetime; replacing bytecode requires submitting a new snapshot/list.

The current Deferred color path is:

```text
G-Buffer -> HDR lighting -> transparent geometry
         -> BeforeToneMapping color passes
         -> Bloom -> White Balance / Global + tonal-range Color Grading
         -> fixed/automatic exposure + ACES fitted Tone Mapping (linear HDR -> linear LDR)
         -> AfterToneMapping color passes
         -> Color Grading LUT -> Vignette -> Chromatic Aberration -> FXAA -> Film Grain
         -> sRGB encoding -> debug grid / Runtime UI / editor UI -> presentation
```

Both stages preserve registration order **within** their stage, regardless of
where their definitions appeared in the list. All optional targets are linear
RGBA16F. `resolutionDivisor` (1-16) uses ceil-divided full-target dimensions;
reduced camera bounds cover complete temporary texels, and sampling is clamped
to the source viewport. Full-size passes retain the exact original camera bounds.
A target is never used simultaneously as input and output.
Returned textures may be reused later in the same command list, but cross-frame
reuse and destruction wait for the associated scene-submission fence. Forward,
`Nothing` camera clear mode, and G-Buffer debug views bypass the list.

Without enabled LDR passes or built-in LDR effects, tone mapping and sRGB encoding
are fused into the final output pass. Therefore an empty or entirely disabled list adds no scratch
texture and preserves the Phase 0 images bit-for-bit. Exposure is applied once.
With LDR effects enabled, the tone-mapped intermediate is linear half-float, not
sRGB. Display alpha is 1. `captureFrame` retains scene HDR **before** optional
effects and captures final display after UI; Forward captures have no HDR image.

Pixel shaders include `Assets/Shaders/PostProcess.hlsli`, use `PostProcessVertex`,
and call `loadInputColor(input.position.xy)`. Root bindings are `t0` (input color),
`s0` (linear clamp), `b0` (source/target viewport, exposure, conversion flag), and
`b1` (four `float4` parameter vectors). Built-ins additionally use `t1` (secondary
color input) and `b2` (its viewport); old single-input shader bindings are unchanged.
Full-resolution identical viewports use
integer texture loads to preserve exact copies. `ColorOutput.hlsli` shares the
ACES fitted tone-mapping curve and sRGB implementation with Deferred debug display.

This is deliberately a small color-only graphics-pass foundation, not a full
render graph. Depth/normal inputs, compute passes, history,
and custom shader asset/hot-reload tracking are later phases. Profile authoring
is implemented separately in Phase 2 below.
Common output shaders participate in the existing ShaderManager reload flow;
user-supplied snapshots are replaced explicitly.

## PostEffect Phase 2 Profiles and Camera authoring

Select a **Camera** in the Inspector and expand **Post Effects**. Post Effects
settings are shared through Profiles and local Camera overrides, and execute in
Deferred rendering. Existing renderer exposure and the diagnostic/custom pass
API remain available.

- **Enable Post Effects** opts this camera into Profile rendering (default off).
  **Weight** is a finite contribution in `[0,1]`; Phase 3 blends parameters from
  neutral values, except FXAA, which blends original/filtered linear LDR images.
- **New Profile**, **Open Profile...**, and **Shared Profile** create/load/select
  a shared asset. **Edit Shared Profile** changes its immutable published
  snapshot, affecting all referencing cameras. **Make Unique** creates a new,
  unsaved GUID and assigns it to this camera without modifying the source.
- **Save Profile** atomically writes the existing file. **Save Profile As...**
  writes `.postprofile` and updates that asset's registered path. **Reload
  (discard edits)** explicitly replaces authored values from disk, only on
  successful verification. The Inspector marks unsaved edits.
- A checkbox to the left of each parameter means **override**. Unchecked
  parameters retain their authored values but inherit the preceding layer's
  value. The effect's Enabled switch is also overridable, so a camera can
  explicitly disable an enabled Profile effect.
- **Camera Overrides** are independent, serialized values, not shared asset
  edits. **Reset Overrides** restores neutral defaults and clears override flags.
  Resolution order is engine defaults -> enabled Profile overrides -> enabled
  local parameter overrides. Phase 4 blends Volumes before these Camera layers.

`PostProcessProfileManager` uses generation-checked handles and owns immutable
`shared_ptr<const PostProcessProfile>` snapshots. Updating a Profile does not
mutate old snapshots. The CPU-only `RenderView::postProcess` contains resolved
values, camera enable and weight, with no manager handles or GPU resources.
Subsequent edits cannot change a queued render view. Fixed exposure is expressed
in EV stops, contrast/saturation in percent, and colors in **linear RGB**;
scalar/color ranges are shared between the Inspector and settings validation.

Scene/Prefab Camera payload v7 stores enable, weight, local values/flags, Volume
layer mask, and
Profile GUID plus a normalized absolute path encoded as UTF-8. Existing v1
Cameras and legacy empty payloads remain compatible with neutral PostEffect
defaults. Launching the project does not require any new Profile assets.
GUID mismatches, stale references, unsupported versions, nonfinite/out-of-range
values, and truncated files are diagnosed rather than silently accepted.
The manager rejects a second registered path with the same GUID; copy assets
using **Make Unique**, not a filesystem copy, to create independent identities.
Moving files outside the editor requires reassigning the Profile; this phase
does not introduce an asset database/automatic relocation.

**Save a new Profile before saving its referencing Scene/Prefab.** Scene files
save references, not shared asset edits; save the Profile separately to persist
those edits. In-memory Play cloning works even before the first Profile save:
only immutable assets are shared and local overrides remain independent.
Inspector editing remains disabled during Play as in the existing editor.

## PostEffect Phase 3 built-in effects

Camera Weight 0, disabled cameras and neutral effect settings retain the one-draw,
zero-allocation baseline path. Exposure and color adjustments fuse into display
conversion when no LDR pass is needed. Settings changes do not rebuild PSOs or
wait for GPU fences; the existing fence-aware pool handles temporary resources.
Forward, `Nothing` clear mode and G-Buffer debug views bypass camera effects.
UI and the editor grid remain after post-processing. Removing the active Camera
clears its PostEffect snapshot.

- **Bloom** follows the Standard multi-scale pyramid (not convolution Bloom):
  Karis-weighted first downsampling suppresses isolated HDR fireflies, followed by
  up to five ceil-half levels from 1/2 through 1/32 and normalized nine-tap tent
  reconstruction. A one-luminance-unit soft threshold is scene-referred and
  evaluated before exposure; `-1` includes all pixels. Size Scale adjusts blur
  radius, Scatter blends local and low-frequency energy, and Intensity scales the
  additive HDR result. Constant fields remain normalized across pyramid depth.
- **Fixed exposure** multiplies renderer exposure by `exp2(EV * Weight)` once.
  No automatic exposure/history is introduced. Tone-map inputs are clamped to
  RGBA16F's finite maximum to avoid overflow at extreme EV.
- **ACES Tone Mapping** uses the fitted ACES filmic curve before the final
  linear-to-sRGB encoding. Fixed exposure and temporally adapted automatic
  exposure are applied once before this curve.
- **Color Grading** applies Kelvin White Balance and Tint, a linear RGB filter,
  global contrast around middle gray (0.18), Rec.709 saturation and Gamma, then
  independently weighted Shadows/Midtones/Highlights saturation, contrast,
  Gamma, Gain and linear color-wheel offsets. Negative colors are clamped to zero.
  Camera/Volume weights blend from neutral values; color grading remains in HDR
  before exposure and tone mapping.
- **Vignette** blends its linear RGB color after tone mapping. Intensity blends
  from zero; smoothness controls radial falloff, and roundness blends between
  viewport-normalized and aspect-correct distance.
- **Chromatic Aberration** separates red and blue channels radially from the
  viewport center after Vignette. Start Offset controls the onset radius; sampling
  is clamped to the active camera viewport.
- **FXAA** detects directional edges using perceptual luminance and bounded
  endpoint searches, combines edge and subpixel reconstruction, and blends the
  filtered **linear LDR** result by Weight. Luminance comparisons use 12-bit
  fractional precision to stabilize nearly equal edges across floating-point
  contraction differences. sRGB encoding remains final-only.
- **Film Grain** adds a frame-varying hash-noise grain in linear LDR after FXAA.
  Intensity controls amplitude and Response weights the grain toward shadows.
  The seed advances only on frames where the effect is active and is transported
  exactly through the shader constants.

All built-in shaders participate in ShaderManager dependency/hot-reload tracking.
Rebuild constructs all candidate PSOs before replacing the previous pipelines.
Bloom's multi-input passes never alias their output with either source.
Sampling honors Direct3D's implicit viewport scissor (floor right/bottom), so
fractional viewports never sample the undrawn bottom/right rows of temporary
targets.

## PostEffect Phase 4 Volumes

Add a **PostProcessVolume** component to a GameObject and assign/create a shared
`.postprofile` in its Inspector. It uses the same Profile open/edit/save/reload/
Make Unique controls as Camera. No physics collider or physics update is required.
One Volume component is permitted per object. Camera **Enable Post Effects** is
the overall gate; **Volume Layer Mask** is an independent hexadecimal 32-bit mask
over Volume GameObject Layers. Mask `00000000` excludes Volumes without disabling
Camera Profile/Overrides.

Only the active Scene and persistent Scene participate, matching the engine's
rendered scenes. Disabled components, inactive hierarchies and objects/ancestors
queued for destruction are excluded. Unassigned Profiles contribute nothing.
Stale Profile handles are diagnosed once and skipped, without stale component
registrations after removal or Play scene swaps.

### Shapes and ordering

- **Global** applies its Weight everywhere and ignores its Transform.
- **Box** uses a local center and positive local full size. It follows the
  engine's world TRS, including hierarchy, rotation, nonuniform and mirrored scale.
  Falloff uses the world-space distance to the oriented Box, including corners.
- **Sphere** uses a local center and positive local radius; world radius is
  local radius times the largest absolute world scale axis.
- **Blend Distance** is a shell **outside** the shape measured in world units.
  Inside/on the shape the influence is full Volume Weight. Outside,
  `Weight * smoothstep(0,1,1-distance/BlendDistance)` falls smoothly to zero.
  Distance zero is a deliberately hard boundary. Invalid/degenerate local world
  transforms are diagnosed and skipped until recovered.

Lower **Priority** is applied first. Equal priorities are ordered by persistent
GameObject GUID, then Scene GUID as a cross-scene tie-breaker, not enumeration
order or memory addresses. The complete order is:

```text
neutral defaults -> spatial Volumes (Priority/GUID)
                 -> Camera Profile overrides -> Camera local overrides
                 -> global Camera Weight -> built-in GPU effects
```

Only parameters with their override checkbox enabled participate. Numeric/color
values interpolate by the Volume influence; Enabled switches interpolate a
separate continuous contribution from 0 to 1. An unchecked switch preserves the
previous contribution. A higher-priority explicit disabled switch fades the
preceding effect out rather than abruptly replacing it.

Numeric and Enabled interpolation are **independent**, and Camera Weight is
applied once afterwards. For example, from neutral settings an exposure Profile
with Enabled=true and EV=4 at Volume influence 0.5 resolves EV=2 and an Enabled
contribution of 0.5, producing EV=1 at Camera Weight=1. If Enabled is inherited
at full contribution, only the numeric interpolation applies. Bloom/Vignette
intensity, exposure EV and color adjustments use their own continuous contribution;
FXAA blends its original/filtered result. This avoids Boolean boundary popping
without adding temporal history or camera-dependent GPU configuration.

### Persistence and validation

Volume payload v1 stores all geometry, priority, weight and saved Profile GUID/
UTF-8 path. Camera v3 adds the Volume mask; v0/v1/v2 restore an all-layers mask.
Scene/Prefab save requires the referenced Profile to be saved first, and never
saves shared Profile edits implicitly. Invalid payloads, GUID/path mismatches and
failed saves retain the previous configuration/output. In-memory Play cloning
supports unsaved Profiles, shares immutable assets, and independently copies
Camera and Volume configuration. No new asset is needed by existing scenes.

Each Camera reuses a CPU-only collection workspace; no component/asset pointers
escape into RenderView, and subsequent Profile/Transform edits cannot change a
queued snapshot. Disabled/zero-weight Cameras skip Volume collection. Allocation
failure is explicitly diagnosed and disables that Camera's effects for the frame.
No Volume change rebuilds PSOs or forces GPU waits.

## PostEffect Phase 5 custom HLSL Effect assets

Create a **Custom Effect** from the shared Profile Inspector for either a Camera
or a Volume. Choose an HLSL source file, set an entry point and the linear color
stage, then **Apply & Compile**. A saved `.posteffect` stores the asset GUID,
source path, entry point, stage, resolution divisor and parameter metadata.
Profiles store ordered references to these assets plus their enabled state and
parameter values. Save the Effect asset before saving any Profile that refers to
it. Profile format v2 adds custom Effects; version 1 Profiles still load with an
empty Effect list.

Custom Effects use the existing fullscreen Root Signature and
`Assets/Shaders/PostProcess.hlsli` contract; they do not declare their own root
signature, resources, or samplers. A Pixel Shader accepts `PostProcessVertex`,
returns `float4 : SV_Target0`, samples the input with `loadInputColor`, and
reads up to 16 scalar constants from `parameters[0..3]`. The Inspector exposes
only the asset-declared parameters with their authored bounds. The source must
include `PostProcess.hlsli` (or otherwise match its exact b0/b1/t0/t1/b2/s0
bindings), for example:

```hlsl
#include "PostProcess.hlsli"

float4 psTint(PostProcessVertex input) : SV_Target0
{
    const float4 source = loadInputColor(input.position.xy);
    return float4(source.rgb * parameters[0].x + parameters[0].yzw, source.a);
}
```

`Assets/Shaders/PostProcessExample.hlsl` is the corresponding project sample.
The source color domain follows the asset stage: **Before Tone Mapping** is
linear HDR; **After Tone Mapping** is linear LDR, before final sRGB encoding.
The resolution divisor is bounded to 1-16. Custom shader output is blended over
the preceding image using the combined Volume influence and Camera Weight, which
keeps shape boundaries continuous and preserves source alpha. Profile effect
lists are applied in their authored order; Volume lists follow the existing
Priority/GUID ordering, then the Camera Profile list follows them. Camera local
overrides remain limited to built-in settings.

DXC compiles during Effect creation or **Apply & Compile**. Successful bytecode
is loaded into a new immutable Shader snapshot before the asset is updated;
compile failure preserves the previous working snapshot. Runtime loading uses
the generated `.cso` referenced by the asset and does not invoke DXC. Keep the
generated `Assets/Shaders/Compiled/PostProcessCustom` bytecode alongside the
project assets when packaging or moving a project. Recompiles use unique cache
files so a failed descriptor save cannot replace bytecode referenced by the last
saved asset.

Effects run through the same linear RGBA16F temporary targets and fixed b0/b1/t0/
t1/b2 bindings as built-in passes. A custom shader cannot access scene depth,
motion vectors, arbitrary textures, UAVs, or temporal history; those belong to
later effect phases. Compiled PSOs are cached per immutable Shader snapshot and
rebuilt transactionally when the shared fullscreen vertex shader changes.

## PostEffect Phase 6 depth, temporal effects, color grading and automatic exposure

Depth-of-field uses the active Camera's linearized deferred depth buffer, with
depth-weighted disk sampling to reduce color bleeding across focus boundaries.
Temporal anti-aliasing jitters the deferred projection, reprojects a per-Camera
HDR history with G-Buffer motion vectors, rejects disocclusions by depth, and
clamps history to the current 3x3 color neighborhood. Motion blur gathers along
the same velocity buffer and rejects samples across depth discontinuities.
Both temporal effects require deferred rendering. Camera cuts can invalidate
history with `CameraComponent::resetTemporalHistory`; resizing, switching
cameras, or disabling TAA also resets it. Camera near/far planes and projection
type are captured in the immutable PostEffect snapshot.

Color grading accepts a **linear 2D strip LUT** with dimensions `N*N` by `N`,
where `N` is 16-64. Blue selects adjacent horizontal slices; red and green
address each slice. Runtime supports uncompressed `R8G8B8A8_UNORM` and
`R16G16B16A16_FLOAT` texture data and interpolates between blue slices. The LUT
is applied after tone mapping and custom After Tone Mapping effects, before
vignette and FXAA. The Inspector registers the selected texture asset and stores
its GUID/path reference in the Profile or Camera payload.

Automatic exposure computes the current frame's log-average HDR luminance using
a bounded half-resolution reduction chain, then derives EV from key,
compensation and minimum/maximum limits. Phase 7 adds per-Camera temporal
adaptation using separate brighten/darken rates. The result is applied during
tone mapping, after the HDR reduction and bloom stage.

Fog was removed from runtime settings and rendering. Its FlatBuffers fields are
retained as reserved wire slots so older profiles remain readable without
changing subsequent field IDs; saving a profile writes neutral values to those
legacy slots.

## PostEffect Phase 7 temporal exposure history

Automatic exposure keeps the GPU-resident adapted EV in a ping-pong 1x1 history
texture. Each frame approaches the current log-average target with
`1 - exp(-speed * deltaTime)`, selecting the brighten rate when EV rises and
the darken rate when EV falls. A zero delta freezes a valid history; the first
sample initializes immediately so startup does not fade from an arbitrary
exposure. History is associated with the active Camera GameObject GUID and is
invalidated when the Camera changes, automatic exposure is inactive, or render
targets are released/resized. This avoids readback stalls and stale exposure
leaking across Cameras.

The two rates are authorable Profile/Volume/Camera settings and default to
3.0 inverse seconds for increasing exposure and 1.0 inverse second for
decreasing exposure. Older Camera v4 and Profile v3 payloads missing these
Phase 7 fields load with those defaults. The Phase 7 Camera v5 and Profile v4
payloads also load with the default Bloom Size Scale when that additive Phase 8
setting is absent. Delta time comes from the active Camera's
simulation update, so paused simulation holds the exposure history.

## Runtime and Play behavior

Runtime requires an existing saved scene. Invalid arguments or unreadable scenes
are logged and exit with a nonzero code. Runtime loads compiled shaders and draws
directly to the window without ImGui, editor panels, or the editor grid.
This is not a distribution build/packaging system.

In the editor menu bar, **Play** clones all managed scenes and persistent objects
in memory. Gameplay callbacks, physics and animation run only during Play.
**Pause** freezes gameplay and simulation time while rendering continues.
**Step** advances one Update, one fixed-duration physics/FixedUpdate step, and one
LateUpdate, then remains paused. **Stop** destroys runtime objects and restores
the original editor scenes, persistent objects, camera and selection.
Closing the editor during Play also disposes the isolated session.

While editing, enabled Animators on active objects preview their animation using
unscaled frame time without starting game Play. Animator Inspector Play/Pause,
clip selection and speed control the preview. Both the active scene and persistent
objects are previewed. Game Play/Pause/Step retains its existing simulation timing;
pausing game Play does not advance the editor preview.

Scene editing/saving and transform gizmos are disabled during Play. Click the
**Game** view to give gameplay keyboard/mouse/gamepad input; selecting another
panel or losing window focus clears transient input and stops vibration.
Editor camera navigation runs only while editing. Animation preview does not execute
gameplay Awake/Start/Update/FixedUpdate/LateUpdate callbacks or enable physics actors.

Components copy configuration through `Component::copyConfigurationTo`.
The default uses versioned payload serialization; override it for configuration
that references unsaved in-memory assets. Copies must not share mutable runtime
state. GameObject GUID references are resolved across all cloned scenes after
construction, before lifecycle callbacks start. Immutable model, material,
animation and physics assets remain shared; Play is not an undo transaction for
global asset edits, files written by gameplay, or static variables.

Camera settings and Main Camera preference are persisted in versioned
FlatBuffers payloads. Legacy empty Camera payloads retain default settings.

## FBX animation import

FBX animation stacks are sampled in seconds at 60-120 Hz. Each stack shares one
sampling schedule across its channels. Key times remain strictly increasing
after conversion to float and to ozz's `time * inverseDuration` ratios; the final
key samples the exact FBX endpoint once, even when the preceding grid sample
rounds to the same float timestamp or normalized ratio.
Animation conversion failures report the clip and, for invalid key times, the
joint, track type, key index, previous time, duration and normalized ratios.
Invalid clips are rejected rather than silently omitted from the model.
For translation, rotation and scale curves on every animation layer, cubic
intervals with nonfinite incoming/outgoing derivatives are changed to linear
interpolation in the imported scene only. Each repaired curve produces a warning
identifying the clip, node, component, interval count and first affected time.
Finite cubic intervals and constant/linear interpolation remain unchanged; the
source FBX is never rewritten. Nonfinite key values or sampled transforms still
fail import rather than being replaced by identity transforms.
Clip conversion also rejects nonfinite/invalid rotations and translation/scale
values outside ozz's finite half-precision range. Runtime evaluation never
publishes nonfinite or singular skinning palettes, retaining the last valid pose
and logging an error instead. Reimport the FBX after updating the engine to repair
previously imported animation data; already saved clips are not auto-repaired.

Euler rotation curves are unrolled with the FBX SDK filter, respecting each
node's rotation order, before resampling. This prevents a 359-to-0-degree wrap
from interpolating through an unintended full turn. Missing axes retain their
channel defaults; quaternion-interpolated nodes are not Euler-filtered. The SDK
recalculates user/break tangents as auto tangents when unrolling is needed, and
the importer logs each affected node. This uses the shortest continuous Euler
path: intentional turns greater than 180 degrees between adjacent keys must
have intermediate rotation keys. As with tangent repairs, reimport previously
saved animation data to apply this correction.

## Deleting animations

In the Animator's **Animation Clips** list, select an animation, right-click its
row and choose **Delete Animation**. Choose **Cancel** (or dismiss the menu) to
keep it. The former removal button is no longer used.

Deletion removes both the Model's clip reference and its embedded animation
tracks, and atomically saves the `.model`/`.mdl` asset before clearing playback.
Failed saves keep the animation and playback unchanged and report an error.
For imported FBX or unsaved Models, first use the Model Renderer's **Save Model As...**
to save a `.model`/`.mdl`; source import files are never rewritten.
The Model asset is shared, so its updated clip list also applies to future loads
and other users of that Model. Separate `.anim` files are not deleted.
Save the Scene/Prefab after editing to persist the Animator's cleared selection.

## Animator root motion

Enable **Apply Root Motion** on an Animator to extract model-space XYZ translation
and full rotation. **Root Motion Joint** selects the source joint by exact name;
**First Root** (the default) selects skeleton joint 0. FBX files often contain a
static scene/group root: select the actual animated root/hips joint in that case.
This is not humanoid retargeting. The component toggle controls extraction;
the clip's existing `applyRootMotion` metadata is not an additional enable gate.

The selected joint's rigid transform is measured relative to the evaluated pose
when extraction is enabled, including a restored/seeked time or a blended pose.
Enabling extraction preserves the authored initial orientation and position,
even when the clip differs from the skeleton rest pose. Changing the extraction
joint also preserves the currently displayed pose. Motion is removed from the
entire evaluated pose, so moving the GameObject does not apply the animation
twice. Animated scale stays in the pose. Selected joint transforms must be finite,
nonsingular TRS transforms; sheared joint transforms are rejected and logged
rather than approximated. Rigid inverses preserve the exact affine last column,
without relaxing animated-bounds validation.

Looping extends the rigid trajectory across clip endpoints, including turning,
multiple wraps per update and reverse playback. PingPong retraces the trajectory;
Once clamps at its endpoint. Seek, state changes, rebind, pause and enabling root
motion do not apply the clip's initial offset as a displacement. Crossfades blend
per-state motion deltas with the frame-average transition weight (including a
frame that extends past the end of the fade), not differences between unrelated
absolute clip positions. Rotation uses shortest-path quaternion interpolation.
Accuracy follows ozz's compressed keys and estimated sampling interpolation.

During Play/Step, movement is applied on the game update thread after all Animator
workers join and before physics/late update. Translation follows the current
world rotation and scale, and is converted back through the parent transform;
rotation is composed in the object's world frame and converted to local rotation
using the engine's parent composition convention, without changing object scale.
A singular object or parent transform is logged and rejected. Unavailable pose snapshots do not advance
root-motion animation time or lose its displacement.

An enabled CharacterController receives collision-resolved movement instead of a
Transform teleport. Its GameObject origin represents its **foot position** from
creation onward, and small motion is not discarded by a fixed one-millimeter
cutoff. Explicit Transform/parent position changes synchronize the controller's
foot before the next collision-resolved move. The capsule remains world-up while
the GameObject receives animation rotation. Root motion does not add gravity;
other movement scripts remain additive.
Paused/unavailable physics rejects controller motion without teleport fallback.
Dynamic Rigidbodies are explicitly unsupported; a kinematic Rigidbody follows
the Transform through the existing physics synchronization. An enabled controller
and Rigidbody cannot be used together. Without an enabled controller, movement
uses the Transform; static colliders follow normal physics synchronization.

Editing previews remove root motion from the pose but never move scene objects.

### Root-motion regression tests

The focused CPU-only suite builds the production importer/Animator code and ozz
without requiring a graphics device. Model serialization cases also link its
terrain/texture-reference dependencies and DirectXTex, without initializing the GPU.
Run from a Visual Studio Developer shell:

```powershell
cmake -S Tools\Animation\Tests -B Build\RootMotionTests -G "Visual Studio 18 2026" -A x64
cmake --build Build\RootMotionTests --config Debug --target RootMotionTests --parallel
ctest --test-dir Build\RootMotionTests -C Debug --output-on-failure
```

Use `Release` for both build/test configuration arguments to test optimized code.
The suite also checks animation removal (first/middle/last embedded clips,
external references and all clips) through Model serialization round trips.
Pass `-DFBXSDK_ROOT="C:\path\to\FBX SDK"` during configuration for a non-default SDK.
Tests compare extracted motion against independent matrix trajectories across
Once/Loop/PingPong, reverse/multiple loops, nonidentity start/rest transforms,
scaled hierarchies, reconfiguration, restored controllers, crossfades and
snapshot backpressure. Generated FBX fixtures cover all six Euler orders,
three axes and sparse/dense curves. Fixtures are removed after the test.
For an affected idle FBX, additionally run:

```powershell
.\Build\RootMotionTests\Debug\RootMotionTests.exe --idle-model "C:\path\to\idle.fbx"
```

This checks every skinned vertex, root-motion world-pose reconstruction
(maximum error 0.2 mm), exact-affine palettes, valid animated bounds and
120-Hz vertex continuity (maximum step 5 cm), using the first root and named
`root`/`pelvis` joints when present.
Animator payload v2 and Play clones retain the selected joint and enable flag;
v1 payloads remain readable and default to First Root. Runtime motion deltas are
transient and are never saved or replayed after loading.

# Lights and directional shadows

Create a GameObject and add **Light** in the Inspector. Choose Directional, Point
or Spot, then set color and intensity. Point and Spot use the object's world
position and Range; Directional and Spot emit along local **+Z**, so rotate the
Transform to aim them. Spot angles specify the full inner/outer cone angle.
Enabled lights in the active and persistent scenes are also rendered while
editing or paused. Up to 32 lights are supported; excess lights are logged.

The first active Directional light with **Cast Shadow** enabled owns a 2048x2048
dynamic shadow map. Set Shadow Distance, Shadow Bias and Normal Bias to suit the
scene's scale. Its camera-relative projection is texel-stabilized and sampled
with 3x3 PCF. Model Renderer's **Cast Shadows** controls individual casters;
Opaque and AlphaTest materials cast shadows, Transparent materials do not.
Point/Spot shadows, cascades and baked lighting are not provided.

Scene lights replace the former hard-coded light direction. A scene with no
enabled lights receives only the base ambient contribution (0.15) and emissive
materials. Add a Directional light to existing scenes when direct lighting is
desired. Light settings persist in scenes/prefabs and copy into isolated Play.

# Screen-space Runtime UI

1. Add **Canvas** to a root GameObject and set Reference Size and Sort Order.
2. Create child GameObjects with **Rect Transform**. Anchor Min/Max interpolate
   the parent's rectangle; Offset Min/Max specify its edges in Canvas units.
   The Y axis points down. Equal anchors make a fixed-size rectangle; anchors
   `(0,0)` and `(1,1)` with zero offsets stretch to fill the parent.
3. Add **UI Image**, **UI Text** or **UI Button** after Rect Transform.
   In **UI Image**, click **Browse...** to select a WIC-readable image with the
   Windows file dialog (including PNG/JPEG/BMP/GIF/TIFF and Unicode paths).
   The current image is the initial selection; cancelling leaves it unchanged.
   **Image Path** remains directly editable. **Clear** removes the image path
   without changing Tint, restoring a solid tinted rectangle. Selected paths
   are referenced in place, not imported or copied into the project.
   Text is UTF-8 and uses an installed font
   family with Unicode shaping, wrapping, clipping and Left/Center/Right
   alignment. Buttons provide normal, hover and pressed colors.
   When Image and Button share an object, the button state modulates the image
   tint instead of drawing an opaque image over the interaction colors.

Canvases scale uniformly and center within the Game image/client area, preserving
the reference aspect ratio. Drawing and hit testing are clipped to the Canvas.
Parent RectTransforms provide nested layout; nested Canvases have independent
reference sizes/order. Higher Sort Order and later sibling drawing win.
UI draws over scene geometry before editor ImGui or Runtime presentation.
Images and text layouts are cached; stale entries are released after 120 UI
frames. Layouts, styles and resource paths persist in scenes/prefabs and Play.

The Windows UI backend uses Direct2D/DirectWrite through D3D11On12. Some Windows
SDK versions emit debug-layer diagnostic 916 when probing a native DX12
resource for D3D11 sharing metadata. This is the interop metadata inquiry, not a
render-target state/lifetime violation. Native image-effect pipelines can also
report diagnostic 680 for their intentionally unbound optional depth/stencil
view. The engine does not suppress either diagnostic.

Set a button's **Click Event** name and register its handler from game code:

```cpp
Engine::RuntimeUI::instance().registerClickEvent("StartGame",
    [](Engine::GameObject& runtimeButton)
    {
        runtimeButton.setName("Started");
    });
```

Register each name once; unregister it when its owning game system is disposed.
Handlers receive the runtime object, never the editing original. Closures must
not retain pointers to editing objects or to objects destroyed on Stop.
Handlers run on GameUpdate. Post main-thread-only session/editor operations via
`MainThreadDispatcher::post` rather than executing them inside a click callback.
Callbacks themselves are not serialized or cloned. An unregistered nonempty
event is logged when clicked. A click requires press and release inside the
same enabled, interactable button. Disabled topmost buttons block lower buttons.
Losing Game view/window focus, Pause or Stop cancels pointer capture; editing
and paused frames never dispatch clicks. Keyboard/gamepad navigation,
world-space UI and general-purpose layout widgets are not provided.

# WAV audio

Add **Audio Source** to an emitter GameObject, set **WAV Path**, Volume, Pitch,
Loop and Play On Awake. Click **Browse...** to select a WAV file with the Windows
file dialog, including Japanese/Unicode paths. The current file (or the Assets
folder when empty) is the initial location. Selection validates and caches WAV
data without opening an audio device or starting playback. Cancelling or selecting
an invalid WAV leaves the existing clip and playback unchanged; failures are logged.
**Clear** removes the clip and stops its voice without resetting playback settings.
Direct **WAV Path** editing remains available. Files are referenced in place,
not copied into the project. Paths are UTF-8 in saved configuration; relative paths
are resolved from the application's working directory. WAV files support mono
or stereo PCM (8/16/24/32-bit) and 32-bit IEEE float, 8-192 kHz, up to 512 MiB.
RIFF chunks and WAVEFORMATEXTENSIBLE metadata are validated before playback.
Compressed audio formats and streaming are not provided.

Enable **3D Spatial** for a mono clip and add exactly one enabled
**Audio Listener** across the active/persistent scenes (usually on the camera).
Listener local +Z/+Y define forward/up. X3DAudio applies speaker panning and
doppler; Min/Max Distance control attenuation and silence beyond the maximum.
Without a listener spatial voices are explicitly logged and muted; with multiple
listeners the first is used and the configuration error is logged.

The default XAudio2 output device opens on first playback, not for `--help`,
scene loading or a silent editor. Output/voice failures are logged, and device
callbacks stop affected gameplay mixing rather than leaving silent queued
voices indefinitely. A subsequent explicit play can reopen output.
`AudioSourceComponent::play`, `pause`, `stop` and `isPlaying` are available to
game code. `isPlaying` means buffers remain queued, including paused playback;
`AudioSystem::playbackSeconds` returns consumed sample time, including loops.
Changing a clip, loop or spatial mode stops its voice; call play to restart.

Editing never starts voices. Play On Awake starts when the runtime component
is enabled. Pause freezes all mixing and preserves sample cursors; Step may
initialize a source but never advances audio while paused. Resume continues
playback. Stop, component disable/removal and shutdown destroy voices. Play
copies share immutable cached WAV data but never share mutable voices.

# GPU occlusion culling

**GPU Occlusion Culling** in Renderer Statistics enables same-frame
Hi-Z visibility and count-buffer `ExecuteIndirect` for Opaque/AlphaTest color
draws. The existing depth pre-pass and shadow pass still draw all their submitted
geometry. Transparent draws remain direct and retain their ordering.

The standard-Z depth target is sampled as R32_FLOAT. A power-of-two-padded
R32_FLOAT mip chain stores the **maximum** depth, including uncovered pixels
at depth 1. Each instance with safe bounds projects its world AABB on the GPU; the
screen rectangle is expanded by two pixels and nearest depth is biased toward
the camera. All overlapping Hi-Z cells at the selected mip must be in front
of that rectangle before the instance is removed. Unvalidated animation bounds, degenerate bounds,
nonfinite projection and near-plane intersections stay visible. Culling is
disabled for `CameraClearMode::Nothing`, which retains old depth.

Instances are tested individually, even inside a shared draw batch. Visible
128-byte instance constants are compacted into that batch's reserved range.
Each batch receives a 24-byte packet: `D3D12_DRAW_INDEXED_ARGUMENTS` followed
by a 32-bit command count. An empty batch has count 0: no color Draw executes,
not merely a predicated Draw or a Draw with zero instances. CPU state setup
and one `ExecuteIndirect` submission per candidate batch still occur.
This is not CPU-side scene filtering or a single global multi-material indirect
submission; CPU object preparation, depth and shadow costs remain.

Resources and descriptors are frame-local and reused only after their fence
completes. Hi-Z mip transitions keep source mips readable while the destination
is writable; generated instances transition to shader-read and packets to
indirect-argument state before color command lists execute. CPU readback is
asynchronous statistics only and never determines current visibility.
Compaction preserves the sorted queue's instance order using a group-wide
prefix scan across consecutive 64-instance tiles, without depending on wave
size or atomic scheduling. This preserves color-pass ordering for overlapping
coplanar instances, including the production `EQUAL` depth comparison.

Camera culling uses `CameraFrustum` world-space clip planes for perspective
and orthographic projections, including jitter and transformed cameras.
`CameraComponent::getFrustum`, `RenderView::frustum`, `DX12Renderer::setFrustum`
and `RenderQueue::submit` use this type instead of `DirectX::BoundingFrustum`;
use `CameraUtils::buildWorldFrustum` to construct it. The old math `Frustum`
alias remains available for perspective-only DirectX collision queries.
Invalid camera matrices log errors and disable clip-plane culling.

Depth clears, model passes, Hi-Z and overlays share
`CameraUtils::toPixelViewport`. Its conversion keeps normalized viewport edges
inside the render target despite float rounding, and model scissor rectangles
use the same floor/ceil convention as clears. Culling input validation permits
only a small float-rounding overshoot at the right/bottom edge; genuinely
out-of-range or nonfinite inputs still fail explicitly.

Statistics distinguish direct submissions, indirect submissions, and completed
GPU color draws. **Frustum-visible Objects** is not an occlusion result.
**Occluded Color Instances** counts submesh instances, not unique GameObjects.
Completed-slot statistics can lag the current frame and return zero when that
slot had culling disabled. Compare GPU frame time with culling enabled/disabled;
Hi-Z construction and compaction cost can outweigh savings in small scenes.

## Current-pose animation bounds

Animated models use the same immutable `SkinningPaletteSnapshot` for bounds
and GPU drawing; no previous-frame pose, clip-wide estimate or fixed padding
around bind-pose bounds determines visibility. GPU mesh creation caches the
local minimum/maximum positions influenced by each bone, plus rigid vertices.
For nonnegative weights, the shader's normalized linear blend lies inside the
union of these transformed influence ranges. Each frame transforms range corners
with the bone matrix followed by the exact mesh-node/world matrix. This handles
model-space skinned meshes, mesh-local vertices, blended poses, nonuniform or
negative scale, and unweighted vertices without CPU-skinning the entire mesh.

Bounds arithmetic uses double precision, a magnitude-dependent float-error
margin (including cancellation and normalization), and outward-rounded extents.
Vertices near the shader's weight threshold contribute to both rigid and
skinned ranges. Bone indices follow the shader's clamp to 255; unused palette
slots retain the same identity-matrix behavior as GPU drawing.
Animator bind-pose correction inverses preserve the exact affine last column
`(0, 0, 0, 1)`: generic matrix inversion can round its homogeneous component away
from one. Both clip and controller initialization reject nonfinite, singular
or non-affine bind transforms explicitly. Bounds validation remains strict;
invalid palette diagnostics include the last column rather than hiding failures
with a tolerance or modifying the render-time palette.

The renderer merges current mesh bounds for whole-model frustum rejection,
then uses each mesh's pose bounds for frustum checks, transparent sorting and
Opaque/AlphaTest Hi-Z. Offscreen shadow casters still submit their shadows.
Skinning draw batching remains unchanged; this does not stop animation updates.
No scene/prefab or asset schema migration is required.

Invalid weights, unavailable influence caches, nonfinite/projective matrices,
invalid palette counts or float-overflow risk are logged and disable animation
bounds culling. `RenderItem::animationBoundsValid` defaults to false, so callers
that only provide bind-pose bounds retain the previous safe animation bypass.
Near-plane intersections also retain Hi-Z's visibility bypass.

# Terrain Phase 1/2/3/4/5/6/7: Generation, materials, PhysX, chunks, LOD and background builds

In the Hierarchy context menu choose **Create Terrain**, or add the **Terrain**
component to an existing GameObject. A new component contains a flat 65x65 grid.
It renders in both the editor and Play/Runtime through the existing model
pipeline, using the default material until terrain layers are explicitly enabled.
It does not need a Model Renderer.

The Terrain Inspector provides:

- **Size X / Size Z**: local-space width and depth, centered around the origin.
- **Height Scale / Height Offset**: `localY = offset + sample * scale`.
- **UV Repeat X / Z** and **Cast Shadows**.
- **Chunk Cells (16..1024)** and **Apply Chunk Settings**: repartition the
  applied surface without changing height data or mesh dimensions.
- **Enable Terrain LOD**, **Maximum LOD (1..4)**, **LOD Distance** and
  **Apply LOD Settings**: explicitly enable distance-based chunk simplification.
- **Apply / Rebuild**: apply the edited dimensions without changing samples.
- **Import Heightmap...**: read PNG, TIFF or BMP height data and rebuild using
  the edited dimensions. 8/16-bit grayscale PNG precision is preserved.
- **Create Flat**: replace heights with a constant normalized value and the
  specified X/Z sample counts. This replaces any imported height data.
- **Generate Noise**: replace heights with seeded Perlin noise or FBM and apply
  the edited mesh dimensions. See the procedural generation controls below.
- **Generate Noise (GPU)**: explicitly use Compute Shader Perlin/FBM, then
  asynchronously read back authoritative heights. CPU remains the default.
- **Background** stage/timings and **Cancel Terrain Generation**: generation
  and all Apply buttons queue work; the current terrain stays active
  until the completed result is applied at a safe frame boundary.

Height samples are linear numeric data in `[0, 1]`, not sRGB colors. Import
ignores gamma metadata, takes the red channel (ignoring alpha), and maps the
image's first row to local `-Z`, increasing towards `+Z`. Each pixel becomes
one vertex; images are not silently resized. Non-square grids are supported.
Dimensions must be 2..1025 samples on each axis. Physical sizes are
0.01..100000, height scale is 0..100000, height offset is -100000..100000,
and UV repeats are 0.001..10000. Invalid input is logged and preserves the
previous terrain; Inspector operations also display a status message.

Scene/Prefab payloads embed the normalized samples, dimensions, UV repeats and
shadow flag. Save normally; there is no separate model asset to save and the
source image is not needed after import. Reload, duplication and Play clones
create independent generated geometry. Unapplied Inspector fields are not saved.
Transforms, parent transforms, layers, component enable/disable, frustum culling
and existing render-path occlusion behavior use the standard engine mechanisms.

The CPU API is in `Source\Terrain\Data\Heightmap.h` and
`Source\Terrain\Geometry\TerrainMeshBuilder.h`; apply data with
`TerrainComponent::setTerrain`. Generation emits 32-bit indices, +Y-facing
triangles, area-weighted smooth normals, orthonormal tangent frames, UVs and
mesh/model bounds. Rebuilds retire the previous model handle; queued immutable
CPU snapshots and GPU fences keep already submitted geometry safe.

Streaming remains out of scope. Phase 8 adds explicit CPU hydraulic erosion,
Phase 9 adds GPU noise, and Phase 10 adds static vegetation placement below.
Phase 7 makes Inspector generation/import/Apply work
asynchronous. Existing synchronous C++ APIs and Scene/Prefab loading remain
synchronous for compatibility; neither API style should regenerate every frame.

## Procedural generation (Phase 2)

In the Terrain Inspector, edit **Procedural Heightmap**, then click
**Generate Noise**. Generation is explicit, not performed continuously while
dragging controls. It replaces the existing imported/flat/generated heights;
failure logs a diagnostic, displays status, and preserves the previous terrain
and last successfully applied recipe.

| Control | Meaning / supported values |
| --- | --- |
| Noise Mode | Perlin (one octave) or FBM (weighted Perlin octaves) |
| Seed | Any unsigned 32-bit value, including 0 and 4294967295 |
| Noise Samples X / Z | 2..1025 samples on each axis, including rectangular grids |
| Frequency | 0.001..1024 base noise cells across each complete grid axis |
| Noise Offset X / Z | -100000..100000 base noise-space translation |
| Octaves | 1..12 (FBM only) |
| Persistence | 0..1 amplitude multiplier for successive octaves (FBM only) |
| Lacunarity | 1..4 coordinate/frequency multiplier for successive octaves (FBM only) |

Defaults are FBM, seed 0, 65x65 samples, frequency 4, zero offsets, 6 octaves,
persistence 0.5 and lacunarity 2. Perlin disables the FBM-only controls but retains
their settings; all stored fields must remain valid in both modes.
The existing **Height Scale / Height Offset** map generated normalized samples to
local Y. Mesh width/depth affect physical size, not the sampled noise domain.
Changing resolution preserves the field at shared coordinates.

The base sample coordinate is
`(x / (width - 1) * frequency + offsetX, z / (height - 1) * frequency + offsetZ)`.
Each octave multiplies both coordinates by lacunarity, retaining the same seed,
and multiplies its amplitude by persistence. Offset is applied before octave
scaling. Negative coordinates use mathematical floor, not truncation.
Perlin uses hashed lattice coordinates, eight fixed unit gradients and quintic
interpolation, with no mutable permutation table or global random generator.
FBM divides its weighted sum by the sum of octave amplitudes. Signed noise is
converted with `0.5 + 0.5 / sqrt(2) * signedNoise`, using a conservative fixed
gradient bound. Output is finite and in `[0, 1]`, but is not stretched to fill
that range on every map. This preserves relative height across adjacent domains
instead of changing terrain contrast according to each grid's extrema.
One FBM octave, or zero persistence, produces exactly the Perlin result.

Use enough samples for the highest octave frequency: undersampling can alias
fine detail, and samples landing entirely on lattice intersections produce a
flat 0.5 map. The generator does not silently change resolution, frequency or
octave count. Maximum settings are bounds for safe arithmetic and allocation,
not a recommendation to generate million-vertex terrain with twelve octaves
on every frame. Background generation is provided by Phase 7 below.

`generateTerrainNoise` in `Source\Terrain\Generation\TerrainNoise.h` generates CPU height
data without registering a model. `TerrainComponent::generateNoise` generates
and applies both the heightmap and mesh settings transactionally.
The same recipe reproduces the same samples in the same build, independently
of generation order and other engine random operations.

Terrain payload version 2 adds the **last successfully applied noise recipe**.
Scene/Prefab/Play clones retain that recipe as well as the actual height samples.
Saved samples remain authoritative: loading never regenerates them implicitly.
Importing an image, creating a flat grid or rebuilding dimensions keeps the last
noise recipe available for future generation, even though the current heights
may no longer match it. Unapplied Inspector recipe edits are not saved.
Version 1 terrain payloads still load their original samples unchanged and get
the default recipe; their next save writes version 4. Invalid/missing version 2+
recipes are rejected without replacing the existing terrain.

## Terrain material layers (Phase 3)

Open **Terrain Material Layers**, check **Enable Layered Material**, edit the
layer controls, then click **Apply Terrain Material**. Edits are drafts until
applied; Scene/Prefab saves store applied values only. **Reset Layer Draft**
restores disabled soil/grass/rock/snow defaults without immediately changing the
surface. Existing terrain remains white/default unless layers are enabled.

There are 1..4 ordered, opaque PBR layers. Layer 0 fills uncovered regions;
each subsequent layer overlays all previous layers according to its coverage:

`coverage = strength * heightBand(sample) * slopeBand(localSlopeDegrees)`

Within an inclusive minimum/maximum interval, a band is 1. Outside it, it falls
smoothly to 0 over the configured blend width (smoothstep); a zero width gives
a hard inclusive boundary. The overlay multiplies previous weights by
`1 - coverage` and receives `coverage` itself. Weights remain nonnegative and
sum to 1, including gaps and overlapping ranges. A higher layer wins when its
coverage is 1; reduce its strength for a partial overlay. Base-layer ranges and
strength are intentionally unused because it always fills the remainder.

- **Height** is the original normalized heightmap sample, not world Y or a
  per-map minimum/maximum remapping. Height offset/scale do not change this
  normalized band selection.
- **Slope** is the angle between the generated smooth vertex normal and local
  +Y, in degrees. Changing physical dimensions/height scale recalculates it.
  Object/parent transforms move the finished surface without changing its local
  automatic layer selection.
- Coverage is calculated at heightmap vertices and interpolated across
  triangles; textures and PBR values blend per pixel. Use sufficient heightmap
  resolution for narrow transitions. No splatmap or brush painting is included.

Each layer has a linear RGB tint, metallic and roughness factors `[0,1]`,
normal scale `[0,2]`, UV repeat `[0.001,10000]`, and optional **BaseColor**,
**Normal** and **Metallic-Roughness** textures. Layer UV repeat multiplies the
terrain mesh's UV Repeat X/Z; sampling wraps. BaseColor is sRGB decoded, while
Normal and Metallic-Roughness are linear data. Metallic uses the blue channel,
roughness the green channel, multiplied by their layer factors. Missing textures
use white BaseColor, flat tangent-space normal and neutral white MR, so scalar
factors work without a texture. Failed texture loads log a warning and use those
same defaults, following the standard material system. BaseColor alpha is not
used: terrain layers are opaque. Normal vectors blend in a common tangent frame,
are renormalized, and follow the terrain's object transform.

Height intervals must be ordered in `[0,1]`, slope intervals in `[0,90]`;
height blend width is `[0,1]`, slope blend width `[0,90]`, and strength `[0,1]`.
All stored slots, including inactive layers, must be valid and finite. Texture
references use GUIDs plus path hints; selecting a texture stages its reference,
and **Clear** stages removal. Textures remain external assets and must be kept
available. Component hints follow existing component persistence (resolved paths);
standalone/embedded material asset hints are relative to their owning file.

`TerrainComponent::setMaterialSettings` applies settings transactionally and
`getMaterialSettings` returns the applied settings. Rebuilding/importing/noise
generation preserves the applied surface and recomputes its blend weights.
Materials are immutable shared CPU assets in MaterialManager, not a new
MaterialComponent. Each terrain owns its generated model/material handles;
Scene/Prefab/Play clones have independent handles but share texture references.
Queued immutable geometry/material snapshots and GPU fences remain valid after
regeneration or component destruction, including before the first GPU upload.
Both Forward and Deferred use the same surface-sampling implementation.

Terrain payload version 3 adds complete layer settings. Versions 1 and 2
without layer data load with layers disabled, preserving their previous look.
Invalid version 3 material data preserves the existing terrain on failure.
Material/model asset version 3 also supports the optional terrain extension;
versions 1 and 2 remain readable. Terrain component saves now use version 4
as described below; the material/model asset version is unchanged.
The standard material property layout and
override block remain unchanged.

## Surface queries and PhysX collision (Phase 4)

Add **TerrainCollider** through **Add Component** on the same GameObject as
**Terrain**. It is optional: surface queries and rendering do not require it.
The collider cooks the generated triangles in memory through the existing
PhysX mesh backend. No external `.physics` file is needed. Scene/Prefab/Play
copies retain common collider settings and rebuild collision from the embedded
terrain samples when activated.

- Terrain Collider is **static**, non-trigger, and cannot share its GameObject
  with a **Rigidbody**. It supports positive, nonzero world scales and rotation,
  including parent transforms supported by the engine's normal TRS hierarchy.
  Reflected, zero-scale and sheared collision transforms are unsupported and
  log errors. Apply supported transforms before enabling collision.
- Common collider friction/restitution, layer, collision mask, query filtering
  and Rigidbody collision events use the existing physics paths. **Center** is
  an optional local collider-only offset; keep it zero to align physics with the
  rendered surface and height-query APIs.
- Import, noise generation and Apply/Rebuild update active collision together
  with geometry. A failed cook/backend update preserves the previous terrain.
  Synchronous C++ rebuilds must run between physics simulation steps. Phase 7
  Inspector requests cook in the background and apply at a frame barrier; do not
  rebuild every frame. Object transform changes synchronize at the next physics
  simulation step, just like other static colliders.
- Collider enablement is independent of Terrain visibility. Disabling/deleting
  the collider or deactivating the object removes its physics shape. Removing
  Terrain also detaches collision; adding Terrain again reconnects during the
  next fixed update.
- Existing **CharacterController** movement collides with terrain triangles,
  uses its slope/step settings, and reports grounding. The controller itself
  does not add gravity or height snapping; gameplay supplies downward movement
  through `move`, or can use the opt-in movement component below. Regeneration
  invalidates cached controller triangles; it does not teleport bodies when
  ground rises through them.
- For a basic playable character, add **CharacterController** and **Character
  Controller Movement** to the Player GameObject, then make the active **Main
  Camera** a child of the Player. The movement component reads WASD during
  fixed updates, moves along world X/Z at 5 units/second, smoothly turns toward
  movement, and applies gravity. Hold the right mouse button and move the mouse
  to orbit the third-person camera; it follows behind the Player at a fixed
  distance and sphere-casts against colliders to avoid clipping through them.
  Focus the **Game** view while playing. Do not add a **Rigidbody** to the
  Player: CharacterController already owns its PhysX actor, and the engine
  rejects using both on the same object.

The public CPU API is independent of PhysX:

```cpp
const auto local = terrain->sampleLocalSurface(localX, localZ);
const auto world = terrain->sampleWorldSurface(worldPosition);
if (world)
{
    const float groundY = world->position.y;
    const Engine::Vector3 groundNormal = world->normal;
    // Use the surface in gameplay; do not replace CharacterController collision.
}
```

`TerrainSurface` contains position and **geometric face normal**, not the
smoothed rendering normal. Local XZ includes the grid boundary. Heights match
the actual `a,c,b; b,c,d` mesh triangles, not bilinear interpolation.
World queries ignore input Y and find the **highest** vertical intersection
at world XZ, with an upward-facing normal. They support invertible affine
transforms, use allocation-free grid traversal, and return `std::nullopt`
outside the projected terrain. At a shared edge either adjacent face normal
can apply. Invalid/nonfinite query inputs are logged. Queries use applied
terrain data, independent of collider Center, enabled state and filters.

## Resident chunk management (Phase 5)

A Terrain remains **one GameObject, one authoritative heightmap and one
generated model handle**, but the model now contains one mesh per chunk.
TerrainCollider creates one corresponding static PhysX triangle shape per
chunk on the same actor. Queries and collision events still identify the
original TerrainCollider/GameObject, not hidden child objects.

Set **Chunk Cells (16..1024)** and click **Apply Chunk Settings**. The default
for newly created terrains is **64 cells per edge** (up to 65x65 vertices per
chunk). A 257x257 heightmap therefore creates 4x4 chunks. Cell count is not
sample count or world size. Arbitrary integer cell counts in the supported
range are accepted; powers of two are not required. Partial chunks at +X/+Z
retain all remaining cells. A chunk edge larger than the grid creates one
mesh. At the maximum supported 1025x1025 heightmap, 16-cell chunks create at
most 4096 meshes/shapes. The image import limit is unchanged.

Chunk size edits are drafts until **Apply Chunk Settings**. That button uses
the applied terrain dimensions, not unapplied Size/Height/UV edits.
**Apply / Rebuild**, import, flat creation, noise generation and material
changes retain the applied chunk settings. All chunks remain resident:
partitioning adds neither distance loading/unloading nor partial height edits.
Phase 6 adds opt-in LOD and early chunk culling; Phase 7 moves Inspector
rebuilds to the background. Terrain-wide bounds are retained.

Normals, tangent frames and automatic layer weights are calculated from the
**whole** grid before partitioning. Border vertices copy identical position,
normal, tangent, bitangent, UV and color data. UVs remain whole-terrain UVs;
textures do not restart at each chunk. Each cell belongs to exactly one chunk;
shared edges duplicate vertices but not triangles. LOD zero and collision keep
the original diagonals. Height queries continue to use authoritative whole-terrain
samples, so results are independent of partition size.

Whole-grid normal/material calculation still uses a temporary complete mesh;
partitioning does not lift the heightmap limit or provide out-of-core storage.
Repartitioning stages the complete model, cooked chunk assets and backend
shapes before replacing live resources. Allocation/cooking/backend failure
preserves the applied terrain and collision. Queued model/material snapshots
retain all old chunks until submission fences retire. Geometry changes
invalidate Character Controller caches. Contact/trigger accounting tracks
individual shape pairs while dispatching one GameObject-level event stream,
including when a body touches several adjacent chunks. Collider settings and
enabled state apply to all its shapes; debug collider/AABB drawing and physics
shape statistics include every chunk.

Public APIs:

```cpp
if (terrain->setChunkSettings({ 32 }))
{
    for (const auto& chunk : terrain->getChunks())
    {
        // Row-major column/row, sampleX/sampleZ, cellsX/cellsZ, meshIndex, bounds.
        // Positions and bounds are in whole-terrain local space.
    }
}
```

`getChunkSettings()` returns applied settings. `getChunks()` is empty before
first generation; its span/references expire on successful regeneration.
`getModel()` still returns the complete generated model, with row-major chunk
meshes. `TerrainCollider::getChunkAssets()` exposes immutable cooked assets in
the same order; legacy `getPhysicsAsset()` returns the **first chunk only**.
Never unload generated model handles externally.

Terrain payload **version 4** saves chunk cell count alongside embedded heights,
dimensions, noise recipe and material settings. Scene/Prefab/Play restore the
same partition with independent geometry and regenerate cooked collision;
no separate chunk files or external physics files are required. Versions 1..3
load as one chunk (`1024` cells per edge), preserving their previous partition
and appearance. Their next save writes the current version 5. Invalid version 4 chunk
settings are rejected without replacing the existing terrain.

## Distance LOD and chunk culling (Phase 6)

LOD is **off by default**, including when loading terrain payloads version 1..4.
Check **Enable Terrain LOD**, edit the following draft controls, then click
**Apply LOD Settings**. Applying uses the existing applied heightmap, dimensions,
material and chunk layout; it does not apply unrelated Inspector drafts.
All rebuild/import/noise/material/chunk operations retain applied LOD settings.

| Control | Meaning |
| --- | --- |
| Maximum LOD | 1..4; level 0 is full resolution, level N has interior stride `2^N` |
| LOD Distance | First transition in world units, finite 0.1..1000000; default 50 |

Each active render view selects from its own camera position to the **closest
point of the transformed chunk AABB**, not its center. Parent transforms,
rotation and nonuniform scale are included. Distances below `D` use level 0,
then `[D,2D)` uses level 1, `[2D,4D)` uses level 2, etc., clamped to available
levels and Maximum LOD. Exact thresholds select the coarser level.
Thin/partial chunks may have fewer useful levels; a candidate is retained only
when it strictly reduces triangle count. Chunks at most two cells wide/deep
keep level 0.

All levels reuse the same full-resolution vertex buffer and attributes.
The interior is sampled more sparsely and joined to a full-resolution outer
border with triangulated transition strips. **Every original unit border edge
survives**, so independently selected neighboring levels meet exactly without
skirts, neighbor-dependent stitching or per-frame mesh rebuilding. Height,
normal, tangent, UV and layer weights at the shared boundary are unchanged.
Simplification changes the interior surface; abrupt distance transitions can
pop. This phase does not add geomorphing, hysteresis or screen-error selection.

Chunk bounds are tested against the camera frustum **before** material/draw
preparation, even when LOD is disabled. Outside chunks submit no camera color
or depth items. Selected color, depth-prepass and existing Hi-Z draws use
identical index ranges. Shadows use a **separate light frustum and level 0**:
an off-camera chunk can still cast a visible shadow, and changing camera LOD
does not simplify its shadow geometry. Cast Shadows and camera layers retain
their normal engine behavior. Invalid frusta conservatively bypass culling.

Authoritative height queries and PhysX always use the full-resolution triangles.
Adding/re-enabling a TerrainCollider after generating LOD cannot cook the
appended alternative indices as overlapping collision geometry.
LOD ranges/settings are immutable snapshots paired with queued model snapshots;
regeneration, camera changes and owner destruction cannot retarget queued draws.

The renderer's Model statistics show visible/culled/shadow chunk counts and
selected versus full-resolution camera triangle counts. These are CPU
submissions **before GPU occlusion**, not GPU-visible triangle counts.
`RendererStatistics::terrainLodChunks` exposes the selected-level distribution.
LOD reduces triangles, not vertex-buffer residency or chunk draw count.
Alternative index ranges add memory; early culling reduces draw submissions,
while full-resolution shadows intentionally retain their geometry cost.

```cpp
if (!terrain->setLodSettings({ true, 4, 50.0f }))
{
    // Error is logged; the previously applied terrain and collision remain valid.
}
```

`getLodSettings()` returns applied settings. `getLodSnapshot()` is null before
generation and otherwise exposes immutable chunk index ranges matching
`getModel()`. `setLodSettings` is a synchronous authoring rebuild, including
full-resolution cooking if a TerrainCollider is attached. Inspector Apply
queues the same CPU preparation in the background; neither is intended
as a per-frame camera update. Distance selection and culling do not recook,
allocate meshes, or modify component state each frame.

Payload **version 5** saves applied LOD settings with embedded samples, chunk
dimensions and materials. Scene/Prefab/Play restore independent generated
resources; missing/invalid version 5 LOD settings are rejected transactionally.
Versions 1..4 load with LOD disabled and retain their existing chunk migration.

## Background terrain generation (Phase 7)

The Terrain Inspector's **Generate Noise**, **Create Flat**, **Import Heightmap**,
**Apply / Rebuild**, **Apply Terrain Material**, **Apply Chunk Settings** and
**Apply LOD Settings** now submit background requests. No additional checkbox
is needed. The modal file picker still runs on the main thread; image metadata
read/decode and all expensive CPU preparation run after selection, on a dedicated
terrain worker. Material texture-reference registration remains on the owner
thread; texture decoding/upload still uses the existing renderer texture path.

The worker pipeline is:

1. Copy captured heights, generate CPU Perlin/FBM (or explicit GPU noise +
   readback in Phase 9), create flat samples, or decode WIC.
2. Apply hydraulic erosion only when explicitly requested (Phase 8), then
   generate whole-grid vertices, smooth normals, tangents and UVs.
3. Calculate whole-grid material layer weights, then partition chunks.
4. Cook full-resolution PhysX chunk data if a TerrainCollider was attached.
5. Append LOD indices, scatter applied vegetation recipes (Phase 10) and publish
   the prepared CPU result.

Requests own value snapshots. Workers do **not** access components, scenes,
resource registries, renderer-owned resources, PhysX scenes, actors or shapes.
Phase 9 GPU noise uses only its independent queue/buffers/fence. Cooking
uses local parameters/output buffers and produces immutable bytes only.
One lazy-started background thread is shared by all terrains. It is separate
from the cooperative frame JobSystem, so a frame/job waiter cannot accidentally
execute a multi-second terrain build. Work is serial within this queue; it
does not create one thread per terrain/chunk/request.

The **Background** label shows Queued, Height samples, GPU noise, GPU readback,
Hydraulic erosion, Mesh, Material weights,
Chunks, PhysX cooking, LOD, Vegetation, Ready to apply, Applied, Cancelled or Failed.
These are stages, not equal-duration progress percentages. Whole-build and final
apply timings appear after completion. **Ready to apply** does not mean that
the new terrain is live. While work is pending, rendering, collision, surface
queries, model handles, applied settings and saved samples continue using the
previous terrain. An ungenerated component with an accepted request does not
run the automatic synchronous fallback or draw partial data.

After game/render/physics work has joined, `Window::run` calls
`TerrainGenerationQueue::applyCompleted()`. It never waits for the worker and
attempts at most **one prepared live terrain result per frame**. Resource
registration and PhysX shape replacement are staged before committing the new
heightmap, geometry, material, chunk/LOD settings and noise recipe together.
Invalid input, decoding/cooking failures or backend failure preserve the
previous applied terrain and emit diagnostics. If TerrainCollider presence
changed while a build ran, the result is rejected explicitly; request again
with the current configuration. There is no surprise main-thread recooking.

**Latest accepted request wins for each component.** A new request cancels
older work without waiting; cancelled queued inputs are pruned and an active
build drops its result. Erosion checks every droplet/step, noise checks each sample row, cooking
checks each chunk, and the remaining work checks between CPU stages. WIC decode
and an individual CPU stage/cooked chunk are not forcibly interrupted. Submitted
GPU dispatches retire on the worker's compute fence before buffers can be reused;
cancellation discards their output rather than forcibly interrupting GPU work.
**Cancel Terrain Generation** also discards already-Ready output. Invalid
requests do not supersede valid work. A successful synchronous edit/load/clone
into the same component invalidates its pending asynchronous result.
Destruction/removal cancels before callbacks can access the owner; lifecycle
deactivation also cancels active gameplay builds. A replacement component at
the same GameObject GUID cannot receive the old component's result.

Each Apply button captures the applied heightmap and **other applied settings**
plus that button's draft. Mesh generation/import/flat/noise captures the mesh
dimension draft. Requests do not implicitly merge a previous pending Apply;
submit the desired complete configuration through `requestRebuild` when
changing multiple settings together. Editing drafts after submission does not
alter the captured build, and completing a build does not overwrite newer
Inspector drafts. Scene/Prefab/Play snapshots contain only applied data:
pending/cancelled requests, progress and unapplied drafts are never saved,
cloned or replayed. Phase 7 retained payload **5**; Phase 8 uses **6** to store
the last applied erosion recipe alongside the authoritative samples.

Existing immediate-return APIs (`setTerrain`, `generateNoise`, `setMaterialSettings`,
`setChunkSettings`, `setLodSettings`) retain synchronous success/failure semantics.
The new owner-thread APIs return **request acceptance**, not completion:

```cpp
if (terrain->requestNoise(recipe, meshSettings))
{
    // Keep using the current terrain. Later, at an owner-thread update:
    const auto status = terrain->getGenerationStatus();
    // Only status.stage == TerrainGenerationStage::Applied means the result is live.
}
```

Additional APIs are `requestTerrain`, `requestFlat`, `requestHeightmap`,
`requestRebuild` and `cancelGeneration`. They must be serialized with scene,
render and other owner operations, just like the existing component APIs.
Custom/headless loops must call `TerrainGenerationQueue::applyCompleted()`
after joining all game/render/physics work, and `shutdown()` **before**
`PhysicsSystem::shutdown()`. Shutdown cancels and joins the worker; component
destruction and ordinary cancellation never join. The normal engine loop and
shutdown are already wired.

**Limits:** this phase removes expensive CPU preparation from the frame, not
all final-application costs. Creating/attaching PhysX shapes and the renderer's
first GPU buffer/texture uploads remain synchronous at their existing safe
points. A large result can therefore still cause a one-time application/upload
spike; this is not a guaranteed 16ms-frame streaming system. Generation keeps
the previous terrain and an independent replacement in memory, increasing
peak RAM. Chunks remain resident; there is no incremental GPU upload,
distance streaming or partial terrain editing.

## Hydraulic erosion (Phase 8)

Open **Hydraulic Erosion** in the Terrain Inspector and click
**Apply Hydraulic Erosion**. This explicitly erodes a snapshot of the **applied**
heightmap, using the other **applied** mesh/material/chunk/LOD settings, not their
unapplied drafts. The dedicated terrain worker runs seeded CPU droplets and
rebuilds the whole mesh, normals, tangents, layer weights, chunks, full-resolution
PhysX collision and LOD ranges. The previous terrain remains active until the
normal frame-boundary transaction succeeds. Progress includes a **Hydraulic
erosion** stage; cancellation and latest-accepted-request semantics are unchanged.

The simulation uses bilinear heights/gradients, inertia-driven unit-cell steps,
water/speed-dependent sediment capacity, and evaporation. Downhill height loss
increases speed; uphill motion decreases it. Erosion uses a radius-weighted disk
centered on the nearest vertex (clipped and weight-normalized at map boundaries);
removal is bounded by the step's downhill drop and available sample heights.
Deposition distributes sediment bilinearly and is bounded by each sample's
remaining normalized-height capacity. There is no min/max renormalization.
Flat maps remain flat; normalized samples always stay within `[0,1]`.

**Units and boundaries:** horizontal distances/radius are in grid cells, vertical
heights are normalized sample values. Mesh dimensions, height scale, object
transforms and rectangular-cell world aspect ratio do not affect the simulation.
This is an authoring model, not a physical water solver. Increasing grid resolution
changes its spatial scale. Edges are open: droplets leaving the grid export carried
sediment. Droplets stopping from lifetime, evaporation or zero direction settle
locally; any sediment exceeding local deposition capacity is also exported.
Consequently total height mass can decrease, but never appears from nowhere.
`TerrainErosionBudget` reports eroded/deposited/exported totals before float output
rounding; eroded equals deposited plus exported.

| Parameter | Default | Accepted range |
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

Identical samples and recipes produce identical samples in the same build,
independently of global RNG state. A fixed droplet count is **not** scaled with
resolution. Costs grow with droplets, lifetime and brush area; extreme settings
can take substantial time. Cancellation is cooperative at every droplet/step,
but subsequent mesh/cooking/upload and final-application limits from Phase 7
still apply. **Reset Erosion Draft** only resets unapplied controls.

```cpp
TerrainErosionSettings erosion;
erosion.seed = 723;
erosion.droplets = 5000;
if (terrain->requestErosion(erosion))
{
    // Accepted, not yet applied. Poll getGenerationStatus() at normal owner-thread updates.
}
```

`TerrainComponent::applyErosion` provides synchronous transactional success/failure;
`erodeTerrain(source, settings, destination, cancelled, budget)` is the standalone
CPU operation and supports aliased source/destination. Invalid input/cancellation
preserves destination and budget. Component APIs preserve pending work on invalid
requests and cancel older work only after successful replacement/acceptance.

**Persistence and cumulative editing:** payload **6** saves the actual eroded
samples plus the last successfully applied recipe. Versions **1..5** load unchanged
samples and default erosion controls. Scene/Prefab/Play clones never replay erosion.
Ordinary Rebuild, material/chunk/LOD changes, save/load, and noise/image/flat
replacement do not automatically run it. Repeated successful erosion applications
are cumulative. Requests submitted while another is pending all capture current
applied samples; they replace, rather than chain onto, pending results.
Pending requests and newer Inspector draft edits are not saved.
There is no erosion undo/history, rainfall mask, cross-terrain flow, live water
rendering or GPU erosion in this phase; Phase 9 accelerates noise generation only.

## GPU terrain noise (Phase 9)

Click **Generate Noise (GPU)** beside **Generate Noise** in the Terrain Inspector.
Both capture the same Perlin/FBM recipe and mesh-dimension draft. The ordinary
button and `generateNoise` / `requestNoise` remain CPU-based; GPU execution is
an explicit action, not a persistent backend setting or an automatic fallback.

```cpp
if (terrain->requestGpuNoise(recipe, meshSettings))
{
    // Request accepted; keep using the old terrain until status.stage == Applied.
}
```

`TerrainNoise.hlsl` runs 8x8 Compute groups, rejects out-of-grid threads, and
writes one normalized float per sample to a structured UAV. The entire existing
noise parameter range is supported: rectangular 2..1025 grids, Perlin/FBM,
uint32 seeds, negative offsets, 12 octaves and extreme frequency/lacunarity.
Only separable axis-coordinate decomposition is done on the CPU, in double
precision (`O((width + height) * octaves)`). Signed 64-bit lattice cells and
fractional offsets are uploaded independently, avoiding float coordinate
collapse at large offsets/high octaves. Hashing, gradients, quintic interpolation,
octave accumulation and normalization run in the shader. Hash arithmetic uses
32-bit pairs and exact low-64-bit multiplication; optional GPU double/Int64
features are not required.

GPU interpolation/accumulation uses floats rather than the CPU's doubles.
The same GPU recipe reproduces exact samples in the same shader/device/build;
cross-device/build bit identity is not promised. Validation requires every tested
sample to be within **2e-6 normalized height units** of CPU noise, including
extreme and maximum-grid recipes. This is a tested tolerance, not a promise of
CPU/GPU bit identity. World height differences scale with Height Scale.
Use saved samples, rather than regenerating recipes on another backend, when
exact surface preservation matters.

The renderer initializes one `TerrainGpuNoise` backend on its own D3D12 device.
It owns an independent **COMPUTE queue**, command allocator/list, immutable PSO,
upload/UAV/readback buffers and fence, separate from rendering queues/resources.
The dedicated terrain worker dispatches, waits for that fence, and reads back
heights. Owner-thread request acceptance and `applyCompleted` never wait for the
compute fence. No renderer frames are needed to finish a request. Concurrent
standalone backend calls serialize internally; the direct `generate` method is
CPU-blocking and is not intended for frame callbacks.

After readback, the existing worker pipeline builds mesh normals/tangents,
material weights, chunks, full-resolution PhysX cooking and LODs from **the same
height samples**. The frame-boundary transaction then replaces the terrain.
Saved data, height queries, collision and rendering cannot independently
regenerate or diverge. Payload remains **6**: authoritative samples plus existing
noise/erosion authoring recipes; no queue, GPU handles or execution choice is
serialized. Scene/Prefab/Play/rebuild does not rerun GPU generation.

Latest-accepted-request, invalid-input preservation, cancellation, progress,
newer Inspector drafts, and applied-only saving retain Phase 7 semantics.
GPU failure is logged and preserves the previous terrain; there is **no silent
CPU fallback**. **GPU noise** and **GPU readback** are stages, not percentages.
The displayed build time includes CPU coordinate preparation, GPU submission/
wait/readback and subsequent CPU terrain work, not GPU timestamp-only time.
Submitted GPU work is not forcibly cancelled: buffers remain owned until the
compute fence completes, even when the owner has been destroyed. Queue shutdown
joins the worker before renderer/device shutdown, then drains/releases the
backend. `DX12Renderer::finalize` enforces that ordering. Custom/headless callers
must do the same, and must not concurrently invoke backend initialize/shutdown.

The normal shader build compiles `TerrainNoise_csMain_cs.cso` with pinned DXC
and Shader Model 6.0; deployment must include it. Like required rendering
shaders, a missing/invalid CSO fails initialization explicitly. The terrain PSO
is immutable during work; this phase does not hot-reload it. Rebuild shaders
and restart to apply shader edits.

**Limits:** only Perlin/FBM height generation is GPU-accelerated here.
Image decoding, mesh/layer/chunk/LOD construction, PhysX cooking and the Phase 8
order-dependent water-droplet erosion remain CPU work. This is not a GPU-resident
terrain mesh, GPU erosion, distance streaming or a fully GPU-driven terrain
pipeline. GPU work competes with rendering on the same hardware; a separate
queue does not guarantee simultaneous execution, speedup or a particular frame
budget. Readback and old/replacement terrain increase memory use. Final PhysX
shape attachment and initial renderer uploads can still cause application spikes.

## Automatic vegetation (Phase 10)

**Scope:** opt-in, seeded static-model scatter owned by `TerrainComponent`.
There are four independent layers; no per-plant GameObjects, PhysX shapes,
wind deformation, streaming or vegetation-specific distance LOD are created.
Existing static-model frustum/layer/shadow culling, compatible opaque/alpha-test
GPU instancing and Hi-Z occlusion are reused. Transparent materials keep the
renderer’s ordinary sorted, noninstanced path.

1. Prepare and save a static `.model` / `.mdl` asset using the existing model
   tools. Use +Y up, with the origin at the plant root; model meshes, nodes,
   embedded/default materials and alpha-test materials are retained.
2. Expand **Terrain Vegetation**, enable **Enable Vegetation**, open one of the
   four **Vegetation Layer** entries, enable it, and **Select Model...**.
3. Configure the seed, density, minimum spacing, maximum instances,
   normalized height/slope ranges, uniform scale range and root offset.
   Yaw is randomized over a full turn. **Align To Surface Normal** aligns local
   +Y with the exact geometric triangle normal; otherwise plants stay upright.
   Root offset follows this chosen up axis. Shadows are controlled per layer,
   independently of the terrain surface's Cast Shadows toggle.
4. Click **Apply Vegetation**. The previous terrain and vegetation remain live
   until the complete replacement succeeds. **Reset Vegetation Draft** changes
   only unapplied edits. Existing cancellation/latest-accepted-wins behavior and
   preservation of newer Inspector drafts apply to vegetation too.

**Placement contract:** dimensions, spacing and scale are terrain-local.
Height filters use the original normalized triangle-interpolated samples,
even with zero height scale; slopes use the actual local geometric normals.
Height/slope ranges are inclusive. Grounding follows the mesh's exact
`a,c,b; b,c,d` diagonal, not bilinear interpolation. Object/parent translation,
rotation, nonuniform scaling and reflection are composed at rendering time;
they do not reseed or redistribute instances.

For each enabled layer, the worker tests `ceil(sizeX * sizeZ * density)`
uniform random XZ candidates in a deterministic order, rejecting height/slope
violations and centers closer than Minimum Spacing to an accepted center.
Spacing is checked at terrain surface anchors **before** root offset and
independently per layer, not between layers or model bounding boxes.
`density` therefore controls the **candidate budget**, not a guaranteed final
population. Filtering/spacing can reduce counts; `maxInstances` stops further
placement when reached. Layers with the same seed/rules intentionally produce
the same layout; use different seeds to avoid matching distributions.

Limits are density **0..100** per local XZ area, spacing **0.1..10000**,
maximum instances **1..25000** per layer (**100000** per terrain),
normalized height **0..1**, slope **0..90 degrees**, scale **0.001..1000**,
and root offset **-1000..1000**. Each layer permits at most **100000
candidates**; exceeding this budget fails explicitly instead of silently
truncating or blocking for an unbounded workload. Disabled numeric fields
are still validated. Zero density is valid and produces no instances.
The cap is a safety bound, not a promise that 100000 visible models will fit
your frame budget.

**Regeneration and persistence:** ordinary height imports, flat/noise/GPU
generation, erosion and mesh/material/chunk/LOD rebuilds all capture the
currently **applied** vegetation recipes and re-scatter against the resulting
authoritative samples. Chunk/LOD changes do not change the distribution.
`setVegetationSettings` is synchronous; `requestVegetation` prepares on the
dedicated terrain worker. CPU placement never accesses scene objects or asset
managers. Model/material loading and final registration remain owner-thread
operations; missing/skinned models fail explicitly and preserve the previous
mesh, vegetation and PhysX together. Loading assets, attaching physics and
initial GPU uploads can still cause frame spikes.

Payload **7** stores applied recipes and model paths alongside authoritative
height samples. Versions **1..6** migrate to disabled vegetation. Scene,
Prefab and Play/duplicate reconstruct exactly the same local transforms from
saved samples and recipes; pending work and newer Inspector drafts are not
saved. Vegetation uses model asset paths with the same working-directory /
absolute reference convention as Model Renderer; deploy those model assets
and their material/texture dependencies too. Models/materials are shared
manager resources, not owned/unloaded by individual vegetation layers.
Queued instance/model/material snapshots remain immutable and alive across
regeneration or scene destruction. Reapply vegetation to capture changed
model/default-material assets.
