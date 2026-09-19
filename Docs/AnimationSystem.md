# Animation System Contracts

## Dependency

- Runtime and offline animation processing use ozz-animation 0.17.0 at commit `744eb9d99f606eda849acb0b1204f7a3dc20bca1`.
- Engine targets and animation tests use the static MSVC runtime (`/MTd` in Debug and `/MT` in Release).

## Transform Contract

- Engine and HLSL matrices are row-major and use `mul(vector, matrix)`.
- ozz runtime matrices use column-vector semantics.
- Conversion to the final skinning palette belongs at the runtime rendering boundary.
- Skeleton joint order is parent-first and is shared by the ozz Skeleton, inverse bind poses, local bind transforms, and vertex bone indices.
- Imported skeletons are limited to 256 joints.

## Asset Contract

- Skeleton, Animation Clip, and Animator Controller assets are independent runtime assets.
- Managers own immutable `shared_ptr<const Asset>` snapshots and expose typed index/generation handles.
- Asset references use 128-bit GUIDs. Reimport derives deterministic GUIDs from the normalized source path and the skeleton or source clip discriminator.
- A Skeleton Signature hashes ordered hierarchy paths and parent indices. A clip is compatible only when its stored Skeleton GUID and Signature match the target Skeleton.
- Missing source animation channels are populated from the Skeleton local bind transform.

## Serialization

- Skeleton asset version is 1 and uses the `SKEL` FlatBuffers identifier.
- Animation Clip asset version is 1 and uses the `ANIM` FlatBuffers identifier.
- Animator Controller asset version is 1 and uses the `ACTR` FlatBuffers identifier.
- FlatBuffers stores metadata and an official ozz Archive payload. Runtime ozz object memory is never persisted directly.
- Each Archive payload stores a 64-bit checksum and is rejected when corrupted.
- Model asset version is 3. It appends a Skeleton GUID and Animation Clip GUID list while retaining legacy embedded Skeleton and Animation fields.

## Migration

- Model versions 1 and 2 remain readable.
- On load, legacy embedded Skeleton and Animation data is converted through the same offline asset builder used by import.
- Model version 3 preserves existing animation asset GUID references; missing references are generated deterministically from source metadata.
- Legacy fields remain available until a later format migration explicitly removes them.

## Thread Ownership

- Animation asset manager operations are thread-safe.
- Published assets are immutable. Runtime sampling state and reusable job buffers are owned by the future Animator instance and are not shared between update jobs.

## Phase 2 Runtime

- `AnimatorInstance` owns playback time, speed, loop count, `SamplingJob::Context`, SoA local transforms, and model-space matrices per GameObject.
- `SamplingJob` and `LocalToModelJob` are the only runtime pose evaluation path. Playback supports Once, Loop, PingPong, negative speed, and large delta times.
- Snapshot storage is a three-slot ring allocated when assets change. Normal updates reuse all sampling and snapshot buffers.
- The row-vector shader palette is `inverseBindPose * currentModelMatrix`. ozz matrix columns are copied into DirectX matrix rows to convert conventions.
- Each snapshot includes a separate inverse-transpose normal palette so non-uniform joint scale does not corrupt normals, tangents, or bitangents.
- `ModelRenderSubmission` owns a shared immutable snapshot until queue consumption. The renderer uploads each unique snapshot once into a fence-protected per-frame buffer and shares it across color, depth, and shadow passes.
- Static models submit no snapshot and continue using the model cache's identity position and normal palettes.
- Dynamic bounds initially use the conservative imported model bounds; CPU vertex deformation is not performed.

## Phase 3 Controller Runtime

- The initial Controller format has one explicit layer with a stable ID, a default state, stable state/parameter/transition IDs, and Float/Int/Bool/Trigger parameters.
- State motions reference Animation Clip GUIDs. Controller binding resolves all state and condition references to array indices and rejects missing or Skeleton-incompatible clips before playback.
- Transition priority is the serialized array order. Any State and current-state transitions share that order, so the first eligible transition wins deterministically.
- Exit time and every condition must pass before a transition starts. Trigger values are consumed only from the selected transition; failed and lower-priority transitions do not consume them.
- Current and destination states own independent `SamplingJob::Context` and local-pose buffers. Cross fades use two `BlendingJob::Layer` values followed by `LocalToModelJob`.
- Controller changes allocate and resolve buffers. Normal updates reuse state poses, blend output, model matrices, and the existing snapshot ring without heap allocation or string lookup.

## Phase 4 Persistence

- Scene and Prefab asset versions are 2. Version 1 files remain readable through the legacy `component_types` fallback.
- Components expose a versioned opaque byte payload contract. Scene and Prefab store type name, enabled state, payload version, and bytes without coupling their schemas to individual Component types.
- Component serialization order is stable by registered type name. Registry factories restore known Components; unknown types and their payloads are ignored safely.
- Animator payload version 1 stores Skeleton, Clip, and Controller GUIDs, speed, root-motion and play state, current state/time, and typed parameter overrides.
- Animator GUIDs and pending playback values remain serializable when referenced assets are missing. Once assets are available, binding resolves GUIDs through `AnimationAssetManager` and applies the saved state.
