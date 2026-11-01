#pragma once
#include <memory>
#include <span>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;

namespace Engine
{
    struct UIDraw;
    /**
     * @brief Native GPU Canvas renderer using Direct2D/DirectWrite over D3D11On12.
     * @details Scene commands must be submitted first. Acquires a render-target-state resource,
     * draws UI, releases it in the same state, then flushes onto the shared direct queue.
     * @thread_safety Not thread-safe. Use only after joining scene jobs; no concurrent queue access.
     */
    class RuntimeUIRenderer final
    {
    public:
        /** @brief Creates an empty native renderer without GPU resources. */
        RuntimeUIRenderer();
        /** @brief Releases resources; the caller must first make the queue idle. */
        ~RuntimeUIRenderer();
        /** @brief Initializes native 2D rendering and Unicode text shaping. */
        bool initialize(ID3D12Device& device, ID3D12CommandQueue& queue);
        /** @brief Releases target interop wrappers before the underlying render target is resized. */
        void releaseTarget() noexcept;
        /** @brief Draws immutable UI snapshots onto a submitted render target. */
        bool render(ID3D12Resource& target, std::span<const UIDraw> draws);
        /** @brief Releases caches and native devices after the queue is idle. */
        void finalize() noexcept;
    private:
        RuntimeUIRenderer(const RuntimeUIRenderer&) = delete;
        RuntimeUIRenderer& operator=(const RuntimeUIRenderer&) = delete;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
