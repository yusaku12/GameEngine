---
applyTo: "Engine/**/Memory/**/*.cpp,Engine/**/Memory/**/*.h,Engine/**/Memory/**/*.hpp,Engine/Core/Memory/**/*.cpp,Engine/Core/Memory/**/*.h,Engine/Core/Memory/**/*.hpp"
---

# Memory System 実装指示書

## 1. 目的

Memory SystemはEngine全体のCPU / GPUメモリ管理を担当する基盤システムである。

Memory Systemは以下を目的とする。

- 安全なメモリ管理
- 明確なOwnership
- Allocation回数の削減
- Fragmentationの抑制
- Cache Localityの向上
- Debug時のMemory Tracking
- GPU Resource Allocationとの連携
- Frame単位の一時メモリ管理

ただし、初期段階から過剰に複雑なMemory Systemを構築してはならない。

必要になった機能から段階的に導入する。

---

# 2. 基本原則

Memory Systemでは以下を最優先する。

1. Correctness
2. Safety
3. Debuggability
4. Performance
5. Complexity

性能向上のためにMemory Safetyを犠牲にしてはならない。

---

# 3. Ownership

すべてのMemoryについてOwnershipを明確にする。

基本ルール:

- `std::unique_ptr` → Exclusive Ownership
- `std::shared_ptr` → Shared Ownershipが本当に必要な場合のみ
- `std::weak_ptr` → Non-owning Reference
- Reference → Non-owning Access
- Raw Pointer → 原則Non-owning Access
- Engine Handle → Engine Resource

Raw Pointerを所有権管理に使用してはならない。

---

# 4. RAII

CPU Resourceは原則RAIIで管理する。

対象:

- Heap Memory
- File Handle
- OS Handle
- Synchronization Object
- GPU Resource Wrapper
- Temporary Resource

`new` / `delete`を直接使用する設計を避ける。

---

# 5. Allocator

必要に応じて以下のAllocatorを提供できる設計にする。

- General Allocator
- Linear Allocator
- Stack Allocator
- Pool Allocator
- Free List Allocator
- Frame Allocator
- Arena Allocator
- Upload Allocator

ただし、用途のないAllocatorを実装してはならない。

---

# 6. General Allocator

通常の長寿命CPU Memory Allocationを担当する。

一般的なEngine ObjectのAllocationに利用する。

---

# 7. Linear Allocator

大量の一時Memoryを高速に確保する用途に使用する。

基本:

Allocate
↓
Allocate
↓
Allocate
↓
Reset

個別Freeを必要としない用途に適している。

---

# 8. Frame Allocator

Frame単位で使用するTemporary Memoryを管理する。

用途例:

- Render Command Data
- Temporary Arrays
- Render Item List
- Visibility List
- Sorting Data
- Per-frame CPU Data

Frame終了後にまとめてRecycleできる設計を優先する。

---

# 9. Pool Allocator

同一サイズまたは限定されたサイズのObjectを大量に生成する場合に使用する。

用途例:

- ECS関連Temporary Object
- Particle
- Command Object
- Small Engine Object

ただし、標準Allocatorより本当に有効か確認してから導入する。

---

# 10. GPU Memory

CPU MemoryとGPU Memoryを明確に区別する。

DirectX 12では以下を考慮する。

- Default Heap
- Upload Heap
- Readback Heap

用途を明確にして使用する。

---

# 11. Upload Memory

CPUからGPUへデータを転送するMemoryを管理する。

用途例:

- Constant Buffer
- Vertex Buffer Upload
- Index Buffer Upload
- Texture Upload

一時Upload ResourceはGPU使用完了前に破棄してはならない。

---

# 12. Readback Memory

GPUからCPUへデータを取得する場合はReadback Heapを使用する。

CPUが即座に読み取れるとは限らない。

GPU / CPU Synchronizationを適切に行う。

---

# 13. Alignment

DirectX 12のAlignment Requirementを常に考慮する。

特にConstant Bufferでは256-byte Alignmentを考慮する。

Allocation SizeとRequested Sizeを混同しない。

---

# 14. Memory Lifetime

Memory Lifetimeを明確にする。

代表:

- Engine Lifetime
- Scene Lifetime
- Resource Lifetime
- Frame Lifetime
- Command Lifetime
- GPU Lifetime

CPU側で不要になったResourceでも、GPUが使用中なら即座に破棄してはならない。

---

# 15. Deferred Destruction

GPU ResourceについてはDeferred Destructionを利用できる設計にする。

基本:

Resource Release Request
↓
Fence Value記録
↓
GPU Execution
↓
Fence Completion
↓
Actual Release

GPU使用中のResourceを直接破棄しない。

---

# 16. Memory Tracking

Debug Buildでは可能な限り以下を追跡できるようにする。

- Allocation Count
- Free Count
- Current Usage
- Peak Usage
- Allocation Size
- Allocation Location
- Owner
- Lifetime

---

# 17. Memory Leak

Debug BuildではMemory Leakを検出可能にする。

Engine Shutdown時に未解放Allocationを確認できる設計を検討する。

---

# 18. Fragmentation

Long-running EngineではMemory Fragmentationを考慮する。

ただし、最初から複雑なCompaction Systemを実装しない。

実際のProfiling結果に基づいて改善する。

---

# 19. Thread Safety

AllocatorがThread-safeかどうかを明確にする。

必要に応じて:

- Thread-safe Allocator
- Thread-local Allocator
- Per-thread Arena
- Lock-free Structure

などを利用する。

すべてのAllocatorを無条件にThread-safeにしない。

---

# 20. Performance

Hot PathではAllocationを可能な限り避ける。

特に:

- Render Loop
- ECS Update
- Animation Update
- Physics Update
- Particle Update
- Command Recording

では注意する。

---

# 21. Debug / Release

Debug Buildでは安全性と診断情報を優先する。

Release Buildでは必要に応じてDebug Trackingを削減できる設計にする。

ただし、Debug機能を無効化するためだけにMemory SystemのArchitectureを複雑化しない。

---

# 22. 禁止事項

以下は禁止する。

- Raw PointerによるOwnership管理
- 不要なGlobal Allocator
- 毎Frameの大量Heap Allocation
- GPU使用中Resourceの破棄
- Alignmentを無視したAllocation
- Leakの無視
- Thread Safetyを無視した共有Allocator
- Profilingなしの過剰最適化
- 必要性のないCustom Allocatorの乱立

---

# 23. 実装方針

Memory Systemを拡張するときは、

1. 実際のAllocation Patternを調査
2. Bottleneckを特定
3. 適切なAllocatorを選択
4. Lifetimeを確認
5. Thread Safetyを確認
6. Debug Trackingを追加
7. Benchmark
8. 必要に応じて最適化

の順番で進める。

「速そうだから」という理由だけでCustom Allocatorを追加しない。