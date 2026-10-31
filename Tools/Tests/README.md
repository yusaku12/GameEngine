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
