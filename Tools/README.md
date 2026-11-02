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
Each dependency's build system checks its own source dependencies on every
engine build, so existing libraries do not mask changes to dependency source.

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

The old `GameEngine\x64` intermediates and compiler files formerly mixed into
`x64\<Configuration>` are not needed after rebuilding with the current project.
Keep `GameEngine.exe`, its `.pdb`, and `libfbxsdk.dll` in the executable directory.
Ignored or untracked files are not automatically safe to delete.

Initialize registered submodules with `git submodule update --init --recursive`.
The current local PhysX, ImGui and spdlog checkouts also contain required files
that are not registered in the parent repository; ImGui and spdlog have local
changes. They are deliberately preserved. A fresh clone is not self-contained
until those dependency sources and changes are supplied separately.

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

## PostEffect Phase 0/1/2/3/4 validation

Run the deterministic GPU validation from the project root with a **Debug x64**
build. It uses the hardware D3D12 renderer with Debug Layer and GPU-based
validation, a hidden Runtime window, and procedurally generated geometry and
materials. It does not load or modify the editor scene or preferences. Runtime UI
interop is not initialized in this UI-free mode; normal launches retain it.

```powershell
.\x64\Debug\GameEngine.exe --validate-rendering ".\Build\RenderingBaseline"
```

The output directory must be new or empty; existing baselines are not overwritten.
This option cannot be combined with `--runtime` or `--scene`. The process exits
automatically with code 0 only after all checks and saves succeed. Release builds
cannot pass the required Debug Layer checks.

The fixture contains middle gray, an HDR highlight `(4, 2, 1)`, linear albedo,
transparent-over-opaque blending, and visible/discarded alpha-tested panels.
Camera, background, ambient lighting, exposure (1), and geometry remain fixed.
For baseline, repeat, serial command recording, odd-size resize (641x359), and
restored size (1280x720), three frames exercise both in-flight slots before capture.
Same-size captures must match bit-for-bit. Resize captures must independently pass
the same analytical probes. Saved DDS/PNG images are decoded again and must match
the GPU readback bit-for-bit.

Each case saves `*-hdr.dds` (linear RGBA16F, preserving values above 1) and
`*-display.png` (sRGB-encoded final RGBA8). Every final pixel is compared against
an independent CPU implementation of the current tone-map fit and sRGB encoding,
with at most 2 byte levels of RGB error. HDR probes use a 0.003 absolute tolerance
for half-float and G-Buffer quantization. Nonfinite/negative HDR, unexpected final
alpha, D3D12 warnings/errors, and discarded debug messages fail validation.
`white.dds` is the generated fixture texture; `results.json` records successful
checks. An incomplete directory without a successful process exit is not a valid
baseline.

Keep a known-good directory unchanged and run subsequent checks into a different
directory. These tests establish the Runtime HDR/composite baseline; they do not
certify perspective cameras, shadows, real asset textures, or editor UI.

Phase 1 also checks empty/disabled lists (one required output draw, no temporary
textures), eight HDR identity passes over 30 frames, repeated pool reuse, LDR
identity passes, HDR/LDR stage ordering, half-resolution processing including odd
sizes, fractional camera viewports, and descriptor release after replacement and
resize. Half-resolution output is compared pixel-by-pixel against independent CPU
bilinear resampling with half-float intermediate quantization, still with at most
2 byte levels of error. Cropped-view probes and unchanged pixels outside the
camera viewport are checked separately. Debug views and Forward/`Nothing` rendering
must remain identical with optional effects on/off. Invalid stage, divisor, and
nonfinite parameter tests intentionally log three application errors; accepting
these inputs or replacing the previous valid list fails validation. D3D12 diagnostics
must still contain **zero** warnings/errors. The version 2 report records draw and
pool counts in addition to image results.

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
         -> Bloom -> color filter / contrast / saturation
         -> exposure + tone mapping (linear HDR -> linear LDR)
         -> AfterToneMapping color passes
         -> Vignette -> FXAA
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

