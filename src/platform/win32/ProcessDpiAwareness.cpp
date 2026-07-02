#include "ProcessDpiAwareness.h"

namespace
{
typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(HANDLE dpi_context);
typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(INT awareness);
typedef BOOL(WINAPI* SetProcessDPIAwareProc)();

constexpr INT kProcessPerMonitorDpiAware = 2;
constexpr LONG_PTR kDpiAwarenessContextPerMonitorAwareV2 = -4;
constexpr LONG_PTR kDpiAwarenessContextPerMonitorAware = -3;

BOOL TrySetProcessDpiAwarenessContext()
{
    const auto user32 = GetModuleHandle(L"user32.dll");
    if (user32 == nullptr)
    {
        return FALSE;
    }

    const auto set_dpi_awareness_context =
        reinterpret_cast<SetProcessDpiAwarenessContextProc>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_dpi_awareness_context == nullptr)
    {
        return FALSE;
    }

    if (set_dpi_awareness_context(reinterpret_cast<HANDLE>(kDpiAwarenessContextPerMonitorAwareV2)))
    {
        return TRUE;
    }

    return set_dpi_awareness_context(reinterpret_cast<HANDLE>(kDpiAwarenessContextPerMonitorAware));
}

BOOL TrySetProcessDpiAwareness()
{
    const auto shcore = LoadLibrary(L"shcore.dll");
    if (shcore == nullptr)
    {
        return FALSE;
    }

    const auto set_process_dpi_awareness =
        reinterpret_cast<SetProcessDpiAwarenessProc>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
    const auto result =
        set_process_dpi_awareness != nullptr && SUCCEEDED(set_process_dpi_awareness(kProcessPerMonitorDpiAware));
    FreeLibrary(shcore);
    return result;
}

BOOL TrySetProcessDPIAware()
{
    const auto user32 = GetModuleHandle(L"user32.dll");
    if (user32 == nullptr)
    {
        return FALSE;
    }

    const auto set_process_dpi_aware =
        reinterpret_cast<SetProcessDPIAwareProc>(GetProcAddress(user32, "SetProcessDPIAware"));
    return set_process_dpi_aware != nullptr && set_process_dpi_aware();
}
} // namespace

BOOL ProcessDpiAwareness::EnableNativeMonitorPixels()
{
    if (TrySetProcessDpiAwarenessContext())
    {
        return TRUE;
    }

    if (TrySetProcessDpiAwareness())
    {
        return TRUE;
    }

    return TrySetProcessDPIAware();
}
