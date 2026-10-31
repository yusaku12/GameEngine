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

Run current-source regression tests (Visual Studio C++ tools required):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Runtime\RunTests.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Runtime\RunTests.ps1 -Configuration Release
```

Tests use the engine project's dependencies and compiler settings with isolated
output/intermediate directories. They create a small `RuntimeFixture.scene`
beside the test executable for standalone startup checks.

After building the normal engine executable and running the regression tests,
run `powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Runtime\SmokeTest.ps1 -Configuration Debug`
(or `Release`) to verify standalone CLI failures, Unicode/space paths, Runtime
camera-color pixels, resize and clean shutdown. This briefly opens its own window
and uses Windows desktop composition to inspect it; an interactive desktop is
required. It closes only processes it starts.
