#include "Pch.h"
#include "Core\Threading\ThreadUtility.h"

namespace Engine
{
    using SetThreadDescriptionFunction = HRESULT(WINAPI*)(HANDLE, PCWSTR);

    /**
     * @brief SetThreadDescriptionを動的に解決する
     * 古いWindowsでは存在しないため、実行時に取得する
     */
    static SetThreadDescriptionFunction resolveSetThreadDescription()
    {
        static SetThreadDescriptionFunction function = []() -> SetThreadDescriptionFunction
            {
                HMODULE module = ::GetModuleHandleW(L"kernel32.dll");
                if (module == nullptr)
                    return nullptr;

                return reinterpret_cast<SetThreadDescriptionFunction>(
                    reinterpret_cast<void*>(::GetProcAddress(module, "SetThreadDescription")));
            }();

        return function;
    }

    ThreadContext& getCurrentThreadContext()
    {
        static thread_local ThreadContext context;
        context.threadId = getCurrentThreadId();
        return context;
    }

    void setCurrentThreadRole(ThreadRole role)
    {
        ThreadContext& context = getCurrentThreadContext();
        context.role = role;
    }

    bool isMainThread()
    {
        return getCurrentThreadContext().role == ThreadRole::Main;
    }

    bool isRenderThread()
    {
        return getCurrentThreadContext().role == ThreadRole::Render;
    }

    void setCurrentThreadName(const char* name)
    {
        if (name == nullptr)
            return;

        ThreadContext& context = getCurrentThreadContext();
        context.name = name;

        SetThreadDescriptionFunction function = resolveSetThreadDescription();
        if (function == nullptr)
            return;

        const std::wstring wideName = String(name).toWide();
        function(::GetCurrentThread(), wideName.c_str());
    }

    uint32_t getCurrentThreadId()
    {
        return static_cast<uint32_t>(::GetCurrentThreadId());
    }

    uint32_t getHardwareConcurrency()
    {
        const unsigned int count = std::thread::hardware_concurrency();
        return count != 0 ? static_cast<uint32_t>(count) : 1u;
    }

    bool setCurrentThreadAffinity(uint32_t processorIndex)
    {
        if (processorIndex >= 64)
            return false;

        const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << processorIndex;
        return ::SetThreadAffinityMask(::GetCurrentThread(), mask) != 0;
    }
} // namespace Engine