Without enabled LDR passes, Vignette or FXAA, tone mapping and sRGB encoding are fused into the
final output pass. Therefore an empty or entirely disabled list adds no scratch
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
tone-map fit and sRGB implementation with Deferred debug display.

This is deliberately a small color-only graphics-pass foundation, not a full
render graph. Depth/normal inputs, compute passes, history,
and custom shader asset/hot-reload tracking are later phases. Profile authoring
is implemented separately in Phase 2 below.
Common output shaders participate in the existing ShaderManager reload flow;
user-supplied snapshots are replaced explicitly. The validation scale shader is
a diagnostic fixture, not a production effect.

## PostEffect Phase 2 Profiles and Camera authoring

Select a **Camera** in the Inspector and expand **Post Effects**. This phase
defines and persists Bloom, fixed exposure, color adjustments, Vignette, and FXAA
settings. Phase 3 executes these resolved settings in Deferred rendering. Existing
renderer exposure and the diagnostic/custom pass API remain available.

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
scalar/color ranges are shared between the Inspector and validation.

Scene/Prefab Camera payload v3 stores enable, weight, local values/flags, Volume
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

`--validate-rendering` now runs Profile and Camera authoring checks first, writes
`authoring-results.json` plus isolated files under `authoring-fixtures`, and
then runs the Phase 0/1 GPU regressions. It tests all setting values and override
flags, inclusive scalar bounds and invalid inputs, atomic saves/path conflicts,
reload failures, Unicode references, Camera v0/v1/v2/v3/v4/v5 compatibility, Scene/Prefab
round trips, actual Play scene cloning, Windows path case aliases, and queued
snapshot independence. A headless ImGui context expands all authoring sections
and exercises the camera enable checkbox and New Profile button without writing
editor preferences or opening native dialogs.
Expected negative tests log application errors, clearly bracketed in the log;
only their rejection and preservation of previous state count as success.
Finally, a nonneutral, zero-weight authoring snapshot is submitted to the GPU:
it must be delivered intact while leaving both baseline images unchanged.
D3D12 diagnostics must still contain zero warnings/errors. The combined
`results.json` is version 7 and links to authoring, built-in effects, custom
effects, Volume, and Phase 6/7 reports.

## PostEffect Phase 3 built-in effects

Camera Weight 0, disabled cameras and neutral effect settings retain the one-draw,
zero-allocation baseline path. Exposure and color adjustments fuse into display
conversion when no LDR pass is needed. Settings changes do not rebuild PSOs or
wait for GPU fences; the existing fence-aware pool handles temporary resources.
Forward, `Nothing` clear mode and G-Buffer debug views bypass camera effects.
UI and the editor grid remain after post-processing. Removing the active Camera
clears its PostEffect snapshot.

- **Bloom** uses a soft threshold, ceil-half downsampling and up to six levels
  (stopping at 1x1), a normalized nine-tap tent reconstruction and an additive HDR
  composite. Scatter blends local/low-frequency energy without multiplying a
  constant field's brightness by pyramid depth. Threshold is **scene-referred**,
  before renderer exposure or EV adjustment. Intensity is multiplied by Weight.
- **Fixed exposure** multiplies renderer exposure by `exp2(EV * Weight)` once.
  No automatic exposure/history is introduced. Tone-map inputs are clamped to
  RGBA16F's finite maximum to avoid overflow at extreme EV.
- **Color adjustments** apply a linear RGB filter blended from white, contrast
  blended from 1 around middle gray (0.18), then Rec.709 saturation blended from 1,
  all in HDR before exposure/tone mapping. Negative colors are clamped to zero.
- **Vignette** blends its linear RGB color after tone mapping. Intensity blends
  from zero; smoothness controls radial falloff, and roundness blends between
  viewport-normalized and aspect-correct distance.
- **FXAA** detects directional edges using perceptual luminance and bounded
  endpoint searches, combines edge and subpixel reconstruction, and blends the
  filtered **linear LDR** result by Weight. Luminance comparisons use 12-bit
  fractional precision to stabilize nearly equal edges across floating-point
  contraction differences. sRGB encoding remains final-only.

