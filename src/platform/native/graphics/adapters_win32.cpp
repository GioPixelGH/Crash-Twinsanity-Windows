// Windows: the graphics adapters in the log, for the bug reports (OpenGL's strings name the one the game draws with, in its driver's
// own numbering): each one's name, its PCI vendor and device, its driver's version as Device Manager shows it (DXGI's user mode
// driver's) and its video memory. DXGI is loaded when it's asked for, so the program doesn't need it to start
#if defined(_WIN32)
#include "display.h"

#include "native.h"

#include <windows.h>
#include <dxgi1_4.h>

#include <cstdio>

// Laptops with two graphics cards (THE LIST 87; #23: an Intel HD active beside a GTX 1050 Ti): NVIDIA's Optimus and AMD's
// switchable graphics run a program that exports these, set to 1, on the dedicated card (their drivers read them as it starts).
// Windows' own choice for the program (Settings > Display > Graphics) still comes first; with one card nothing changes. The log's
// "graphics:" line names the card that draws
extern "C"
{
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace NativeGraphics
{
void LogAdapters()
{
    // (Kept loaded: the drivers may use it)
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    auto create = dxgi != nullptr ? reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")))
                                  : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (create == nullptr || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
    {
        Native::Log("graphics: the adapters can't be listed (no DXGI)");
        return;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; index++)
    {
        DXGI_ADAPTER_DESC1 description;
        // Windows' own software adapter (the Basic Render Driver) left out
        if (SUCCEEDED(adapter->GetDesc1(&description)) && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
            char driver[48] = "unknown";
            LARGE_INTEGER version;
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
            {
                std::snprintf(driver, sizeof(driver), "%u.%u.%u.%u", HIWORD(version.HighPart), LOWORD(version.HighPart),
                              HIWORD(version.LowPart), LOWORD(version.LowPart));
            }

            Native::Log("graphics: adapter %u: %s (PCI %04x:%04x), driver %s, %llu MB of video memory", index, name,
                        description.VendorId, description.DeviceId, driver,
                        static_cast<unsigned long long>(description.DedicatedVideoMemory >> 20));
        }

        adapter->Release();
    }

    factory->Release();
}

// DXGI 1.4's count of the program's video memory (QueryVideoMemoryInfo, Windows 10's WDDM 2): each adapter's local segment (its own
// memory) and non-local one (shared memory), the adapter where the program uses the most reported. The factory is made once
bool VideoMemoryNow(VideoMemory* memory)
{
    static IDXGIFactory1* factory = nullptr;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
        auto create = dxgi != nullptr
                          ? reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")))
                          : nullptr;
        if (create == nullptr || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        {
            factory = nullptr;
        }
    }

    if (factory == nullptr)
    {
        return false;
    }

    bool known = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; index++)
    {
        IDXGIAdapter3* adapter3 = nullptr;
        DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
        DXGI_QUERY_VIDEO_MEMORY_INFO shared = {};
        if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3))))
        {
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
                SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared)) &&
                (!known || local.CurrentUsage > memory->used))
            {
                DXGI_ADAPTER_DESC1 description;
                memory->used = local.CurrentUsage;
                memory->budget = local.Budget;
                memory->shared = shared.CurrentUsage;
                memory->adapter[0] = '\0';
                if (SUCCEEDED(adapter->GetDesc1(&description)))
                {
                    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, memory->adapter, sizeof(memory->adapter) - 1, nullptr,
                                        nullptr);
                }

                known = true;
            }

            adapter3->Release();
        }

        adapter->Release();
    }

    return known;
}
}
#else
#include "display.h"

namespace NativeGraphics
{
bool VideoMemoryNow(VideoMemory*)
{
    return false;
}
}
#endif
