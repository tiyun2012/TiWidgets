#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <optional>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

#ifndef NTDDI_WIN10_RS4
#error "This code requires Windows 10 SDK version 17134 (RS4) or later for IDXGIFactory6."
#endif

// Helper: translate HRESULT to readable text for diagnostics.
std::string HResultToString(HRESULT hr)
{
    char* msgBuffer = nullptr;
    DWORD msgLength = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msgBuffer),
        0,
        nullptr);

    std::string result = msgLength ? std::string(msgBuffer, msgLength) : "Unknown error";
    if (msgBuffer) {
        LocalFree(msgBuffer);
    }
    return result;
}

// Feature checks (extend as needed).
bool CheckRaytracingSupport(ID3D12Device* device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        return options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }
    return false;
}

// A minimal, engine-style device provider that selects the best adapter and
// enables enhanced debugging in debug builds, with richer diagnostics.
class DX12EngineDevice {
public:
    // Strategies for adapter selection.
    enum class SelectionStrategy {
        HighPerformance,   // Respect DXGI GPU preference ordering.
        MinimumPower,
        SpecificVendor,    // Match vendorId when provided.
        MostVRAM
    };

    struct DeviceCreationSettings {
#ifdef _DEBUG
        bool enableDebugLayer = true;
        bool enableGPUValidation = true;
#else
        bool enableDebugLayer = false;
        bool enableGPUValidation = false;
#endif
        bool logAdapterDetails = true;
        D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
        SelectionStrategy selectionStrategy = SelectionStrategy::HighPerformance;
        std::optional<UINT> preferredVendorId; // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel, etc.
    };

    struct AdapterPersistentInfo {
        LUID adapterLuid{0, 0};
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        std::wstring description;
    };

    struct DeviceCapabilities {
        bool rayTracingSupported = false;
        bool meshShadersSupported = false;
        UINT nodeCount = 1;
    };

    DX12EngineDevice() = default;
    ~DX12EngineDevice()
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
#if defined(_DEBUG)
        if (m_device) {
            ComPtr<ID3D12DebugDevice> debugDevice;
            if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&debugDevice)))) {
                debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            }
        }
#endif
        Cleanup();
    }

    DX12EngineDevice(DX12EngineDevice&& other) noexcept
    {
        std::lock_guard<std::mutex> lock(other.m_initMutex);
        MoveFrom(std::move(other));
    }

    DX12EngineDevice& operator=(DX12EngineDevice&& other) noexcept
    {
        if (this != &other) {
            std::scoped_lock lock(m_initMutex, other.m_initMutex);
            Cleanup();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    DX12EngineDevice(const DX12EngineDevice&) = delete;
    DX12EngineDevice& operator=(const DX12EngineDevice&) = delete;

    bool Initialize(const DeviceCreationSettings& settings = {})
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        if (!ValidateSettings(settings)) {
            return false;
        }
        m_settings = settings;

        auto totalStart = std::chrono::high_resolution_clock::now();

        CheckPowerState();

        auto phaseStart = std::chrono::high_resolution_clock::now();
        if (!EnableDebugLayer()) return false;
        m_timing.debugLayerTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - phaseStart);

        phaseStart = std::chrono::high_resolution_clock::now();
        if (!CreateFactory()) return false;
        m_timing.factoryCreationTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - phaseStart);

        phaseStart = std::chrono::high_resolution_clock::now();
        if (!SelectAdapter()) return false;
        m_timing.adapterSelectionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - phaseStart);

        phaseStart = std::chrono::high_resolution_clock::now();
        if (!CreateDevice()) return false;
        m_timing.deviceCreationTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - phaseStart);

        m_timing.totalTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - totalStart);

        std::cout << "Engine Hardware Interface Initialized Successfully.\n";

        if (m_settings.logAdapterDetails) {
            std::cout << "Timing (μs) | DebugLayer: " << m_timing.debugLayerTime.count()
                      << " | Factory: " << m_timing.factoryCreationTime.count()
                      << " | Adapter: " << m_timing.adapterSelectionTime.count()
                      << " | Device: " << m_timing.deviceCreationTime.count()
                      << " | Total: " << m_timing.totalTime.count() << "\n";
        }
        return true;
    }

    ID3D12Device* GetDevice() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        return m_device.Get();
    }
    IDXGIAdapter1* GetAdapter() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        return m_hardwareAdapter.Get();
    }
    D3D_FEATURE_LEVEL GetFeatureLevel() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        return m_selectedFeatureLevel;
    }
    std::wstring GetAdapterName() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        if (!m_hardwareAdapter) return L"";
        DXGI_ADAPTER_DESC1 desc{};
        m_hardwareAdapter->GetDesc1(&desc);
        return desc.Description;
    }
    bool IsRaytracingSupported() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        return m_caps.rayTracingSupported;
    }
    DeviceCapabilities GetCapabilities() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        return m_caps;
    }

    std::optional<AdapterPersistentInfo> GetCurrentAdapterInfo() const
    {
        std::lock_guard<std::mutex> lock(m_initMutex);
        if (!m_hardwareAdapter) return std::nullopt;
        DXGI_ADAPTER_DESC1 desc{};
        m_hardwareAdapter->GetDesc1(&desc);
        AdapterPersistentInfo info{};
        info.adapterLuid = desc.AdapterLuid;
        info.featureLevel = m_selectedFeatureLevel;
        info.description = desc.Description;
        return info;
    }

    bool TrySelectPersistentAdapter(const AdapterPersistentInfo& info)
    {
        if (!m_factory) return false;

        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(m_factory->EnumAdapterByLuid(info.adapterLuid, IID_PPV_ARGS(&adapter)))) {
            return false;
        }

        if (ProbeFeatureLevel(adapter.Get()) >= info.featureLevel) {
            m_hardwareAdapter = adapter;
            m_selectedFeatureLevel = info.featureLevel;
            return true;
        }
        return false;
    }

