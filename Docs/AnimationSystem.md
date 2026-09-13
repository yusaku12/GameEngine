# Animation System Contracts

## Phase 0 dependency

- ozz-animation is the `Library/ozz-animation` submodule pinned to release `0.17.0` (commit `744eb9d99f606eda849acb0b1204f7a3dc20bca1`).
- The dependency is built as static libraries with the static MSVC runtime. Tests, samples, how-tos, FBX tools, and glTF tools are disabled.
- ozz is distributed under the MIT license tracked at `Library/ozz-animation/LICENSE.md`.
- `Tools/Animation/build_ozz.ps1 -Configuration Debug|Release -RunTests` builds the dependency and runs the offline/archive/runtime smoke test.

## Transform contract

- Engine and shader matrices are row-major. Shader vertices use `mul(vector, matrix)`.
- Assimp matrices are transposed into the engine row-vector representation by `AssimpModelImporter::convertMatrix`.
- ozz `Float4x4` values are column-vector matrices. Conversion to an engine `Matrix` must transpose the 4x4 values exactly once before skinning calculations.
- The skinning palette contract is `inverseBindPose * animatedModelMatrix` in engine row-vector convention. Phase 2 must lock this with bind-pose identity and known one-joint rotation tests before renderer integration.
- Coordinate handedness and unit conversion must be applied consistently to mesh, skeleton bind pose, and animation tracks. Existing FBX root scale behavior remains unchanged until Phase 1 replaces it with an importer-wide conversion contract.

## Joint contract

- ozz skeleton joint order is authoritative for runtime tracks, inverse bind poses, and vertex bone indices.
- Parent indices precede children. Import rejects duplicate hierarchy paths, missing weighted joints, invalid parents, and more than 256 skinning joints.
- Compatibility uses a stable signature over ordered hierarchy paths and parent indices, not display names alone.

## Asset and serialization contract

- Model, Skeleton, Animation Clip, and Animator Controller are separate assets addressed persistently by `AssetGUID`.
- Runtime ozz objects are persisted only through the official ozz Archive API. FlatBuffers stores versioned metadata and an ozz Archive blob; it never stores the in-memory object representation.
- Existing `.model` v1/v2 skeleton and animation fields remain readable during migration. Phase 1 adds conversion to the separate assets before those legacy fields can be removed.

## Ownership and threading

- Asset managers own immutable shared asset snapshots and protect mutable manager state with a mutex.
- `AnimatorInstance` owns playback state, sampling contexts, and reusable pose buffers per GameObject; it is main-thread only initially.
- Animator code does not own DirectX resources or command lists.
- Render submissions own immutable skinning palette snapshots through queue consumption. The render thread never references mutable animator memory.
- GPU model and palette caches remain render-thread only.
