---

## applyTo: "**/*.cpp,**/*.h,**/*.hpp,**/*.inl"

# C++ 実装指示書

## 1. 基本

Modern C++を使用する。

基本言語規格はC++20以降とする。

既存プロジェクトで使用している規格が異なる場合は、既存設定を優先する。

---

## 2. Ownership

所有権は明確にする。

使用基準:

* std::unique_ptr → 単独所有
* std::shared_ptr → 真に共有所有が必要な場合
* std::weak_ptr → shared_ptrへの非所有参照
* T& → 非所有参照
* T* → 非所有参照
* Handle → Engine Resource

raw pointerによる所有権管理は禁止。

---

## 3. RAII

ResourceはRAIIで管理する。

new/deleteを直接使用することを基本にしない。

例外的に必要な場合は、所有権が明確になるように設計する。

---

## 4. Const Correctness

変更しないものにはconstを付ける。

適切な場合:

const T&
std::span<const T>
std::string_view

を使用する。

---

## 5. Casting

C-style castは禁止。

必要に応じて:

static_cast
dynamic_cast
const_cast
reinterpret_cast

を使用する。

reinterpret_castは必要最小限にする。

---

## 6. enum

通常のenumよりenum classを優先する。

---

## 7. Header

Headerでは不要なincludeを避ける。

Forward Declarationが利用可能なら検討する。

Circular Dependencyを作らない。

---

## 8. クラス設計

クラスには明確な責務を持たせる。

一つのクラスが以下をすべて担当するような設計は禁止:

* Resource Loading
* Rendering
* Input
* Scene
* Physics
* Audio
* Networking

必要に応じて責務を分割する。

---

## 9. 継承

継承は本当にpolymorphismが必要な場合のみ使用する。

基本はcompositionを優先する。

---

## 10. Performance

Hot Pathでは不要なallocationを避ける。

特に以下では注意する。

* Render Loop
* ECS Update
* Particle Update
* Animation Update
* Physics Update
* Command Recording

---

## 11. Exception

既存EngineがExceptionを使用していない場合、Exceptionを主要なエラー処理方式として新規導入しない。

EngineのResult / Error Systemを優先する。

---

## 12. Thread Safety

共有状態を扱うクラスはThread Safetyを明確にする。

必要であればDoxygenで記載する。

例:

@thread_safety Thread-safe.
@thread_safety Not thread-safe. Access must be synchronized externally.

---

## 13. Doxygen

Public APIにはDoxygenを使用する。

例:

/**

* @brief GPU Bufferを作成する。
*
* @param size Bufferサイズ。
* @param usage Bufferの用途。
*
* @return 作成されたBuffer。
*
* @thread_safety Thread-safeではない。
  */

---

## 14. Build

変更後は可能な限りBuildして確認する。

コンパイルできないコードを完成品として報告しない。

---

## 15. Warning

新しいWarningを追加しない。

Warningを無視するためにCompiler Warningを無効化しない。

---

## 16. 既存コード

既存コードを修正する場合は、まず使用箇所を確認する。

API変更時にはCallerへの影響を確認する。

---

## 17. コード生成

新しいコードを作る際は既存コードの:

* 命名
* インデント
* Header構造
* Namespace
* Include順序
* Error Handling
* Logging
* Doxygen

を可能な限り維持する。

---

## 18. プルリクエスト

必ずプルリクエストを作成する。
プルリクエストを作成する際は、以下を確認する。

* コードスタイルが統一されているか
* 不要な変更が含まれていないか
* ビルドが通るか
* テストが通るか
* Doxygenコメントが適切に記載されているか

レビューを依頼する前に、自分で変更内容を確認する。
