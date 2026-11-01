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

Scene editing/saving and transform gizmos are disabled during Play. Click the
**Game** view to give gameplay keyboard/mouse/gamepad input; selecting another
panel or losing window focus clears transient input and stops vibration.
Editor camera navigation runs only while editing. Editing does not execute
Awake/Start/Update/FixedUpdate/LateUpdate or enable physics actors.

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