private:
    struct AdapterInfo {
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc{};
        D3D_FEATURE_LEVEL maxFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    };

    struct InitializationTiming {
        std::chrono::microseconds debugLayerTime{0};
        std::chrono::microseconds factoryCreationTime{0};
        std::chrono::microseconds adapterSelectionTime{0};
        std::chrono::microseconds deviceCreationTime{0};
        std::chrono::microseconds totalTime{0};
    };

    DeviceCreationSettings m_settings{};
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<IDXGIAdapter1> m_hardwareAdapter;
    D3D_FEATURE_LEVEL m_selectedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    DeviceCapabilities m_caps{};
    InitializationTiming m_timing{};
    mutable std::mutex m_initMutex;

    bool ValidateSettings(const DeviceCreationSettings& settings)
    {
        if (settings.selectionStrategy == SelectionStrategy::SpecificVendor &&
            !settings.preferredVendorId) {
            std::cerr << "SpecificVendor strategy selected but no vendor ID provided.\n";
            return false;
        }

        if (settings.minFeatureLevel < D3D_FEATURE_LEVEL_11_0) {
            std::cerr << "Minimum feature level must be at least 11_0.\n";
            return false;
        }

        return true;
    }

    void LogAdapterDetails(const DXGI_ADAPTER_DESC1& desc) const
    {
        if (!m_settings.logAdapterDetails) return;

        std::wcout << L"\n=== GPU Details ===\n";
        std::wcout << L"Name: " << desc.Description << L"\n";
        std::wcout << L"Vendor ID: 0x" << std::hex << desc.VendorId << std::dec << L"\n";
        std::wcout << L"Device ID: 0x" << std::hex << desc.DeviceId << std::dec << L"\n";
        std::wcout << L"Dedicated VRAM: " << (desc.DedicatedVideoMemory >> 20) << L" MB\n";
        std::wcout << L"Dedicated System Memory: " << (desc.DedicatedSystemMemory >> 20) << L" MB\n";
        std::wcout << L"Shared System Memory: " << (desc.SharedSystemMemory >> 20) << L" MB\n";
    }

    bool EnableDebugLayer()
    {
#if defined(_DEBUG)
        if (m_settings.enableDebugLayer) {
            ComPtr<ID3D12Debug1> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
                if (m_settings.enableGPUValidation) {
                    debugController->SetEnableGPUBasedValidation(TRUE);
                    std::cout << "DX12 Debug Layer & GPU Validation Enabled.\n";
                } else {
                    std::cout << "DX12 Debug Layer Enabled (GPU validation off).\n";
                }
            } else {
                std::cout << "Debug layer unavailable; continuing without it.\n";
            }
        }
