# Component removal

In the Inspector, click the **X** on an optional component header, or right-click
the header and choose **Remove Component**. This removes only that instance.
Required components (including Transform) cannot be removed. Removal completes
after component enumeration, invoking the existing disable/destroy lifecycle.
Saving the scene or prefab afterwards persists its updated component list.

`GameObject::removeComponent(Component&)` removes one instance, including a
queued addition. Do not use its reference after a successful removal request.
The existing `removeComponent<T>()` API still removes all instances of that type.

## Regression tests

From the repository root in a Visual Studio Developer PowerShell:

```powershell
MSBuild .\GameEngine.ComponentTests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
if ($LASTEXITCODE -eq 0) { & .\x64\ComponentTests\Debug\ComponentRemovalTests.exe }
```

The test project imports the engine's existing build/dependency settings, with
separate intermediate and output directories. Tests cover individual and
type-wide removal, required/foreign components, uninitialized/disabled lifecycle,
deferred and duplicate requests, pending additions, update self-removal,
reentrant replacement, actual Inspector X-button mouse input, and active-camera
unregistration. They also verify scene save/load does not restore deleted
components, and PhysX actor/shape counts remain correct when deleting Rigidbody
and Collider components in either order and re-adding them. No renderer or
native window is needed.

## Texture path regression tests

```powershell
MSBuild .\GameEngine.TexturePathTests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
if ($LASTEXITCODE -eq 0) { & .\x64\TexturePathTests\Debug\TexturePathTests.exe }
```

Run from the repository root. This project tests actual serialized relative
paths for all five material texture slots, relocation with an unrelated working
directory, material/model Save As, legacy model paths, version 1 material/model
compatibility and migration, rejected cross-volume and drive-relative references,
relative FBX import from a generated fixture, external model material/animation
references, drive-letter case normalization, GUID-only material hints, and
actual DDS GPU loading/cache behavior. The last test
requires a D3D12-capable device; no native window is created. Temporary assets
are created in a unique directory and removed after the test run.

Pass `--verify-jammo` to additionally verify the locally migrated Jammo model's
25 meshes, 3 embedded animations and 3 existing texture hints. Jammo assets are
ignored by Git and are not required by the default tests.