All built-in shaders participate in ShaderManager dependency/hot-reload tracking.
Rebuild constructs all candidate PSOs before replacing the previous pipelines.
Bloom's multi-input passes never alias their output with either source.
Sampling honors Direct3D's implicit viewport scissor (floor right/bottom), so
fractional viewports never sample the undrawn bottom/right rows of temporary
targets. This fixes the old reduced-resolution cropped-viewport display fixture;
the remaining Phase 2 baseline images stay bit-for-bit unchanged.

`effects-results.json` records independent CPU/GPU per-pixel comparisons, still
limited to **2 RGB byte levels**, raw scene HDR preservation, alpha, draw counts,
Bloom depth, bounded fence-aware target/descriptor reuse and zero D3D12 warnings
or errors. Tests include EV limits, weighted colors and effects, threshold/scatter
limits, constant-field Bloom normalization, rotated diagonal FXAA (at least 10%
lower squared edge energy), combined custom HDR/LDR stages over 30 frames,
fractional/odd viewports, 1x1, single-row/column targets, disabled/zero-weight/neutral
values and Camera removal. Saved DDS/PNG files are round-trip verified. Keep
the Phase 0/1/2 baselines unchanged; save Phase 3 results into a new directory.

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

The GPU validation also writes `volume-results.json`, versioned Volume/Scene/
Prefab fixtures, and captured moving-Camera results. It checks spatial faces/
corners/shells, mirrored and parent transforms, ordering, masks, disabled/inactive/
destroyed objects, ignored scenes, persistent scenes, snapshot isolation,
workspace reuse, all scalar/color/switch descriptors, Unicode cold loading,
old Camera versions, malformed payloads and failure preservation. Headless ImGui
renders all three shapes and exercises New Profile creation. GPU cases compare
the actual Camera Volume snapshot with independently predicted settings and verify
all saved images round-trip; the effect suite additionally compares continuous
contributions to independent CPU pixel references with at most 2 RGB byte levels.
Native dialogs and arbitrary real-world scenes are not covered by these fixtures.
Keep all Phase 0-4 baselines unchanged and validate into a new output directory.

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

Phase 5 validation writes `custom-effects-results.json` and HDR/LDR display
captures. It compiles and cold-loads a custom asset, round-trips Profile
references and parameter values, checks legacy Profile v1 compatibility,
verifies failed compilation preserves the old Shader, compares both stages to
an independent CPU display reference, exercises fractional Volume/Camera
contributions, and checks Profile effect ordering through the real Camera
snapshot path. Saved DDS/PNG round-trips and D3D12 diagnostics remain required.

## PostEffect Phase 6 depth, color grading and automatic exposure

Phase 6 adds depth-of-field and exponential fog using the active Camera's
linearized deferred depth buffer. Depth-of-field uses the configured focus
distance/range and maximum blur radius; fog uses density, start distance and
color. Both run before tone mapping and require a matching Camera depth target.
The Camera near/far planes and projection type are captured with the immutable
PostEffect snapshot, so queued views use the same depth interpretation they were
authored with.

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

Phase 6 validation writes `phase6-results.json` and captures for neutral and
active DOF, depth-based fog, a generated invert-red strip LUT, current-frame
automatic exposure, and the combined stack. Checks compare raw HDR against the
neutral baseline, verify the LUT and exposure against independent CPU
references, assert effect/draw counts and output changes, round-trip saved
captures, and require zero D3D12 diagnostics.

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
Phase 7 fields load with those defaults. Delta time comes from the active Camera's
simulation update, so paused simulation holds the exposure history.

Phase 7 validation writes `phase7-results.json` and checks immediate first
sample, both adaptation directions, convergence, zero-delta freeze, Camera
history isolation, and reset after disabling automatic exposure. Captured
outputs are checked against an independent CPU recurrence and saved-image
round-trips; each active case also checks HDR invariance, pass counts, and
D3D12 diagnostics.

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