#endif
        return true; // Non-fatal if unavailable in release builds.
    }

    bool CreateFactory()
    {
        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        if (m_settings.enableDebugLayer) {
            dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        return SUCCEEDED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory)));
    }

    bool SelectAdapter()
    {
        ComPtr<IDXGIAdapter1> adapter;
        std::vector<AdapterInfo> suitableAdapters;

        DXGI_GPU_PREFERENCE preference = m_settings.gpuPreference;
        for (UINT adapterIndex = 0;
             m_factory->EnumAdapterByGpuPreference(
                 adapterIndex,
                 preference,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++adapterIndex) {

            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue; // skip software adapters
            }

            AdapterInfo info{};
            info.adapter = adapter;
            info.desc = desc;
            info.maxFeatureLevel = ProbeFeatureLevel(adapter.Get());

            // Respect minimum feature requirement.
            if (info.maxFeatureLevel < m_settings.minFeatureLevel) {
                continue;
            }

            suitableAdapters.push_back(std::move(info));
        }

        if (suitableAdapters.empty()) {
            std::cerr << "No suitable hardware adapter found.\n";
            return false;
        }

        // Selection strategies.
        auto matchesVendor = [this](const AdapterInfo& info) {
            return m_settings.preferredVendorId && info.desc.VendorId == *m_settings.preferredVendorId;
        };

        AdapterInfo* chosen = nullptr;
        switch (m_settings.selectionStrategy) {
        case SelectionStrategy::SpecificVendor:
            if (auto it = std::find_if(suitableAdapters.begin(), suitableAdapters.end(), matchesVendor);
                it != suitableAdapters.end()) {
                chosen = &(*it);
            }
            break;
        case SelectionStrategy::MostVRAM:
            chosen = &*std::max_element(
                suitableAdapters.begin(), suitableAdapters.end(),
                [](const AdapterInfo& a, const AdapterInfo& b) {
                    return a.desc.DedicatedVideoMemory < b.desc.DedicatedVideoMemory;
                });
            break;
        case SelectionStrategy::MinimumPower:
            // Pick the first one enumerated (typically iGPU when preference set accordingly).
            chosen = &suitableAdapters.front();
            break;
        case SelectionStrategy::HighPerformance:
        default:
            // Enumeration order already biased by DXGI_GPU_PREFERENCE; take first.
            chosen = &suitableAdapters.front();
            break;
        }

        if (!chosen) {
            std::cerr << "Adapter selection strategy did not yield a choice.\n";
            return false;
        }

        m_hardwareAdapter = chosen->adapter;
        m_selectedFeatureLevel = chosen->maxFeatureLevel;
        LogAdapterDetails(chosen->desc);
        std::wcout << L"Selected GPU: " << chosen->desc.Description << L"\n";
        return true;
    }

    bool CreateDevice()
    {
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        HRESULT hr = E_FAIL;
        for (auto level : levels) {
            if (level < m_settings.minFeatureLevel || level > m_selectedFeatureLevel) {
                continue;
            }

            hr = D3D12CreateDevice(m_hardwareAdapter.Get(), level, IID_PPV_ARGS(&m_device));
            if (SUCCEEDED(hr)) {
                m_selectedFeatureLevel = level;
                std::cout << "Device created with Feature Level: 0x" << std::hex << level << std::dec << "\n";
                break;
            } else {
                std::cerr << "D3D12CreateDevice failed at level 0x" << std::hex << level << std::dec
                          << " (" << HResultToString(hr) << ")\n";
            }
        }

        if (!m_device) {
            std::cerr << "Failed to create a D3D12 device.\n";
            return false;
        }

        QueryDeviceCapabilities();
        std::cout << "Ray Tracing Support: " << (m_caps.rayTracingSupported ? "Yes" : "No") << "\n";
        return true;
    }

    void QueryDeviceCapabilities()
    {
        if (!m_device) return;

        // Ray tracing
        m_caps.rayTracingSupported = CheckRaytracingSupport(m_device.Get());

        // Mesh shaders
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
            m_caps.meshShadersSupported = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        }

        m_caps.nodeCount = m_device->GetNodeCount();
    }

    D3D_FEATURE_LEVEL ProbeFeatureLevel(IDXGIAdapter1* adapter) const
    {
        const D3D_FEATURE_LEVEL probeLevels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        for (auto level : probeLevels) {
            if (SUCCEEDED(D3D12CreateDevice(adapter, level, _uuidof(ID3D12Device), nullptr))) {
                return level;
            }
        }
        return D3D_FEATURE_LEVEL_11_0;
    }

    void Cleanup()
    {
        m_device.Reset();
        m_hardwareAdapter.Reset();
        m_factory.Reset();
    }

    void MoveFrom(DX12EngineDevice&& other)
    {
        m_settings = other.m_settings;
        m_factory = std::move(other.m_factory);
        m_device = std::move(other.m_device);
        m_hardwareAdapter = std::move(other.m_hardwareAdapter);
        m_selectedFeatureLevel = other.m_selectedFeatureLevel;
        m_caps = other.m_caps;
        m_timing = other.m_timing;

        // Leave other in a safe, empty state.
        other.m_selectedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        other.m_caps = {};
        other.m_timing = {};
    }

    void CheckPowerState() const
    {
        SYSTEM_POWER_STATUS powerStatus{};
        if (GetSystemPowerStatus(&powerStatus)) {
            if (powerStatus.ACLineStatus == 0) {
                std::cout << "Running on battery - consider power-saving optimizations\n";
            }
        }
    }
};

static_assert(sizeof(DX12EngineDevice) % 8 == 0, "DX12EngineDevice should be 8-byte aligned for optimal performance");

int main()
{
    DX12EngineDevice::DeviceCreationSettings settings{};
    settings.selectionStrategy = DX12EngineDevice::SelectionStrategy::MostVRAM;
    settings.logAdapterDetails = true;

    // Example forcing vendor:
    // settings.preferredVendorId = 0x10DE;
    // settings.selectionStrategy = DX12EngineDevice::SelectionStrategy::SpecificVendor;

    DX12EngineDevice engine;
    if (engine.Initialize(settings)) {
        std::cout << "Device ready for rendering pipeline setup.\n";

        // Accessors for downstream setup
        ID3D12Device* device = engine.GetDevice();
        (void)device; // suppress unused warning for this sample

        // Next steps: command queue, swap chain, descriptor heaps, etc.
    }

    return 0;
}
