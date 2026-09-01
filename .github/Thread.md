---
applyTo: "Engine/**/Threading/**/*.cpp,Engine/**/Threading/**/*.h,Engine/**/Threading/**/*.hpp,Engine/**/Thread*.cpp,Engine/**/Thread*.h,Engine/**/Job*.cpp,Engine/**/Job*.h,Engine/**/Task*.cpp,Engine/**/Task*.h"
---

# Threading / Job System 実装指示書

## 1. 目的

Engineはマルチスレッド実行を前提として設計する。

目的:

- CPU利用率の向上
- Asset Loadingの非同期化
- Animationの並列処理
- Physicsの並列処理
- ECS Systemの並列処理
- Render Preparationの並列化
- CPU/GPU並列実行

ただし、ThreadingによってArchitectureを不必要に複雑化してはならない。

---

# 2. Thread Model

基本的に以下のThreadを考慮する。

- Main Thread
- Render Thread
- Worker Thread
- Asset Loading Thread
- Background Thread

実際に必要なThreadだけを作成する。

---

# 3. Main Thread

Main Threadは以下を担当可能とする。

- Application
- Window
- Input
- Game Logic
- Editor

ただし、重い処理をMain Threadへ集中させない。

---

# 4. Render Thread

Render Threadは以下を担当可能とする。

- Render Preparation
- Command Recording
- GPU Submission

DirectX 12 APIへアクセスするThreadのルールを明確にする。

---

# 5. Worker Thread

Worker ThreadはCPU負荷の高い処理を担当する。

例:

- Asset Loading
- Animation Evaluation
- Mesh Processing
- Texture Processing
- ECS Processing
- Particle Processing
- Visibility Processing

---

# 6. Job System

将来的に以下を提供可能な設計にする。

- Job
- Task
- Job Queue
- Worker
- Job Dependency
- Job Completion
- Task Group
- Cancellation
- Continuation

---

# 7. Job設計

Jobは可能な限り小さく独立した処理単位として設計する。

巨大なJobへ大量の処理を詰め込まない。

ただし、Jobの細分化によってScheduling Overheadが増えすぎないようにする。

---

# 8. Job Dependency

Job間に依存関係がある場合は明示する。

例:

Asset Load
↓
Mesh Processing
↓
GPU Upload
↓
Resource Ready

依存関係をMutexだけで表現しない。

---

# 9. Thread Pool

Thread Poolを利用する場合、Worker Thread数を適切に管理する。

CPU Core数を考慮する。

無意味に大量のThreadを生成しない。

---

# 10. Synchronization

Synchronization Primitiveを用途に応じて選択する。

利用可能:

- std::mutex
- std::shared_mutex
- std::condition_variable
- std::atomic
- std::semaphore
- std::latch
- std::barrier

必要に応じてEngine独自Primitiveを実装する。

---

# 11. Mutex

Mutexは必要な箇所だけに使用する。

Mutexを追加するだけでThread Safety問題を解決したことにしない。

Lock Scopeを可能な限り小さくする。

---

# 12. Atomic

単純な共有状態にはstd::atomicを利用する。

ただし、Atomicにすれば常に安全とは考えない。

複数の状態をAtomicだけで整合性を維持しようとしない。

---

# 13. Lock-free

Lock-free Algorithmは必要な場合のみ使用する。

「Lock-freeだから高速」と仮定しない。

実際のBenchmarkで判断する。

---

# 14. Data Race

Data Raceを絶対に発生させない。

共有Dataについて以下を明確にする。

- Owner
- Reader
- Writer
- Lifetime
- Synchronization

---

# 15. Immutable Data

複数ThreadからアクセスするDataは、可能であればImmutableにする。

Read-only Dataは同期コストを削減できる。

---

# 16. Thread-local

Threadごとに独立して利用できるDataはThread-localを検討する。

用途例:

- Temporary Allocator
- Scratch Memory
- Job Context
- Logging Context

---

# 17. ECSとの連携

ECS Systemは将来的に並列実行可能な設計を目指す。

System間のRead / Write Dependencyを明確にする。

同一Componentを複数Threadから同時に変更する場合は適切に同期する。

---

# 18. Renderingとの連携

Render Preparationは可能な限り並列化可能な設計にする。

例:

Scene
↓
Parallel Culling
↓
Parallel Render Item Generation
↓
Sorting
↓
Render Thread
↓
Command Recording

GPU Command Recording自体のThreadingについてはDirectX 12の制約を確認する。

---

# 19. GPU Synchronization

CPU Thread SynchronizationとGPU Synchronizationを混同しない。

CPU Thread間:

- Mutex
- Atomic
- Job Dependency

GPU / CPU間:

- Fence

を基本とする。

---

# 20. Wait

Worker ThreadやMain Threadを不必要にBlockしない。

以下のような設計を避ける。

Job Start
↓
Wait
↓
Job Complete

非同期化の意味がなくなるため、可能な限りDependencyで解決する。

---

# 21. Shutdown

Thread / Job SystemのShutdownでは以下を保証する。

1. 新しいJobを受け付けない
2. Queueを停止する
3. Worker Threadを安全に停止する
4. Pending Jobを処理または破棄する
5. Worker ThreadをJoinする
6. Resourceを安全に破棄する

---

# 22. Exception

Worker Thread内で発生したExceptionを無視しない。

Job SystemのError Handling方針を明確にする。

Threadを突然Terminateさせる設計は禁止する。

---

# 23. Logging

複数ThreadからLoggerを使用する場合、LoggerのThread Safetyを確認する。

大量のWorker Threadから毎Frame大量Loggingしない。

---

# 24. Performance

Threadingの目的はCPU Performance向上である。

ただし以下のOverheadを考慮する。

- Job Scheduling
- Context Switching
- Lock Contention
- Cache Miss
- False Sharing
- Synchronization
- Memory Allocation

Thread数を増やすだけの最適化は禁止する。

---

# 25. False Sharing

頻繁に更新されるThread-local / Atomic DataではFalse Sharingを考慮する。

必要に応じてCache Line Alignmentを使用する。

---

# 26. 禁止事項

以下は禁止する。

- 無意味なThread生成
- Thread Safetyを無視したGlobal State
- 広範囲なMutex Lock
- 毎FrameのThread生成 / 破棄
- Data Race
- Deadlockを考慮しないLock
- GPU FenceとCPU Mutexの混同
- BenchmarkなしのThreading最適化

---

# 27. 実装方針

Threading機能を追加するときは、

1. 処理の依存関係を確認
2. Shared Dataを特定
3. Ownershipを確認
4. Read / Writeを確認
5. Job Dependencyを設計
6. Synchronizationを最小化
7. Benchmark
8. Data Raceを確認
9. Shutdownを確認

の順番で実装する。

ConcurrencyよりもCorrectnessを優先する。