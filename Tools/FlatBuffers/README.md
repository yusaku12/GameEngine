# FlatBuffers workflow

The schemas in `Schemas/FlatBuffers` are the source of truth. Generated headers are written to `Generated/FlatBuffers` and are not edited by hand.

## First-time setup

The Visual Studio project uses `Tools/FlatBuffers/flatc.exe` by default. To use another installation, set the MSBuild property `FLATBUFFERS_FLATC_EXECUTABLE` or the environment variable with the same name to the full path of a matching `flatc.exe`.

The repository currently expects FlatBuffers `25.12.19`; generated headers contain a compile-time version check.

## Manual generation

From the repository root:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\FlatBuffers\build_schema.ps1
```

The batch equivalent is:

```bat
Tools\FlatBuffers\build_schema.bat
```

Both commands fail with a clear message when `flatc.exe` is unavailable. They generate the shared and model headers together because `Model.fbs` includes `Common.fbs`.

## Normal Visual Studio build

No manual generation is required. `GameEngine.vcxproj` runs the PowerShell generator before C++ compilation and tracks both schema files as inputs. A schema change therefore regenerates the headers before the affected C++ files compile.

## Using the model serializer

```cpp
#include "Assets\\Model\\Serialization\\ModelSerializer.h"

Engine::Serialization::ModelSerializer serializer;
Engine::ModelResource model;

if (!serializer.save("Data/Characters/player.mdl", model))
    return false;

Engine::ModelResource loaded;
if (!serializer.load("Data/Characters/player.mdl", loaded))
    return false;
```

`ModelSerializer` converts between engine resource types and generated FlatBuffers types. It writes the `MODL` file identifier, schema version `1`, and model asset version `1`. Loading verifies the identifier, FlatBuffers buffer, supported versions, required bounds, matrix sizes, and resource indices before returning data.

Asset paths should be project-relative or asset IDs, not absolute machine paths. `sourcePath` is stored as the model's generic path string and is not used to open files during deserialization.

## Schema evolution

Only append fields or tables. Do not change the meaning or type of an existing field and do not reuse removed fields. Increment the relevant asset version when engine-side migration is needed; update `SerializationVersions.h` and add migration logic before accepting the new version.