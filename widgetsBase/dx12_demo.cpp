#include "dock_framework.h"
#include "dock_layout.h"
#include "dx12_canvas.h"
#include "dx12_dock_widget.h"
#include "window_manager.h"
#include "dock_splitter.h"
#include "dock_theme.h"
#include "dock_renderer.h"
#include "icon_module.h"

#include <windows.h>
#include <windowsx.h>
#include <dbghelp.h>
#include <dwmapi.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <numeric>
#include <atomic>
#include <cmath>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("D3D12 call failed");
}

class DX12Demo;

namespace {
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;
constexpr bool kEnableTabUi = false;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

bool EnvEnabled(const char* name, bool defaultEnabled = false)
{
    const char* value = std::getenv(name);
    if (!value) {
        return defaultEnabled;
    }
    return value[0] != '0';
}

int EnvInt(const char* name, int defaultValue)
{
    const char* value = std::getenv(name);
    if (!value) return defaultValue;
    return std::atoi(value);
}

std::string EnvString(const char* name, const char* defaultValue)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return defaultValue ? std::string(defaultValue) : std::string();
    }
    return std::string(value);
}

double Percentile(std::vector<double> values, double pct)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = pct * (values.size() - 1);
    const size_t idx = static_cast<size_t>(rank);
    if (idx + 1 >= values.size()) return values.back();
    const double frac = rank - idx;
    return values[idx] * (1.0 - frac) + values[idx + 1] * frac;
}

template <typename T>
T SafeClamp(T value, T low, T high)
{
    if (high < low) {
        std::swap(low, high);
    }
    return std::clamp(value, low, high);
}

DFColor AdjustColor(const DFColor& c, float delta)
{
    return {
        SafeClamp(c.r + delta, 0.0f, 1.0f),
        SafeClamp(c.g + delta, 0.0f, 1.0f),
        SafeClamp(c.b + delta, 0.0f, 1.0f),
        c.a
    };
}

DFRect ComputeMainClientRect(const DFRect& viewRect, const df::DockTheme& theme)
{
    // Keep content close to the border while preserving a visible frame.
    const float pad = std::clamp(theme.clientAreaPadding * 0.5f, 1.0f, 3.0f);
    return {
        viewRect.x + pad,
        viewRect.y + pad,
        std::max(0.0f, viewRect.width - pad * 2.0f),
        std::max(0.0f, viewRect.height - pad * 2.0f)
    };
}

bool TryParseHexColor(std::string text, DFColor& out);
COLORREF ToColorRef(const DFColor& c);
COLORREF ContrastTextColor(const DFColor& bg);

bool IsRenderableDockWidget(const df::DockWidget* widget)
{
    if (!widget || widget->isFloating()) return false;
    const DFRect& b = widget->bounds();
    return b.width > 1.0f && b.height > 1.0f;
}

bool IsPointInTabDockCenterZone(const DFRect& bounds, const DFPoint& point)
{
    const float insetX = SafeClamp(bounds.width * 0.28f, 18.0f, 140.0f);
    const float insetY = SafeClamp(bounds.height * 0.28f, 14.0f, 120.0f);
    const DFRect center{
        bounds.x + insetX,
        bounds.y + insetY,
        std::max(0.0f, bounds.width - insetX * 2.0f),
        std::max(0.0f, bounds.height - insetY * 2.0f)
    };
    return center.width > 1.0f && center.height > 1.0f && center.contains(point);
}

struct TabVisual {
    df::DockLayout::Node* node = nullptr;
    DFRect strip{};
    std::vector<DFRect> tabRects{};
    std::vector<df::DockWidget*> widgets{};
};

struct TabInteractionHit {
    df::DockLayout::Node* node = nullptr;
    int tabIndex = -1;
    DFRect tabRect{};
    DFRect closeRect{};
    bool closeHit = false;
};

enum class ActionOwner {
    None,
    FloatingWindow,
    DockWidgetDrag,
    SplitterDrag,
    TabGesture
};

struct TabGestureState {
    bool active = false;
    bool undocked = false;
    df::DockLayout::Node* node = nullptr;
    int tabIndex = 0;
    DFRect strip{};
    DFPoint start{};
};

constexpr float kTabUndockDragThresholdPx = 10.0f;

const char* ActionOwnerName(ActionOwner action)
{
    switch (action) {
    case ActionOwner::FloatingWindow: return "floating";
    case ActionOwner::DockWidgetDrag: return "dock_drag";
    case ActionOwner::SplitterDrag: return "splitter";
    case ActionOwner::TabGesture: return "tab";
    case ActionOwner::None:
    default:
        return "none";
    }
}

void CollectTabVisuals(df::DockLayout::Node* node, std::vector<TabVisual>& out)
{
    if (!kEnableTabUi) {
        return;
    }
    if (!node) return;
    if (node->type == df::DockLayout::Node::Type::Tab && !node->children.empty()) {
        TabVisual visual;
        visual.node = node;
        visual.strip = df::DockLayout::TabStripRect(*node, node->bounds);
        for (size_t i = 0; i < node->children.size(); ++i) {
            visual.tabRects.push_back(df::DockLayout::TabRectForIndex(*node, node->bounds, i, node->children.size()));
            visual.widgets.push_back(node->children[i] ? node->children[i]->widget : nullptr);
        }
        out.push_back(visual);
    }
    for (auto& child : node->children) {
        CollectTabVisuals(child.get(), out);
    }
    CollectTabVisuals(node->first.get(), out);
    CollectTabVisuals(node->second.get(), out);
}

bool HandleTabInteraction(df::DockLayout::Node* node, const DFPoint& p, TabInteractionHit& outHit)
{
    if (!kEnableTabUi) {
        return false;
    }
    if (!node || !node->bounds.contains(p)) return false;

    if (node->type == df::DockLayout::Node::Type::Tab && !node->children.empty()) {
        const DFRect bar = df::DockLayout::TabStripRect(*node, node->bounds);
        if (bar.contains(p)) {
            for (size_t i = 0; i < node->children.size(); ++i) {
                const DFRect tabRect = df::DockLayout::TabRectForIndex(*node, node->bounds, i, node->children.size());
                if (tabRect.width <= 1.0f || tabRect.height <= 1.0f || !tabRect.contains(p)) {
                    continue;
                }
                outHit.node = node;
                outHit.tabIndex = static_cast<int>(i);
                outHit.tabRect = tabRect;
                outHit.closeRect = df::DockRenderer::tabCloseRect(tabRect);
                outHit.closeHit = outHit.closeRect.contains(p);
                return true;
            }
        }
    }

    if (node->type == df::DockLayout::Node::Type::Tab) {
        for (auto& child : node->children) {
            if (HandleTabInteraction(child.get(), p, outHit)) return true;
        }
    } else if (node->type == df::DockLayout::Node::Type::Split) {
        if (node->first && HandleTabInteraction(node->first.get(), p, outHit)) return true;
        if (node->second && HandleTabInteraction(node->second.get(), p, outHit)) return true;
    }

    return false;
}

bool HandleTabInteraction(df::DockLayout::Node* node, const DFPoint& p)
{
    if (!kEnableTabUi) {
        return false;
    }
    TabInteractionHit hit{};
    if (!HandleTabInteraction(node, p, hit)) {
        return false;
    }
    if (hit.closeHit) {
        if (hit.node && hit.tabIndex >= 0 &&
            hit.tabIndex < static_cast<int>(hit.node->children.size()) &&
            hit.node->children[hit.tabIndex] &&
            hit.node->children[hit.tabIndex]->widget) {
            df::DockManager::instance().closeWidget(hit.node->children[hit.tabIndex]->widget);
            return true;
        }
        return false;
    }
    if (hit.node) {
        hit.node->activeTab = hit.tabIndex;
        return true;
    }
    return false;
}

df::DockLayout::Node* FindWidgetNode(df::DockLayout::Node* node, df::DockWidget* widget)
{
    if (!node || !widget) return nullptr;
    if (node->type == df::DockLayout::Node::Type::Widget && node->widget == widget) {
        return node;
    }
    for (auto& child : node->children) {
        if (auto* found = FindWidgetNode(child.get(), widget)) {
            return found;
        }
    }
    if (auto* first = FindWidgetNode(node->first.get(), widget)) {
        return first;
    }
    return FindWidgetNode(node->second.get(), widget);
}

df::DockLayout::Node* FindParentTabOfWidget(df::DockLayout::Node* node, df::DockWidget* widget)
{
    if (!node || !widget) return nullptr;

    if (node->type == df::DockLayout::Node::Type::Tab) {
        for (auto& child : node->children) {
            if (child && child->type == df::DockLayout::Node::Type::Widget && child->widget == widget) {
                return node;
            }
        }
    }

    for (auto& child : node->children) {
        if (auto* found = FindParentTabOfWidget(child.get(), widget)) {
            return found;
        }
    }
    if (auto* found = FindParentTabOfWidget(node->first.get(), widget)) {
        return found;
    }
    return FindParentTabOfWidget(node->second.get(), widget);
}

std::string BuildTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u_%02u-%02u-%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::atomic<bool> gCrashCaptured{false};

bool ShouldCaptureException(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
        return true;
    default:
        return false;
    }
}

void WriteCrashArtifacts(EXCEPTION_POINTERS* info)
{
    if (gCrashCaptured.exchange(true)) {
        return;
    }

    const std::string stamp = BuildTimestamp();
    const std::string dumpName = "dx12_demo_crash_" + stamp + ".dmp";
    const std::string reportName = "crash_report.log";

    HANDLE dumpFile = CreateFileA(
        dumpName.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (dumpFile != INVALID_HANDLE_VALUE) {
        const auto dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = info;
        dumpInfo.ClientPointers = FALSE;
        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            dumpFile,
            dumpType,
            info ? &dumpInfo : nullptr,
            nullptr,
            nullptr);
        CloseHandle(dumpFile);
    }

    DWORD exceptionCode = 0;
    void* exceptionAddress = nullptr;
    if (info && info->ExceptionRecord) {
        exceptionCode = info->ExceptionRecord->ExceptionCode;
        exceptionAddress = info->ExceptionRecord->ExceptionAddress;
    }

    std::ofstream report(reportName, std::ios::out | std::ios::app);
    if (report.is_open()) {
        report << "[" << stamp << "] "
               << "exception=0x" << std::hex << exceptionCode << std::dec
               << " address=" << exceptionAddress
               << " dump=" << dumpName << "\n";
    }
}

void AppendRuntimeError(const char* scope, const std::string& message)
{
    std::ofstream report("crash_report.log", std::ios::out | std::ios::app);
    if (report.is_open()) {
        report << "[" << BuildTimestamp() << "] "
               << "runtime_error scope=" << (scope ? scope : "unknown")
               << " message=" << message << "\n";
    }
}

LONG CALLBACK CrashVectoredHandler(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord && ShouldCaptureException(info->ExceptionRecord->ExceptionCode)) {
        WriteCrashArtifacts(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
{
    WriteCrashArtifacts(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashReporter()
{
    AddVectoredExceptionHandler(1, CrashVectoredHandler);
    SetUnhandledExceptionFilter(CrashFilter);
}

class EventConsole {
public:
    struct Stats {
        int conflicts = 0;
        int handledNone = 0;
        int handledSplitter = 0;
        int handledDockDrag = 0;
        int handledFloatingWindow = 0;
        int handledTab = 0;
        int handledWidget = 0;
        int handledOther = 0;
    };

    EventConsole()
    {
        consoleEnabled_ = EnvEnabled("DF_EVENT_CONSOLE", true);
        verbose_ = EnvEnabled("DF_EVENT_VERBOSE", true);

        if (consoleEnabled_ && !GetConsoleWindow()) {
            if (AllocConsole()) {
                ownsConsole_ = true;
            }
        }

        if (consoleEnabled_) {
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
            freopen_s(&stream, "CONIN$", "r", stdin);
            SetConsoleTitleA("DX12 Event Console");
        }

        file_.open("event_conflicts.log", std::ios::out | std::ios::app);
        start_ = std::chrono::steady_clock::now();
        logLine("=== session start ===");
    }

    ~EventConsole()
    {
        logLine("=== session end ===");
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
        if (ownsConsole_) {
            FreeConsole();
        }
    }

    void logIncoming(const Event& e, bool dragging)
    {
        if (!verbose_) return;
        if (e.type == Event::Type::MouseMove && !dragging) return;
        logLine(buildPrefix(e) + "incoming");
    }

    void logConflict(const Event& e, bool hitFloating, bool hitSplitter, int widgetHits)
    {
        ++stats_.conflicts;
        std::ostringstream oss;
        oss << buildPrefix(e) << "conflict floating=" << (hitFloating ? "1" : "0")
            << " splitter=" << (hitSplitter ? "1" : "0")
            << " widgets=" << widgetHits;
        logLine(oss.str());
    }

    void logHandled(const Event& e, const std::string& handler)
    {
        if (handler == "none") {
            ++stats_.handledNone;
        } else if (handler == "splitter") {
            ++stats_.handledSplitter;
        } else if (handler == "dock_drag") {
            ++stats_.handledDockDrag;
        } else if (handler == "floating_window" || handler.rfind("floating_", 0) == 0) {
            ++stats_.handledFloatingWindow;
        } else if (handler.rfind("tab:", 0) == 0) {
            ++stats_.handledTab;
        } else if (handler.rfind("widget:", 0) == 0) {
            ++stats_.handledWidget;
        } else {
            ++stats_.handledOther;
        }
        if (!verbose_ && handler != "none") return;
        if (verbose_ && e.type == Event::Type::MouseMove && handler == "none") return;
        logLine(buildPrefix(e) + "handled_by=" + handler);
    }

    void logAutomation(const std::string& message)
    {
        logLine(std::string("[auto] ") + message);
    }

    const Stats& stats() const { return stats_; }

private:
    std::string buildPrefix(const Event& e)
    {
        using namespace std::chrono;
        const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start_).count();
        std::ostringstream oss;
        oss << "[" << std::setw(6) << elapsed << "ms] "
            << toString(e.type) << " (" << static_cast<int>(e.x) << ", " << static_cast<int>(e.y) << ") ";
        return oss.str();
    }

    static const char* toString(Event::Type type)
    {
        switch (type) {
        case Event::Type::MouseDown: return "MouseDown";
        case Event::Type::MouseDoubleClick: return "MouseDoubleClick";
        case Event::Type::MouseDrag: return "MouseDrag";
        case Event::Type::MouseUp: return "MouseUp";
        case Event::Type::MouseMove: return "MouseMove";
        case Event::Type::KeyDown: return "KeyDown";
        case Event::Type::KeyUp: return "KeyUp";
        case Event::Type::TextInput: return "TextInput";
        default: return "Other";
        }
    }

    void logLine(const std::string& line)
    {
        if (consoleEnabled_) {
            std::printf("%s\n", line.c_str());
        }
        if (file_.is_open()) {
            file_ << line << "\n";
            file_.flush();
        }
    }

    bool consoleEnabled_ = true;
    bool verbose_ = true;
    bool ownsConsole_ = false;
    std::ofstream file_;
    std::chrono::steady_clock::time_point start_{};
    Stats stats_{};
};

struct NativeFloatingHostCreateData {
    DX12Demo* demo = nullptr;
    df::DockWidget* widget = nullptr;
};

constexpr const wchar_t* kNativeFloatingHostClass = L"DFNativeFloatingHostWnd";
constexpr int kNativeHostTitleBarHeight = 13;
constexpr int kNativeHostCloseSize = 8;
constexpr int kNativeHostCloseMargin = 3;
constexpr int kNativeHostCloseCornerRadius = 3;

RECT NativeHostCloseRect(const RECT& clientRect)
{
    const int titleBottom = std::min<int>(static_cast<int>(clientRect.bottom), kNativeHostTitleBarHeight);
    const int y = std::max(0, (titleBottom - kNativeHostCloseSize) / 2);
    const int right = static_cast<int>(clientRect.right);
    const int left = std::max(0, right - kNativeHostCloseSize - kNativeHostCloseMargin);
    const int closeRight = std::max(0, right - kNativeHostCloseMargin);
    return RECT{
        left,
        y,
        closeRight,
        y + kNativeHostCloseSize
    };
}
}

class DX12Demo {
public:
    DX12Demo(HINSTANCE hInstance);
    ~DX12Demo();
    int run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK FloatingHostWndProc(HWND, UINT, WPARAM, LPARAM);
    static bool isEnvEnabled(const char* name);
    void initWindow(HINSTANCE);
    void initD3D12();
    void initDocking();
    void renderFrame();
    void waitForGPU();
    void handleResize(UINT width, UINT height);
    LRESULT handleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleKeyMessage(WPARAM wParam, LPARAM lParam);
    LRESULT handleCharMessage(WPARAM wParam, LPARAM lParam);
    void processEvent(Event& event);
    void dispatchMouseEvent(Event& event);
    bool handleShortcutKey(int key, bool ctrlDown, bool shiftDown);
    bool closeTabNode(df::DockLayout::Node* node, int tabIndex);
    df::DockLayout::Node* findTabNodeNearCursor() const;
    void updateHoverState(const DFPoint& point);
    bool handleActiveAction(Event& event);
    bool beginTabGesture(Event& event);
    bool handleTabGesture(Event& event);
    bool undockActiveTab(const DFPoint& mousePos);
    bool tryDockFloatingWindowAtPoint(df::WindowFrame* window, const DFPoint& mousePos);
    bool tryDockFloatingWindowByOverlap(df::WindowFrame* window, const DFPoint& dropPoint);
    df::DockWidget* pickDockTarget(const DFPoint& mousePos, df::DockWidget* movingWidget) const;
    df::DockWidget* pickDockTargetByOverlap(const DFRect& movingBounds, const DFPoint& dropPoint, df::DockWidget* movingWidget) const;
    bool dockFloatingWindowIntoTarget(df::WindowFrame* window, df::DockWidget* targetWidget);
    void renderDebugOverlay();
    void updateStatusCaption();
    void clearActiveAction();
    void refreshLayoutState();
    void syncClientOriginScreen();
    void syncNativeFloatingHosts();
    void createNativeFloatingHost(df::WindowFrame* frame);
    void destroyNativeFloatingHost(df::DockWidget* widget);
    void destroyAllNativeFloatingHosts();
    void onNativeFloatingHostMovedOrSized(df::DockWidget* widget, HWND hwnd);
    bool tryDockNativeFloatingHost(df::DockWidget* widget, HWND hwnd);
    bool tryDockFloatingWindowAtEdge(df::WindowFrame* window, const DFPoint& mousePos);
    void paintNativeFloatingHost(HWND hwnd);
    void applyWindowTitleBarStyle(HWND hwnd) const;
    bool runAutomatedEventChecks();
    bool injectEvent(Event::Type type, float x, float y, const char* expectedPrefix, const char* label);

    HWND hwnd_ = nullptr;
    bool running_ = true;
    bool automationMode_ = false;
    std::string lastDispatchHandler_;
    std::string themeName_ = "dark";

    // D3D12
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12Resource> renderTargets_[2];
    UINT rtvStride_ = 0;
    UINT frameIndex_ = 0;

    ComPtr<ID3D12CommandAllocator> commandAllocator_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;

    ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f, 1.0f};
    D3D12_RECT scissor_{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};

    // Docking
    std::unique_ptr<DX12Canvas> canvas_;
    df::DockLayout layout_;
    df::DockSplitter splitter_;
    std::vector<std::unique_ptr<df::DX12DockWidget>> widgets_;
    std::vector<TabVisual> tabVisuals_;
    df::WindowFrame* floatingWindow_ = nullptr;
    ActionOwner activeAction_ = ActionOwner::None;
    df::WindowFrame* activeWindow_ = nullptr;
    TabGestureState tabGesture_{};
    EventConsole eventConsole_;
    DFPoint lastMousePos_{};
    bool leftMouseDown_ = false;
    bool captureActive_ = false;
    bool resizing_ = false;
    bool resizeDebug_ = false;
    bool inSizeMove_ = false;
    bool hasPendingResize_ = false;
    UINT pendingResizeW_ = 0;
    UINT pendingResizeH_ = 0;
    bool liveResizeRenderInProgress_ = false;
    bool liveNativeMoveRenderInProgress_ = false;
    bool statusDirty_ = true;
    int captionFrameCountdown_ = 0;
    int hoveredTabVisual_ = -1;
    int hoveredTabSlot_ = -1;
    int hoveredCloseSlot_ = -1;
    df::DockWidget* hoveredDockWidget_ = nullptr;

    int renderFramesPostAutomation_ = 0;
    int eventSleepMs_ = 0;
    bool showWindowInAutomation_ = false;
    std::vector<double> eventDurationsMs_;
    std::vector<double> frameTimesMs_;

    struct NativeFloatingHost {
        HWND hwnd = nullptr;
        df::WindowFrame* frame = nullptr;
        bool syncing = false;
        bool dockDragActive = false;
    };
    std::unordered_map<df::DockWidget*, NativeFloatingHost> nativeFloatingHosts_;
    std::vector<df::DockWidget*> pendingNativeHostClose_;
    bool nativeFloatHostsEnabled_ = false;
    bool nativeFloatClassRegistered_ = false;
    bool showDebugOverlay_ = true;
};

// --- DX12Demo implementation ---
DX12Demo::DX12Demo(HINSTANCE hInstance)
{
    automationMode_ = isEnvEnabled("DF_AUTOMATE_EVENTS");
    eventSleepMs_ = EnvInt("DF_AUTOMATION_EVENT_SLEEP_MS", 0);
    renderFramesPostAutomation_ = EnvInt("DF_AUTOMATION_RENDER_FRAMES", 0);
    showWindowInAutomation_ = EnvEnabled("DF_EVENT_SHOW_WINDOW", false);
    resizeDebug_ = EnvEnabled("DF_RESIZE_DEBUG", false);
    nativeFloatHostsEnabled_ = EnvEnabled("DF_NATIVE_FLOAT_HOSTS", !automationMode_);
    showDebugOverlay_ = !automationMode_;
    themeName_ = EnvString("DF_THEME", "dark");
    df::SetThemeByName(themeName_);
    if (EnvEnabled("DF_FAST_VISUALS", false)) {
        df::DockTheme theme = df::CurrentTheme();
        df::ApplyFastVisualPreset(theme);
        df::SetTheme(theme);
    }
    const std::string titleBarColorHex = EnvString("DF_TITLE_BAR_COLOR", "");
    if (!titleBarColorHex.empty()) {
        DFColor parsed{};
        if (TryParseHexColor(titleBarColorHex, parsed)) {
            df::DockTheme theme = df::CurrentTheme();
            theme.titleBar = parsed;
            df::SetTheme(theme);
        } else {
            eventConsole_.logAutomation(
                std::string("invalid DF_TITLE_BAR_COLOR='") + titleBarColorHex + "', expected #RRGGBB");
        }
    }
    eventDurationsMs_.reserve(4096);
    frameTimesMs_.reserve(1024);
    initWindow(hInstance);
    initD3D12();
    initDocking();
}

DX12Demo::~DX12Demo()
{
    destroyAllNativeFloatingHosts();
    waitForGPU();
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

void DX12Demo::initWindow(HINSTANCE hInstance)
{
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX12DockDemoClass";
    RegisterClassEx(&wc);

    if (nativeFloatHostsEnabled_) {
        WNDCLASSEX fwc{ sizeof(WNDCLASSEX) };
        fwc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        fwc.lpfnWndProc = FloatingHostWndProc;
        fwc.hInstance = hInstance;
        fwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        fwc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        fwc.lpszClassName = kNativeFloatingHostClass;
        nativeFloatClassRegistered_ = RegisterClassEx(&fwc) != 0;
    }

    RECT rect{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowW(wc.lpszClassName, L"DX12 Docking Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, this);
    applyWindowTitleBarStyle(hwnd_);
    const bool showWindow = !automationMode_ || showWindowInAutomation_;
    ShowWindow(hwnd_, showWindow ? SW_SHOWDEFAULT : SW_HIDE);
    syncClientOriginScreen();
}

void DX12Demo::initD3D12()
{
    UINT dxgiFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_))))
            break;
    }

    D3D12_COMMAND_QUEUE_DESC qdesc{};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device_->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&commandQueue_)));

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.BufferCount = 2;
    scDesc.Width = WINDOW_WIDTH;
    scDesc.Height = WINDOW_HEIGHT;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue_.Get(), hwnd_, &scDesc, nullptr, nullptr, &swapChain1));
    ThrowIfFailed(swapChain1.As(&swapChain_));
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = 2;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)));
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create render targets
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i) {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])));
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, handle);
        handle.ptr += rtvStride_;
    }

    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_)));
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_)));
    commandList_->Close();

    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    fenceValue_ = 1;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DX12Demo::initDocking()
{
    canvas_ = std::make_unique<DX12Canvas>(device_.Get(), commandList_.Get(), (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);

    auto addWidget = [&](const char* title) {
        auto w = std::make_unique<df::DX12DockWidget>(title);
        auto raw = w.get();
        df::DockManager::instance().registerWidget(raw);
        widgets_.push_back(std::move(w));
        return raw;
    };

    auto* hierarchy = addWidget("Hierarchy");
    auto* viewport = addWidget("Viewport");
    auto* inspector = addWidget("Inspector");
    auto* console = addWidget("Console");
    auto* profiler = addWidget("Profiler");
    auto* assets = addWidget("Assets");
    auto* timeline = addWidget("Timeline");

    // Content-driven minima improve splitter/tab behavior under deep nesting.
    hierarchy->setMinimumSize(250.0f, 220.0f);
    viewport->setMinimumSize(520.0f, 320.0f);
    inspector->setMinimumSize(290.0f, 220.0f);
    console->setMinimumSize(280.0f, 200.0f);
    profiler->setMinimumSize(300.0f, 220.0f);
    assets->setMinimumSize(280.0f, 220.0f);
    timeline->setMinimumSize(300.0f, 190.0f);

    auto root = std::make_unique<df::DockLayout::Node>();
    root->type = df::DockLayout::Node::Type::Split;
    root->vertical = true;
    root->ratio = 0.22f;
    root->splitSizing = df::DockLayout::Node::SplitSizing::FixedFirst;
    root->fixedSize = 280.0f;
    root->minFirstSize = 220.0f;
    root->minSecondSize = 360.0f;
    root->first = std::make_unique<df::DockLayout::Node>();
    root->first->type = df::DockLayout::Node::Type::Widget;
    root->first->widget = hierarchy;
    root->second = std::make_unique<df::DockLayout::Node>();
    root->second->type = df::DockLayout::Node::Type::Split;
    root->second->vertical = false;
    root->second->ratio = 0.70f;
    root->second->splitSizing = df::DockLayout::Node::SplitSizing::FixedSecond;
    root->second->fixedSize = 250.0f;
    root->second->minFirstSize = 220.0f;
    root->second->minSecondSize = 180.0f;
    root->second->first = std::make_unique<df::DockLayout::Node>();
    root->second->first->type = df::DockLayout::Node::Type::Widget;
    root->second->first->widget = viewport;
    root->second->second = std::make_unique<df::DockLayout::Node>();
    root->second->second->type = df::DockLayout::Node::Type::Tab;
    root->second->second->activeTab = 0;
    auto inspectorNode = std::make_unique<df::DockLayout::Node>();
    inspectorNode->type = df::DockLayout::Node::Type::Widget;
    inspectorNode->widget = inspector;
    auto consoleNode = std::make_unique<df::DockLayout::Node>();
    consoleNode->type = df::DockLayout::Node::Type::Widget;
    consoleNode->widget = console;
    auto timelineNode = std::make_unique<df::DockLayout::Node>();
    timelineNode->type = df::DockLayout::Node::Type::Widget;
    timelineNode->widget = timeline;
    auto assetsNode = std::make_unique<df::DockLayout::Node>();
    assetsNode->type = df::DockLayout::Node::Type::Widget;
    assetsNode->widget = assets;
    root->second->second->children.push_back(std::move(inspectorNode));
    root->second->second->children.push_back(std::move(consoleNode));
    root->second->second->children.push_back(std::move(timelineNode));
    root->second->second->children.push_back(std::move(assetsNode));

    layout_.setRoot(std::move(root));
    floatingWindow_ = df::WindowManager::instance().createFloatingWindow(
        profiler, {780.0f, 120.0f, 340.0f, 230.0f});

    if (!automationMode_) {
        auto* tools = addWidget("Tools");
        df::WindowManager::instance().createFloatingWindow(
            tools, {920.0f, 210.0f, 320.0f, 220.0f});
    }
    refreshLayoutState();
}

void DX12Demo::refreshLayoutState()
{
    const DFRect viewRect{0.0f, 0.0f, viewport_.Width, viewport_.Height};
    const auto& theme = df::CurrentTheme();
    const DFRect clientRect = ComputeMainClientRect(viewRect, theme);
    df::DockManager::instance().setMainLayout(&layout_, clientRect);
    layout_.update(clientRect);
    splitter_.updateSplitters(layout_.root(), clientRect);
    tabVisuals_.clear();
    CollectTabVisuals(layout_.root(), tabVisuals_);
    df::DockManager::instance().setDragBounds(clientRect);
    // Floating windows use virtual-desktop bounds (multi-monitor) in local client space.
    const DFPoint origin = df::WindowManager::instance().clientOriginScreen();
    const float vx = static_cast<float>(GetSystemMetrics(SM_XVIRTUALSCREEN)) - origin.x;
    const float vy = static_cast<float>(GetSystemMetrics(SM_YVIRTUALSCREEN)) - origin.y;
    const float vw = static_cast<float>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const float vh = static_cast<float>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const DFRect virtualWork{
        vw > 0.0f ? vx : viewRect.x,
        vh > 0.0f ? vy : viewRect.y,
        vw > 0.0f ? vw : viewRect.width,
        vh > 0.0f ? vh : viewRect.height
    };
    df::WindowManager::instance().setWorkArea(virtualWork);
    syncNativeFloatingHosts();
    statusDirty_ = true;
    captionFrameCountdown_ = 0;
}

void DX12Demo::syncClientOriginScreen()
{
    if (!hwnd_) {
        return;
    }
    POINT topLeft{0, 0};
    if (!ClientToScreen(hwnd_, &topLeft)) {
        return;
    }
    df::WindowManager::instance().setClientOriginScreen(
        DFPoint{static_cast<float>(topLeft.x), static_cast<float>(topLeft.y)});
}

void DX12Demo::createNativeFloatingHost(df::WindowFrame* frame)
{
    if (!nativeFloatHostsEnabled_ || !frame || !frame->content() || !nativeFloatClassRegistered_) {
        return;
    }
    df::DockWidget* widget = frame->content();
    if (!widget || nativeFloatingHosts_.find(widget) != nativeFloatingHosts_.end()) {
        return;
    }

    const DFRect gb = frame->globalBounds();
    auto* init = new NativeFloatingHostCreateData{this, widget};
    HWND host = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kNativeFloatingHostClass,
        std::wstring(widget->title().begin(), widget->title().end()).c_str(),
        WS_POPUP | WS_THICKFRAME | WS_VISIBLE,
        static_cast<int>(gb.x),
        static_cast<int>(gb.y),
        std::max(160, static_cast<int>(gb.width)),
        std::max(120, static_cast<int>(gb.height)),
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        init);
    if (!host) {
        delete init;
        return;
    }
    NativeFloatingHost entry;
    entry.hwnd = host;
    entry.frame = frame;
    entry.syncing = false;
    nativeFloatingHosts_[widget] = entry;
}

void DX12Demo::destroyNativeFloatingHost(df::DockWidget* widget)
{
    if (!widget) {
        return;
    }
    auto it = nativeFloatingHosts_.find(widget);
    if (it == nativeFloatingHosts_.end()) {
        return;
    }
    HWND hwnd = it->second.hwnd;
    nativeFloatingHosts_.erase(it);
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

void DX12Demo::destroyAllNativeFloatingHosts()
{
    std::vector<HWND> windows;
    windows.reserve(nativeFloatingHosts_.size());
    for (const auto& kv : nativeFloatingHosts_) {
        if (kv.second.hwnd && IsWindow(kv.second.hwnd)) {
            windows.push_back(kv.second.hwnd);
        }
    }
    nativeFloatingHosts_.clear();
    for (HWND hwnd : windows) {
        DestroyWindow(hwnd);
    }
}

void DX12Demo::onNativeFloatingHostMovedOrSized(df::DockWidget* widget, HWND hwnd)
{
    if (!widget || !hwnd) {
        return;
    }
    auto it = nativeFloatingHosts_.find(widget);
    if (it == nativeFloatingHosts_.end()) {
        return;
    }
    if (it->second.syncing || !it->second.frame) {
        return;
    }

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) {
        return;
    }
    const DFPoint origin = df::WindowManager::instance().clientOriginScreen();
    const DFRect local{
        static_cast<float>(wr.left) - origin.x,
        static_cast<float>(wr.top) - origin.y,
        static_cast<float>(wr.right - wr.left),
        static_cast<float>(wr.bottom - wr.top)
    };
    it->second.frame->setBounds(local);
    statusDirty_ = true;
}

void DX12Demo::applyWindowTitleBarStyle(HWND hwnd) const
{
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }

    const auto& theme = df::CurrentTheme();
    const COLORREF captionColor = ToColorRef(theme.titleBar);
    const COLORREF textColor = ContrastTextColor(theme.titleBar);
    const BOOL darkMode = TRUE;

    DwmSetWindowAttribute(
        hwnd,
        static_cast<DWMWINDOWATTRIBUTE>(DWMWA_USE_IMMERSIVE_DARK_MODE),
        &darkMode,
        sizeof(darkMode));
    DwmSetWindowAttribute(
        hwnd,
        static_cast<DWMWINDOWATTRIBUTE>(DWMWA_CAPTION_COLOR),
        &captionColor,
        sizeof(captionColor));
    DwmSetWindowAttribute(
        hwnd,
        static_cast<DWMWINDOWATTRIBUTE>(DWMWA_TEXT_COLOR),
        &textColor,
        sizeof(textColor));
}

void DX12Demo::paintNativeFloatingHost(HWND hwnd)
{
    if (!hwnd) {
        return;
    }
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc) {
        return;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const auto& theme = df::CurrentTheme();
    auto* hostWidget = reinterpret_cast<df::DockWidget*>(GetPropW(hwnd, L"DF_FLOAT_WIDGET"));
    const auto hostVisual = hostWidget ? hostWidget->visualOptions() : df::DockWidget::VisualOptions{};
    const bool drawClientArea = theme.drawClientArea && hostVisual.drawClientArea;
    const bool drawClientAreaBorder = theme.drawClientAreaBorder && hostVisual.drawClientAreaBorder;
    const bool drawRoundedClientArea = theme.drawRoundedClientArea && hostVisual.drawRoundedClientArea;
    const bool drawTitleIcons = theme.drawTitleBarIcons && hostVisual.drawTitleBarIcons;
    const COLORREF titleColor = ToColorRef(theme.titleBar);
    const COLORREF bodyColor = ToColorRef(theme.dockBackground);
    const COLORREF titleTextColor = ContrastTextColor(theme.titleBar);
    const DFColor closeBase = DFColorFromHex(0xF1F4FF);
    const DFColor closeHover = DFColorFromHex(0xFFFFFF);

    RECT titleRect = rc;
    titleRect.bottom = std::min<LONG>(rc.bottom, static_cast<LONG>(kNativeHostTitleBarHeight));

    HBRUSH titleBrush = CreateSolidBrush(titleColor);
    FillRect(hdc, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    RECT bodyRect = rc;
    bodyRect.top = titleRect.bottom;
    HBRUSH bodyBrush = CreateSolidBrush(bodyColor);
    FillRect(hdc, &bodyRect, bodyBrush);
    DeleteObject(bodyBrush);

    if (drawClientArea) {
        const int pad = std::max(0, static_cast<int>(std::lround(theme.clientAreaPadding)));
        RECT clientRect{
            rc.left + pad,
            bodyRect.top + pad,
            rc.right - pad,
            rc.bottom - pad
        };
        if (clientRect.right > clientRect.left + 2 && clientRect.bottom > clientRect.top + 2) {
            const int radius = drawRoundedClientArea
                ? std::max(0, static_cast<int>(std::lround(theme.clientAreaCornerRadius)))
                : 0;
            const int borderPx = std::max(1, static_cast<int>(std::lround(theme.clientAreaBorderThickness)));
            HBRUSH clientBrush = CreateSolidBrush(ToColorRef(theme.clientAreaFill));
            HPEN clientPen = drawClientAreaBorder
                ? CreatePen(PS_SOLID, borderPx, ToColorRef(theme.clientAreaBorder))
                : static_cast<HPEN>(GetStockObject(NULL_PEN));
            HGDIOBJ oldPenClient = SelectObject(hdc, clientPen);
            HGDIOBJ oldBrushClient = SelectObject(hdc, clientBrush);
            RoundRect(
                hdc,
                clientRect.left,
                clientRect.top,
                clientRect.right,
                clientRect.bottom,
                radius * 2,
                radius * 2);
            SelectObject(hdc, oldBrushClient);
            SelectObject(hdc, oldPenClient);
            DeleteObject(clientBrush);
            if (drawClientAreaBorder) {
                DeleteObject(clientPen);
            }
        }
    }

    std::wstring title(256, L'\0');
    const int written = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    if (written > 0) {
        title.resize(static_cast<size_t>(written));
    } else {
        title.clear();
        if (hostWidget) {
            const std::string fallback = hostWidget->title();
            title.assign(fallback.begin(), fallback.end());
        }
    }

    RECT closeRect = drawTitleIcons ? NativeHostCloseRect(rc) : RECT{rc.right, 0, rc.right, 0};
    POINT cursor{};
    bool closeHoverActive = false;
    if (drawTitleIcons && GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
        closeHoverActive = PtInRect(&closeRect, cursor) != 0;
    }

    if (closeHoverActive) {
        const COLORREF hoverBg = ToColorRef(AdjustColor(theme.titleBar, 0.10f));
        HBRUSH hoverBrush = CreateSolidBrush(hoverBg);
        HPEN noPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
        HGDIOBJ oldPen = SelectObject(hdc, noPen);
        HGDIOBJ oldBrush = SelectObject(hdc, hoverBrush);
        RoundRect(
            hdc,
            closeRect.left,
            closeRect.top,
            closeRect.right,
            closeRect.bottom,
            kNativeHostCloseCornerRadius,
            kNativeHostCloseCornerRadius);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hoverBrush);
    }

    RECT textRect{
        rc.left + 8,
        titleRect.top,
        std::max(rc.left + 8, closeRect.left - 8),
        titleRect.bottom
    };
    std::string titleText;
    titleText.reserve(title.size());
    for (wchar_t wc : title) {
        titleText.push_back((wc >= 0 && wc <= 127) ? static_cast<char>(wc) : '?');
    }
    const DFRect textRectF{
        static_cast<float>(textRect.left),
        static_cast<float>(textRect.top),
        static_cast<float>(std::max(0L, textRect.right - textRect.left)),
        static_cast<float>(std::max(0L, textRect.bottom - textRect.top))
    };
    const float titleFontScale = std::clamp(theme.tabFontScale, 0.3f, 2.0f);
    const std::string clippedTitle = DFClipTextToWidth(titleText, textRectF.width, true, titleFontScale);
    if (!clippedTitle.empty()) {
        const int savedDc = SaveDC(hdc);
        IntersectClipRect(hdc, textRect.left, textRect.top, textRect.right, textRect.bottom);
        HBRUSH textBrush = CreateSolidBrush(titleTextColor);
        // Keep native host text crisp: avoid rounded pixel smoothing in GDI.
        const bool smoothText = false;
        HPEN noPen = nullptr;
        HGDIOBJ oldPen = nullptr;
        if (smoothText) {
            noPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
            oldPen = SelectObject(hdc, noPen);
        }
        const float textX = std::floor(textRectF.x);
        const float textY = std::floor(DFTextBaselineYForRect(textRectF, titleFontScale));
        DFDrawBitmapTextPixels(
            textX,
            textY,
            clippedTitle,
            [&](float px, float py, float w, float h) {
                if (smoothText) {
                    const int left = static_cast<int>(std::floor(px));
                    const int top = static_cast<int>(std::floor(py));
                    const int right = static_cast<int>(std::ceil(px + w));
                    const int bottom = static_cast<int>(std::ceil(py + h));
                    RECT pixelRect{
                        static_cast<LONG>(left),
                        static_cast<LONG>(top),
                        static_cast<LONG>(right),
                        static_cast<LONG>(bottom)
                    };
                    // Very small glyph cells (<=1px after rasterization) can
                    // disappear with RoundRect in GDI. Fall back to solid fill.
                    if ((right - left) < 2 || (bottom - top) < 2) {
                        FillRect(hdc, &pixelRect, textBrush);
                    } else {
                        const int radius = std::max(1, static_cast<int>(std::round(w * 0.35f)));
                        HGDIOBJ oldBrush = SelectObject(hdc, textBrush);
                        RoundRect(hdc, left, top, right, bottom, radius, radius);
                        SelectObject(hdc, oldBrush);
                    }
                    return;
                }
                RECT pixelRect{
                    static_cast<LONG>(std::floor(px)),
                    static_cast<LONG>(std::floor(py)),
                    static_cast<LONG>(std::ceil(px + w)),
                    static_cast<LONG>(std::ceil(py + h))
                };
                FillRect(hdc, &pixelRect, textBrush);
            },
            titleFontScale);
        if (smoothText) {
            SelectObject(hdc, oldPen);
        }
        DeleteObject(textBrush);
        RestoreDC(hdc, savedDc);
    }

    if (drawTitleIcons) {
        const DFColor iconColor = closeHoverActive ? closeHover : closeBase;
        const COLORREF iconRef = ToColorRef(iconColor);
        const int closeW = std::max(1, static_cast<int>(closeRect.right - closeRect.left));
        const int closeH = std::max(1, static_cast<int>(closeRect.bottom - closeRect.top));
        const int iconMin = std::min(closeW, closeH);
        const int iconPadding = (iconMin <= 9) ? 1 : 2;
        const int stroke = (iconMin <= 11) ? 2 : 1;
        const int ix0 = static_cast<int>(closeRect.left) + iconPadding;
        const int iy0 = static_cast<int>(closeRect.top) + iconPadding;
        const int ix1 = std::max(ix0, static_cast<int>(closeRect.right) - iconPadding - 1);
        const int iy1 = std::max(iy0, static_cast<int>(closeRect.bottom) - iconPadding - 1);
        HBRUSH iconBrush = CreateSolidBrush(iconRef);
        auto drawPixel = [&](int x, int y) {
            const int half = stroke / 2;
            RECT r{
                x - half,
                y - half,
                x - half + stroke,
                y - half + stroke
            };
            FillRect(hdc, &r, iconBrush);
        };
        auto drawDiag = [&](int x0, int y0, int x1, int y1) {
            const int dx = x1 - x0;
            const int dy = y1 - y0;
            const int steps = std::max(std::abs(dx), std::abs(dy));
            if (steps <= 0) {
                drawPixel(x0, y0);
                return;
            }
            for (int i = 0; i <= steps; ++i) {
                const int x = x0 + (dx * i + (dx >= 0 ? steps / 2 : -steps / 2)) / steps;
                const int y = y0 + (dy * i + (dy >= 0 ? steps / 2 : -steps / 2)) / steps;
                drawPixel(x, y);
            }
        };
        drawDiag(ix0, iy0, ix1, iy1);
        drawDiag(ix0, iy1, ix1, iy0);
        DeleteObject(iconBrush);
    }

    EndPaint(hwnd, &ps);
}

namespace {

bool TryParseHexColor(std::string text, DFColor& out)
{
    if (text.empty()) {
        return false;
    }
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), text.end());
    if (!text.empty() && text[0] == '#') {
        text.erase(text.begin());
    }
    if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
        text = text.substr(2);
    }
    if (text.size() != 6) {
        return false;
    }

    char* end = nullptr;
    const unsigned long rgb = std::strtoul(text.c_str(), &end, 16);
    if (!end || *end != '\0' || rgb > 0xFFFFFFul) {
        return false;
    }

    out = DFColorFromHex(static_cast<uint32_t>(rgb));
    return true;
}

COLORREF ToColorRef(const DFColor& c)
{
    const auto toByte = [](float v) -> BYTE {
        return static_cast<BYTE>(SafeClamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const BYTE r = toByte(c.r);
    const BYTE g = toByte(c.g);
    const BYTE b = toByte(c.b);
    return RGB(r, g, b);
}

COLORREF ContrastTextColor(const DFColor& bg)
{
    const float luminance = bg.r * 0.2126f + bg.g * 0.7152f + bg.b * 0.0722f;
    return (luminance < 0.45f) ? RGB(240, 240, 240) : RGB(30, 30, 30);
}

} // namespace

void DX12Demo::syncNativeFloatingHosts()
{
    if (!nativeFloatHostsEnabled_) {
        return;
    }

    if (!pendingNativeHostClose_.empty()) {
        const auto pending = pendingNativeHostClose_;
        pendingNativeHostClose_.clear();
        for (df::DockWidget* widget : pending) {
            if (!widget) continue;
            df::DockManager::instance().closeWidget(widget);
            if (floatingWindow_ && floatingWindow_->content() == widget) {
                floatingWindow_ = nullptr;
            }
        }
        refreshLayoutState();
    }

    auto windows = df::WindowManager::instance().windowsSnapshot();
    std::unordered_map<df::DockWidget*, df::WindowFrame*> floatingNow;
    for (auto* frame : windows) {
        if (!frame || !frame->content()) {
            continue;
        }
        if (!frame->content()->isFloating()) {
            continue;
        }
        floatingNow[frame->content()] = frame;
        createNativeFloatingHost(frame);
    }

    for (auto it = nativeFloatingHosts_.begin(); it != nativeFloatingHosts_.end();) {
        if (floatingNow.find(it->first) == floatingNow.end()) {
            HWND hwnd = it->second.hwnd;
            it = nativeFloatingHosts_.erase(it);
            if (hwnd && IsWindow(hwnd)) {
                DestroyWindow(hwnd);
            }
            continue;
        }
        ++it;
    }

    for (auto& kv : nativeFloatingHosts_) {
        df::DockWidget* widget = kv.first;
        auto found = floatingNow.find(widget);
        if (found == floatingNow.end()) {
            continue;
        }
        kv.second.frame = found->second;
        const DFRect gb = kv.second.frame->globalBounds();
        HWND hwnd = kv.second.hwnd;
        if (!hwnd || !IsWindow(hwnd)) {
            continue;
        }
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        const int x = static_cast<int>(gb.x);
        const int y = static_cast<int>(gb.y);
        const int w = std::max(160, static_cast<int>(gb.width));
        const int h = std::max(120, static_cast<int>(gb.height));
        if (std::abs(wr.left - x) > 1 || std::abs(wr.top - y) > 1 ||
            std::abs((wr.right - wr.left) - w) > 1 || std::abs((wr.bottom - wr.top) - h) > 1) {
            kv.second.syncing = true;
            SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            kv.second.syncing = false;
        }
        if (widget) {
            std::wstring title(widget->title().begin(), widget->title().end());
            SetWindowTextW(hwnd, title.c_str());
        }
        applyWindowTitleBarStyle(hwnd);
    }
}

void DX12Demo::clearActiveAction()
{
    df::DockManager::instance().endDrag();
    df::DockManager::instance().cancelFloatingDrag();
    splitter_.endDrag();
    df::WindowManager::instance().cancelAllDrags();
    activeAction_ = ActionOwner::None;
    activeWindow_ = nullptr;
    tabGesture_ = TabGestureState{};
    statusDirty_ = true;
}

void DX12Demo::updateHoverState(const DFPoint& point)
{
    hoveredTabVisual_ = -1;
    hoveredTabSlot_ = -1;
    hoveredCloseSlot_ = -1;
    hoveredDockWidget_ = nullptr;

    for (size_t i = 0; i < tabVisuals_.size(); ++i) {
        const auto& visual = tabVisuals_[i];
        for (size_t tabIndex = 0; tabIndex < visual.tabRects.size(); ++tabIndex) {
            if (!visual.tabRects[tabIndex].contains(point)) {
                continue;
            }
            hoveredTabVisual_ = static_cast<int>(i);
            hoveredTabSlot_ = static_cast<int>(tabIndex);
            if (df::DockRenderer::tabCloseRect(visual.tabRects[tabIndex]).contains(point)) {
                hoveredCloseSlot_ = static_cast<int>(tabIndex);
            }
            return;
        }
    }

    for (auto& widget : widgets_) {
        if (!IsRenderableDockWidget(widget.get())) {
            continue;
        }
        if (widget->bounds().contains(point)) {
            hoveredDockWidget_ = widget.get();
            return;
        }
    }
}

df::DockLayout::Node* DX12Demo::findTabNodeNearCursor() const
{
    for (const auto& visual : tabVisuals_) {
        if (!visual.node) continue;
        if (visual.strip.contains(lastMousePos_) || visual.node->bounds.contains(lastMousePos_)) {
            return visual.node;
        }
    }
    return tabVisuals_.empty() ? nullptr : tabVisuals_.front().node;
}

bool DX12Demo::closeTabNode(df::DockLayout::Node* node, int tabIndex)
{
    if (!node || node->type != df::DockLayout::Node::Type::Tab || node->children.empty()) {
        return false;
    }
    if (tabIndex < 0 || tabIndex >= static_cast<int>(node->children.size())) {
        return false;
    }

    df::DockWidget* closingWidget = node->children[tabIndex] ? node->children[tabIndex]->widget : nullptr;
    if (!closingWidget) {
        return false;
    }

    node->children.erase(node->children.begin() + tabIndex);
    if (node->children.empty()) {
        node->type = df::DockLayout::Node::Type::Widget;
        node->widget = nullptr;
        node->activeTab = 0;
    } else if (node->children.size() == 1) {
        // Collapse single-tab containers back to a plain widget node.
        std::unique_ptr<df::DockLayout::Node> survivor = std::move(node->children.front());
        node->children.clear();
        if (survivor && survivor->type == df::DockLayout::Node::Type::Widget) {
            node->type = df::DockLayout::Node::Type::Widget;
            node->widget = survivor->widget;
            node->activeTab = 0;
        } else if (survivor) {
            *node = std::move(*survivor);
        }
    } else {
        node->activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
    }

    // Keep a closed widget out of hit-testing/rendering until reopened.
    closingWidget->setBounds({0.0f, 0.0f, 0.0f, 0.0f});

    refreshLayoutState();
    statusDirty_ = true;
    return true;
}

bool DX12Demo::handleShortcutKey(int key, bool ctrlDown, bool shiftDown)
{
    if (key == VK_F1) {
        showDebugOverlay_ = !showDebugOverlay_;
        statusDirty_ = true;
        captionFrameCountdown_ = 0;
        lastDispatchHandler_ = "key:toggle_overlay";
        eventConsole_.logAutomation(std::string("shortcut F1 -> debug overlay ") + (showDebugOverlay_ ? "on" : "off"));
        return true;
    }

    if (key == VK_ESCAPE) {
        clearActiveAction();
        if (captureActive_) {
            ReleaseCapture();
            captureActive_ = false;
        }
        leftMouseDown_ = false;
        lastDispatchHandler_ = "key:cancel";
        eventConsole_.logAutomation("shortcut ESC -> cancel action");
        return true;
    }

    if (kEnableTabUi && ctrlDown && key == VK_TAB) {
        refreshLayoutState();
        auto* node = findTabNodeNearCursor();
        if (node && node->type == df::DockLayout::Node::Type::Tab && !node->children.empty()) {
            const int step = shiftDown ? -1 : 1;
            const int count = static_cast<int>(node->children.size());
            node->activeTab = (node->activeTab + step + count) % count;
            refreshLayoutState();
            lastDispatchHandler_ = "key:tab_cycle";
            eventConsole_.logAutomation("shortcut Ctrl+Tab -> cycle tab");
            return true;
        }
        return false;
    }

    if (ctrlDown && key == 'W') {
        refreshLayoutState();
        if (kEnableTabUi) {
            auto* node = findTabNodeNearCursor();
            if (closeTabNode(node, node ? node->activeTab : 0)) {
                lastDispatchHandler_ = "key:tab_close";
                eventConsole_.logAutomation("shortcut Ctrl+W -> close tab");
                return true;
            }
        }
        if (floatingWindow_) {
            df::WindowManager::instance().destroyWindow(floatingWindow_);
            floatingWindow_ = nullptr;
            clearActiveAction();
            refreshLayoutState();
            lastDispatchHandler_ = "key:window_close";
            eventConsole_.logAutomation("shortcut Ctrl+W -> close floating window");
            return true;
        }
    }

    return false;
}

LRESULT DX12Demo::handleKeyMessage(WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    Event event(Event::Type::KeyDown);
    event.key = static_cast<int>(wParam);
    event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    event.handled = handleShortcutKey(event.key, event.ctrl, event.shift);
    if (!event.handled) {
        processEvent(event);
    }
    statusDirty_ = true;
    updateStatusCaption();
    return event.handled ? 0 : DefWindowProc(hwnd_, WM_KEYDOWN, wParam, lParam);
}

LRESULT DX12Demo::handleCharMessage(WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    Event event(Event::Type::TextInput);
    const wchar_t ch = static_cast<wchar_t>(wParam);

    char utf8Buffer[8]{};
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        &ch,
        1,
        utf8Buffer,
        static_cast<int>(sizeof(utf8Buffer)),
        nullptr,
        nullptr);
    if (written > 0) {
        event.textUtf8.assign(utf8Buffer, utf8Buffer + written);
    }

    event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (!event.textUtf8.empty()) {
        processEvent(event);
    }

    statusDirty_ = true;
    updateStatusCaption();
    return event.handled ? 0 : DefWindowProc(hwnd_, WM_CHAR, wParam, lParam);
}

df::DockWidget* DX12Demo::pickDockTarget(const DFPoint& mousePos, df::DockWidget* movingWidget) const
{
    df::DockWidget* best = nullptr;
    float bestArea = 0.0f;
    for (const auto& widget : widgets_) {
        if (!widget || widget.get() == movingWidget || widget->isFloating()) {
            continue;
        }
        const DFRect& b = widget->bounds();
        if (b.width <= 2.0f || b.height <= 2.0f || !b.contains(mousePos)) {
            continue;
        }
        const float area = b.width * b.height;
        if (!best || area < bestArea) {
            best = widget.get();
            bestArea = area;
        }
    }
    return best;
}

df::DockWidget* DX12Demo::pickDockTargetByOverlap(const DFRect& movingBounds, const DFPoint& dropPoint, df::DockWidget* movingWidget) const
{
    df::DockWidget* best = nullptr;
    float bestOverlap = 0.0f;
    for (const auto& widget : widgets_) {
        if (!widget || widget.get() == movingWidget || widget->isFloating()) {
            continue;
        }
        const DFRect& b = widget->bounds();
        if (b.width <= 2.0f || b.height <= 2.0f) {
            continue;
        }
        if (!IsPointInTabDockCenterZone(b, dropPoint)) {
            continue;
        }

        const float ix0 = std::max(movingBounds.x, b.x);
        const float iy0 = std::max(movingBounds.y, b.y);
        const float ix1 = std::min(movingBounds.x + movingBounds.width, b.x + b.width);
        const float iy1 = std::min(movingBounds.y + movingBounds.height, b.y + b.height);
        if (ix1 <= ix0 || iy1 <= iy0) {
            continue;
        }

        const float overlap = (ix1 - ix0) * (iy1 - iy0);
        if (!best || overlap > bestOverlap) {
            best = widget.get();
            bestOverlap = overlap;
        }
    }
    return best;
}

bool DX12Demo::dockFloatingWindowIntoTarget(df::WindowFrame* window, df::DockWidget* targetWidget)
{
    if (!window || !window->content() || !targetWidget) {
        return false;
    }

    df::DockWidget* movingWidget = window->content();

    if (auto* tabParent = FindParentTabOfWidget(layout_.root(), targetWidget)) {
        auto incoming = std::make_unique<df::DockLayout::Node>();
        incoming->type = df::DockLayout::Node::Type::Widget;
        incoming->widget = movingWidget;
        tabParent->children.push_back(std::move(incoming));
        tabParent->activeTab = static_cast<int>(tabParent->children.size()) - 1;

        if (floatingWindow_ == window) {
            floatingWindow_ = nullptr;
        }
        if (activeWindow_ == window) {
            activeWindow_ = nullptr;
        }
        df::WindowManager::instance().destroyWindow(window);
        refreshLayoutState();
        statusDirty_ = true;
        return true;
    }

    auto* targetNode = FindWidgetNode(layout_.root(), targetWidget);
    if (!targetNode || targetNode->type != df::DockLayout::Node::Type::Widget || targetNode->widget != targetWidget) {
        return false;
    }

    auto original = std::make_unique<df::DockLayout::Node>();
    original->type = df::DockLayout::Node::Type::Widget;
    original->widget = targetWidget;

    auto incoming = std::make_unique<df::DockLayout::Node>();
    incoming->type = df::DockLayout::Node::Type::Widget;
    incoming->widget = movingWidget;

    targetNode->type = df::DockLayout::Node::Type::Tab;
    targetNode->widget = nullptr;
    targetNode->first.reset();
    targetNode->second.reset();
    targetNode->children.clear();
    targetNode->children.push_back(std::move(original));
    targetNode->children.push_back(std::move(incoming));
    targetNode->activeTab = 1;

    if (floatingWindow_ == window) {
        floatingWindow_ = nullptr;
    }
    if (activeWindow_ == window) {
        activeWindow_ = nullptr;
    }
    df::WindowManager::instance().destroyWindow(window);
    refreshLayoutState();
    statusDirty_ = true;
    return true;
}

bool DX12Demo::tryDockFloatingWindowAtPoint(df::WindowFrame* window, const DFPoint& mousePos)
{
    if (!window || !window->content()) return false;

    df::DockWidget* movingWidget = window->content();
    df::DockWidget* targetWidget = pickDockTarget(mousePos, movingWidget);
    if (!targetWidget) {
        return false;
    }

    return dockFloatingWindowIntoTarget(window, targetWidget);
}

bool DX12Demo::tryDockFloatingWindowByOverlap(df::WindowFrame* window, const DFPoint& dropPoint)
{
    if (!window || !window->content()) {
        return false;
    }
    df::DockWidget* movingWidget = window->content();
    const DFRect movingBounds = window->bounds();
    df::DockWidget* targetWidget = pickDockTargetByOverlap(movingBounds, dropPoint, movingWidget);
    if (!targetWidget) {
        return false;
    }
    return dockFloatingWindowIntoTarget(window, targetWidget);
}

bool DX12Demo::tryDockFloatingWindowAtEdge(df::WindowFrame* window, const DFPoint& mousePos)
{
    if (!window || !window->content()) {
        return false;
    }
    const float edgeMargin = 12.0f;
    if (mousePos.x < -edgeMargin || mousePos.y < -edgeMargin ||
        mousePos.x > viewport_.Width + edgeMargin || mousePos.y > viewport_.Height + edgeMargin) {
        return false;
    }

    const float threshold = 12.0f;
    const float left = mousePos.x;
    const float right = viewport_.Width - mousePos.x;
    const float top = mousePos.y;
    const float bottom = viewport_.Height - mousePos.y;
    const float minDist = std::min(std::min(left, right), std::min(top, bottom));
    if (minDist > threshold) {
        return false;
    }

    enum class Edge { Left, Right, Top, Bottom };
    Edge edge = Edge::Left;
    float best = left;
    if (right < best) { best = right; edge = Edge::Right; }
    if (top < best) { best = top; edge = Edge::Top; }
    if (bottom < best) { edge = Edge::Bottom; }

    auto incoming = std::make_unique<df::DockLayout::Node>();
    incoming->type = df::DockLayout::Node::Type::Widget;
    incoming->widget = window->content();

    std::unique_ptr<df::DockLayout::Node> root = layout_.takeRoot();
    if (!root) {
        layout_.setRoot(std::move(incoming));
    } else {
        auto split = std::make_unique<df::DockLayout::Node>();
        split->type = df::DockLayout::Node::Type::Split;
        split->vertical = (edge == Edge::Left || edge == Edge::Right);
        split->ratio = 0.28f;
        split->minFirstSize = 120.0f;
        split->minSecondSize = 120.0f;
        if (edge == Edge::Left || edge == Edge::Top) {
            split->first = std::move(incoming);
            split->second = std::move(root);
        } else {
            split->first = std::move(root);
            split->second = std::move(incoming);
        }
        layout_.setRoot(std::move(split));
    }

    if (floatingWindow_ == window) {
        floatingWindow_ = nullptr;
    }
    if (activeWindow_ == window) {
        activeWindow_ = nullptr;
    }
    df::WindowManager::instance().destroyWindow(window);
    refreshLayoutState();
    statusDirty_ = true;
    return true;
}

bool DX12Demo::tryDockNativeFloatingHost(df::DockWidget* widget, HWND hwnd)
{
    if (!widget || !hwnd) {
        return false;
    }
    auto it = nativeFloatingHosts_.find(widget);
    if (it == nativeFloatingHosts_.end() || !it->second.frame) {
        return false;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return false;
    }
    POINT local = cursor;
    if (!ScreenToClient(hwnd_, &local)) {
        return false;
    }
    const DFPoint mousePos{static_cast<float>(local.x), static_cast<float>(local.y)};

    // Use the same DockManager drop-resolution path as in-client floating drags.
    // Dock only when a real drag session is active for this exact frame.
    if (it->second.frame) {
        auto& mgr = df::DockManager::instance();
        if (!mgr.isFloatingDragging() || mgr.floatingDragWindow() != it->second.frame) {
            return false;
        }
        mgr.updateFloatingDrag(mousePos);
        mgr.endFloatingDrag(mousePos);
        return !widget->isFloating();
    }

    return false;
}

void DX12Demo::renderDebugOverlay()
{
    const auto& theme = df::CurrentTheme();
    const float overlayW = 180.0f;
    const float overlayH = 44.0f;
    const DFRect panel{8.0f, 8.0f, overlayW, overlayH};
    canvas_->drawRectangle(panel, theme.overlayPanel);

    DFColor actionColor{0.35f, 0.35f, 0.35f, 1.0f};
    switch (activeAction_) {
    case ActionOwner::FloatingWindow: actionColor = {0.85f, 0.55f, 0.20f, 1.0f}; break;
    case ActionOwner::DockWidgetDrag: actionColor = {0.20f, 0.72f, 0.95f, 1.0f}; break;
    case ActionOwner::SplitterDrag: actionColor = {0.38f, 0.78f, 0.34f, 1.0f}; break;
    case ActionOwner::TabGesture: actionColor = {0.74f, 0.42f, 0.92f, 1.0f}; break;
    case ActionOwner::None:
    default:
        break;
    }

    canvas_->drawRectangle({panel.x + 8.0f, panel.y + 8.0f, 18.0f, 18.0f}, actionColor);
    canvas_->drawRectangle(
        {panel.x + 30.0f, panel.y + 8.0f, 18.0f, 18.0f},
        leftMouseDown_ ? DFColor{0.95f, 0.28f, 0.28f, 1.0f} : DFColor{0.35f, 0.35f, 0.35f, 1.0f});
    canvas_->drawRectangle(
        {panel.x + 52.0f, panel.y + 8.0f, 18.0f, 18.0f},
        splitter_.isDragging() ? DFColor{0.30f, 0.6f, 1.0f, 1.0f} : DFColor{0.35f, 0.35f, 0.35f, 1.0f});
    canvas_->drawRectangle(
        {panel.x + 74.0f, panel.y + 8.0f, 18.0f, 18.0f},
        df::DockManager::instance().isDragging() ? DFColor{0.2f, 0.72f, 0.95f, 1.0f} : DFColor{0.35f, 0.35f, 0.35f, 1.0f});
    canvas_->drawRectangle(
        {panel.x + 96.0f, panel.y + 8.0f, 18.0f, 18.0f},
        df::WindowManager::instance().hasDraggingWindow() ? DFColor{0.85f, 0.55f, 0.20f, 1.0f} : DFColor{0.35f, 0.35f, 0.35f, 1.0f});

    if (df::DockManager::instance().isFloatingDragging()) {
        canvas_->drawRectangle({panel.x + 118.0f, panel.y + 8.0f, 18.0f, 18.0f}, theme.overlayAccent);
    }
}

void DX12Demo::updateStatusCaption()
{
    if (!statusDirty_ && captionFrameCountdown_ > 0) {
        --captionFrameCountdown_;
        return;
    }

    int floatingCount = 0;
    for (const auto& widget : widgets_) {
        if (widget && widget->isFloating()) {
            ++floatingCount;
        }
    }

    double avgFrameMs = 0.0;
    if (!frameTimesMs_.empty()) {
        const size_t tail = std::min<size_t>(frameTimesMs_.size(), 60);
        double sum = 0.0;
        for (size_t i = frameTimesMs_.size() - tail; i < frameTimesMs_.size(); ++i) {
            sum += frameTimesMs_[i];
        }
        avgFrameMs = sum / static_cast<double>(tail);
    }
    const double fps = (avgFrameMs > 0.001) ? (1000.0 / avgFrameMs) : 0.0;

    std::ostringstream oss;
    oss << "DX12 Docking Demo"
        << " | mode=" << (automationMode_ ? "auto" : "manual")
        << " | theme=" << themeName_
        << " | fps=" << static_cast<int>(fps + 0.5)
        << " (" << std::fixed << std::setprecision(1) << avgFrameMs << "ms)"
        << " | mouse=(" << static_cast<int>(lastMousePos_.x) << "," << static_cast<int>(lastMousePos_.y) << ")"
        << " lmb=" << (leftMouseDown_ ? "down" : "up")
        << " action=" << ActionOwnerName(activeAction_)
        << " dock_drag=" << (df::DockManager::instance().isDragging() ? "1" : "0")
        << " float_drag=" << (df::DockManager::instance().isFloatingDragging() ? "1" : "0")
        << " split_drag=" << (splitter_.isDragging() ? "1" : "0")
        << " win_drag=" << (df::WindowManager::instance().hasDraggingWindow() ? "1" : "0")
        << " tabs=" << tabVisuals_.size()
        << " floating=" << floatingCount;
    const std::string caption = oss.str();
    SetWindowTextA(hwnd_, caption.c_str());

    statusDirty_ = false;
    captionFrameCountdown_ = 8;
}

bool DX12Demo::beginTabGesture(Event& event)
{
    if (!kEnableTabUi) return false;
    if (event.type != Event::Type::MouseDown) return false;
    const DFPoint p{event.x, event.y};
    TabInteractionHit hit{};
    if (!HandleTabInteraction(layout_.root(), p, hit) || !hit.node) {
        return false;
    }

    if (hit.closeHit) {
        if (closeTabNode(hit.node, hit.tabIndex)) {
            event.handled = true;
            lastDispatchHandler_ = "tab:close";
            eventConsole_.logHandled(event, lastDispatchHandler_);
            statusDirty_ = true;
            return true;
        }
        return false;
    }

    if (hit.node->activeTab != hit.tabIndex) {
        // Activate tab immediately on press so click behavior feels responsive.
        hit.node->activeTab = hit.tabIndex;
        refreshLayoutState();
        updateHoverState(p);
    }

    tabGesture_.active = true;
    tabGesture_.node = hit.node;
    tabGesture_.tabIndex = hit.tabIndex;
    tabGesture_.strip = {
        hit.node->bounds.x,
        hit.node->bounds.y,
        hit.node->bounds.width,
        hit.node->tabBarHeight
    };
    tabGesture_.start = p;
    activeAction_ = ActionOwner::TabGesture;
    event.handled = true;
    lastDispatchHandler_ = "tab:hold";
    eventConsole_.logHandled(event, lastDispatchHandler_);
    statusDirty_ = true;
    return true;
}

bool DX12Demo::undockActiveTab(const DFPoint& mousePos)
{
    if (!tabGesture_.active || !tabGesture_.node) return false;
    auto* node = tabGesture_.node;
    if (!node->children.size()) return false;
    if (tabGesture_.tabIndex < 0 || tabGesture_.tabIndex >= static_cast<int>(node->children.size())) return false;

    df::DockWidget* undockedWidget = node->children[tabGesture_.tabIndex] ? node->children[tabGesture_.tabIndex]->widget : nullptr;
    if (!undockedWidget) return false;

    const DFRect sourceBounds = undockedWidget->bounds();
    const float width = std::max(260.0f, sourceBounds.width);
    const float height = std::max(180.0f, sourceBounds.height + df::DX12DockWidget::TITLE_BAR_HEIGHT);
    DFRect floatBounds{
        mousePos.x - width * 0.35f,
        mousePos.y - 14.0f,
        width,
        height
    };

    node->children.erase(node->children.begin() + tabGesture_.tabIndex);
    if (node->children.empty()) {
        node->type = df::DockLayout::Node::Type::Widget;
        node->widget = nullptr;
        node->activeTab = 0;
    } else if (node->children.size() == 1) {
        std::unique_ptr<df::DockLayout::Node> survivor = std::move(node->children.front());
        node->children.clear();
        if (survivor && survivor->type == df::DockLayout::Node::Type::Widget) {
            node->type = df::DockLayout::Node::Type::Widget;
            node->widget = survivor->widget;
            node->activeTab = 0;
        } else if (survivor) {
            *node = std::move(*survivor);
        }
    } else {
        node->activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
    }

    auto* newWindow = df::WindowManager::instance().createFloatingWindow(undockedWidget, floatBounds);
    if (!newWindow) return false;

    refreshLayoutState();
    auto& mgr = df::DockManager::instance();
    mgr.startFloatingDrag(newWindow, mousePos);
    // Seed drop candidates immediately so a quick release after undock
    // still resolves docking targets (edges/center) deterministically.
    mgr.updateFloatingDrag(mousePos);

    activeWindow_ = newWindow;
    activeAction_ = ActionOwner::FloatingWindow;
    tabGesture_.undocked = true;
    tabGesture_.active = false;
    lastDispatchHandler_ = "tab:undock";
    statusDirty_ = true;
    return true;
}

bool DX12Demo::handleTabGesture(Event& event)
{
    if (!tabGesture_.active) return false;

    const DFPoint p{event.x, event.y};
    if (event.type == Event::Type::MouseMove) {
        if (tabGesture_.node &&
            tabGesture_.node->type == df::DockLayout::Node::Type::Tab &&
            tabGesture_.strip.contains(p) &&
            tabGesture_.node->children.size() > 1) {
            const float tabWidth = tabGesture_.strip.width / static_cast<float>(tabGesture_.node->children.size());
            const int hoveredIndex = static_cast<int>((p.x - tabGesture_.strip.x) / std::max(1.0f, tabWidth));
            if (hoveredIndex >= 0 &&
                hoveredIndex < static_cast<int>(tabGesture_.node->children.size()) &&
                hoveredIndex != tabGesture_.tabIndex) {
                std::swap(tabGesture_.node->children[hoveredIndex], tabGesture_.node->children[tabGesture_.tabIndex]);
                tabGesture_.tabIndex = hoveredIndex;
                tabGesture_.node->activeTab = hoveredIndex;
                refreshLayoutState();
                statusDirty_ = true;
            }
        }

        const float dx = p.x - tabGesture_.start.x;
        const float dy = p.y - tabGesture_.start.y;
        const float distSq = dx * dx + dy * dy;
        const bool movedEnough = distSq > (kTabUndockDragThresholdPx * kTabUndockDragThresholdPx);
        const bool outsideStrip = !tabGesture_.strip.contains(p);
        if (!tabGesture_.undocked && movedEnough && outsideStrip) {
            if (undockActiveTab(p)) {
                event.handled = true;
                eventConsole_.logHandled(event, "tab:undock");
                statusDirty_ = true;
                return true;
            }
        }
        event.handled = true;
        lastDispatchHandler_ = "tab:hold";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        statusDirty_ = true;
        return true;
    }

    if (event.type == Event::Type::MouseUp) {
        if (tabGesture_.node) {
            tabGesture_.node->activeTab = tabGesture_.tabIndex;
            refreshLayoutState();
            lastDispatchHandler_ = "tab:select";
            eventConsole_.logHandled(event, lastDispatchHandler_);
        }
        clearActiveAction();
        event.handled = true;
        statusDirty_ = true;
        return true;
    }

    return false;
}

bool DX12Demo::handleActiveAction(Event& event)
{
    if (activeAction_ == ActionOwner::None) return false;
    if (activeWindow_ && !df::WindowManager::instance().hasWindow(activeWindow_)) {
        activeWindow_ = nullptr;
    }

    switch (activeAction_) {
    case ActionOwner::FloatingWindow: {
        auto& mgr = df::DockManager::instance();
        if (mgr.isFloatingDragging() && mgr.handleEvent(event)) {
            event.handled = true;
            lastDispatchHandler_ = "floating_drag";
            eventConsole_.logHandled(event, lastDispatchHandler_);
            if (event.type == Event::Type::MouseUp) {
                activeWindow_ = nullptr;
                clearActiveAction();
                refreshLayoutState();
                if (widgets_.size() > 4 && widgets_[4] && !widgets_[4]->isFloating()) {
                    floatingWindow_ = nullptr;
                }
            }
            statusDirty_ = true;
            return true;
        }
        if (activeWindow_ && activeWindow_->handleEvent(event)) {
            if (activeWindow_->consumeCloseRequest()) {
                df::DockManager::instance().closeWidget(activeWindow_->content());
                auto* closing = activeWindow_;
                activeWindow_ = nullptr;
                if (closing == floatingWindow_) {
                    floatingWindow_ = nullptr;
                }
                clearActiveAction();
                refreshLayoutState();
                event.handled = true;
                lastDispatchHandler_ = "floating_close";
                eventConsole_.logHandled(event, lastDispatchHandler_);
                statusDirty_ = true;
                return true;
            }
            event.handled = true;
            lastDispatchHandler_ = "floating_window";
            eventConsole_.logHandled(event, lastDispatchHandler_);
            if (event.type == Event::Type::MouseUp) {
                clearActiveAction();
            }
            statusDirty_ = true;
            return true;
        }
        if (event.type == Event::Type::MouseUp) {
            activeWindow_ = nullptr;
            clearActiveAction();
            event.handled = true;
            statusDirty_ = true;
            return true;
        }
        event.handled = true;
        statusDirty_ = true;
        return true;
    }

    case ActionOwner::DockWidgetDrag: {
        auto& mgr = df::DockManager::instance();
        if (mgr.handleEvent(event)) {
            if (mgr.isFloatingDragging()) {
                activeAction_ = ActionOwner::FloatingWindow;
                activeWindow_ = df::WindowManager::instance().findWindowAtPoint({event.x, event.y});
                lastDispatchHandler_ = "floating_drag";
            } else {
                lastDispatchHandler_ = "dock_drag";
            }
            eventConsole_.logHandled(event, lastDispatchHandler_);
            if (event.type == Event::Type::MouseUp) {
                clearActiveAction();
            }
            statusDirty_ = true;
            return true;
        }
        if (event.type == Event::Type::MouseUp) {
            clearActiveAction();
            event.handled = true;
            statusDirty_ = true;
            return true;
        }
        event.handled = true;
        statusDirty_ = true;
        return true;
    }

    case ActionOwner::SplitterDrag:
        if (splitter_.handleEvent(event)) {
            lastDispatchHandler_ = "splitter";
            eventConsole_.logHandled(event, lastDispatchHandler_);
            refreshLayoutState();
            if (event.type == Event::Type::MouseUp) {
                clearActiveAction();
            }
            statusDirty_ = true;
            return true;
        }
        if (event.type == Event::Type::MouseUp) {
            clearActiveAction();
            event.handled = true;
            statusDirty_ = true;
            return true;
        }
        event.handled = true;
        statusDirty_ = true;
        return true;

    case ActionOwner::TabGesture:
        return handleTabGesture(event);

    case ActionOwner::None:
    default:
        return false;
    }
}

void DX12Demo::renderFrame()
{
    const auto frameStart = std::chrono::steady_clock::now();
    ThrowIfFailed(commandAllocator_->Reset());
    ThrowIfFailed(commandList_->Reset(commandAllocator_.Get(), nullptr));

    // Transition to render target
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets_[frameIndex_].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frameIndex_ * rtvStride_;
    const float clearDFColor[] = {55.0f / 255.0f, 53.0f / 255.0f, 62.0f / 255.0f, 1.0f};
    commandList_->ClearRenderTargetView(rtv, clearDFColor, 0, nullptr);
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissor_);

    canvas_->clear();
    refreshLayoutState();
    syncNativeFloatingHosts();
    const auto& theme = df::CurrentTheme();
    const DFRect viewRect{0.0f, 0.0f, viewport_.Width, viewport_.Height};
    const DFRect mainClientRect = ComputeMainClientRect(viewRect, theme);
    if (theme.drawClientArea &&
        mainClientRect.width > 0.0f &&
        mainClientRect.height > 0.0f) {
        const float maxRadius = std::min(mainClientRect.width, mainClientRect.height) * 0.5f;
        const float cornerRadius = theme.drawRoundedClientArea
            ? std::clamp(theme.clientAreaCornerRadius, 0.0f, maxRadius)
            : 0.0f;
        if (cornerRadius > 0.0f) {
            canvas_->drawRoundedRectangle(mainClientRect, cornerRadius, theme.clientAreaFill);
        } else {
            canvas_->drawRectangle(mainClientRect, theme.clientAreaFill);
        }
        if (theme.drawClientAreaBorder) {
            canvas_->drawRoundedRectangleOutline(
                mainClientRect,
                cornerRadius,
                theme.clientAreaBorder,
                std::max(0.5f, theme.clientAreaBorderThickness));
        }
    }
    df::DockRenderer renderer;
    renderer.setMousePosition(lastMousePos_);
    renderer.render(*canvas_, layout_.root());

    for (auto& w : widgets_) {
        if (IsRenderableDockWidget(w.get())) {
            if (theme.drawWidgetHoverOutline &&
                hoveredDockWidget_ == w.get() &&
                activeAction_ == ActionOwner::None) {
                const DFRect b = w->bounds();
                const DFColor hover = theme.overlayAccentSoft;
                canvas_->drawRectangle({b.x, b.y, b.width, 2.0f}, hover);
                canvas_->drawRectangle({b.x, b.y + b.height - 2.0f, b.width, 2.0f}, hover);
            }
        }
    }
    if (showDebugOverlay_) {
        renderDebugOverlay();
    }
    splitter_.render(*canvas_);
    df::WindowManager::instance().updateAllWindows();
    if (!nativeFloatHostsEnabled_) {
        df::WindowManager::instance().renderAllWindows(*canvas_);
    }
    df::DockManager::instance().overlay().render(*canvas_);
    canvas_->flush();

    // Transition to present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList_->Close());
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    swapChain_->Present(1, 0);

    waitForGPU();
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    const auto frameEnd = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    frameTimesMs_.push_back(ms);
    updateStatusCaption();
}

void DX12Demo::waitForGPU()
{
    const UINT64 fenceToWait = fenceValue_;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceToWait));
    fenceValue_++;
    if (fence_->GetCompletedValue() < fenceToWait) {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceToWait, fenceEvent_));
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DX12Demo::handleResize(UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    if (!swapChain_ || !device_ || !rtvHeap_) return;

    resizing_ = true;
    syncClientOriginScreen();
    const UINT currentW = static_cast<UINT>(viewport_.Width);
    const UINT currentH = static_cast<UINT>(viewport_.Height);
    if (width == currentW && height == currentH) {
        resizing_ = false;
        return;
    }
    // Qt-like behavior: only reflow layout on resize; never scale UI interaction coordinates.

    waitForGPU();

    for (auto& rt : renderTargets_) {
        rt.Reset();
    }

    ThrowIfFailed(swapChain_->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i) {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])));
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, handle);
        handle.ptr += rtvStride_;
    }

    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    scissor_.left = 0;
    scissor_.top = 0;
    scissor_.right = static_cast<LONG>(width);
    scissor_.bottom = static_cast<LONG>(height);

    if (canvas_) {
        canvas_->setRenderSize(static_cast<float>(width), static_cast<float>(height));
    }

    lastMousePos_.x = SafeClamp(lastMousePos_.x, 0.0f, viewport_.Width);
    lastMousePos_.y = SafeClamp(lastMousePos_.y, 0.0f, viewport_.Height);

    // Resizing invalidates ongoing gestures/drag assumptions.
    clearActiveAction();
    if (captureActive_) {
        ReleaseCapture();
        captureActive_ = false;
        leftMouseDown_ = false;
    }
    refreshLayoutState();
    resizing_ = false;
    statusDirty_ = true;

    if (resizeDebug_) {
        std::ostringstream oss;
        oss << "resize_debug old=" << currentW << "x" << currentH
            << " new=" << width << "x" << height
            << " scale=(1.000,1.000)"
            << " action=" << ActionOwnerName(activeAction_)
            << " dock_drag=" << (df::DockManager::instance().isDragging() ? "1" : "0")
            << " split_drag=" << (splitter_.isDragging() ? "1" : "0")
            << " win_drag=" << (df::WindowManager::instance().hasDraggingWindow() ? "1" : "0");
        eventConsole_.logAutomation(oss.str());
        for (const auto& widget : widgets_) {
            if (!widget || widget->isFloating()) continue;
            const DFRect b = widget->bounds();
            std::ostringstream wss;
            wss << "resize_debug_panel name=" << widget->title()
                << " bounds=(" << static_cast<int>(b.x) << "," << static_cast<int>(b.y) << ","
                << static_cast<int>(b.width) << "," << static_cast<int>(b.height) << ")"
                << " viewport=(" << static_cast<int>(viewport_.Width) << "," << static_cast<int>(viewport_.Height) << ")";
            eventConsole_.logAutomation(wss.str());
        }
    }
}

int DX12Demo::run()
{
    if (EnvEnabled("DF_FORCE_CRASH", false)) {
        volatile int* crash = nullptr;
        *crash = 1;
    }

    if (automationMode_) {
        return runAutomatedEventChecks() ? 0 : 2;
    }

    MSG msg{};
    while (running_) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running_ = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (running_) {
            renderFrame();
        }
    }
    return 0;
}

bool DX12Demo::isEnvEnabled(const char* name)
{
    return EnvEnabled(name, false);
}

LRESULT CALLBACK DX12Demo::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto create = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* demo = reinterpret_cast<DX12Demo*>(create->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)demo);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    auto* demo = reinterpret_cast<DX12Demo*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (!demo) return DefWindowProc(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_ENTERSIZEMOVE:
        demo->inSizeMove_ = true;
        demo->resizing_ = true;
        demo->statusDirty_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        demo->inSizeMove_ = false;
        demo->resizing_ = false;
        demo->hasPendingResize_ = false;
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            const UINT w = static_cast<UINT>(LOWORD(lParam));
            const UINT h = static_cast<UINT>(HIWORD(lParam));
            demo->syncClientOriginScreen();
            demo->pendingResizeW_ = w;
            demo->pendingResizeH_ = h;
            demo->hasPendingResize_ = (w > 0 && h > 0);
            try {
                demo->handleResize(w, h);
                // During interactive sizing, force an immediate frame so
                // layout-driven geometry updates continuously with the mouse.
                if (demo->inSizeMove_ && !demo->liveResizeRenderInProgress_) {
                    demo->liveResizeRenderInProgress_ = true;
                    demo->renderFrame();
                    demo->liveResizeRenderInProgress_ = false;
                }
            } catch (const std::exception& e) {
                demo->liveResizeRenderInProgress_ = false;
                AppendRuntimeError("WM_SIZE", e.what());
                PostQuitMessage(-1);
            } catch (...) {
                demo->liveResizeRenderInProgress_ = false;
                AppendRuntimeError("WM_SIZE", "unknown exception");
                PostQuitMessage(-1);
            }
        }
        return 0;
    case WM_MOVE:
        demo->syncClientOriginScreen();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
        return demo->handleMouseMessage(msg, wParam, lParam);
    case WM_CHAR:
        return demo->handleCharMessage(wParam, lParam);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        return demo->handleKeyMessage(wParam, lParam);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK DX12Demo::FloatingHostWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* init = reinterpret_cast<NativeFloatingHostCreateData*>(cs ? cs->lpCreateParams : nullptr);
        if (init) {
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(init->demo));
            SetPropW(hWnd, L"DF_FLOAT_WIDGET", reinterpret_cast<HANDLE>(init->widget));
            delete init;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    auto* demo = reinterpret_cast<DX12Demo*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    auto* widget = reinterpret_cast<df::DockWidget*>(GetPropW(hWnd, L"DF_FLOAT_WIDGET"));

    auto findHost = [demo, widget]() -> DX12Demo::NativeFloatingHost* {
        if (!demo || !widget) {
            return nullptr;
        }
        auto it = demo->nativeFloatingHosts_.find(widget);
        if (it == demo->nativeFloatingHosts_.end()) {
            return nullptr;
        }
        return &it->second;
    };

    auto cursorToMainClient = [demo]() -> DFPoint {
        POINT cursor{};
        if (!demo || !GetCursorPos(&cursor)) {
            return {0.0f, 0.0f};
        }
        if (!demo->hwnd_ || !IsWindow(demo->hwnd_) || !ScreenToClient(demo->hwnd_, &cursor)) {
            return {0.0f, 0.0f};
        }
        return {static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
    };

    switch (msg) {
    case WM_NCHITTEST:
        if (demo && widget) {
            const auto& theme = df::CurrentTheme();
            const bool drawTitleIcons = theme.drawTitleBarIcons && widget->visualOptions().drawTitleBarIcons;
            const LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
            if (hit != HTCLIENT) {
                return hit;
            }
            POINT pt{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };
            if (!ScreenToClient(hWnd, &pt)) {
                return HTCLIENT;
            }
            RECT rc{};
            GetClientRect(hWnd, &rc);
            if (drawTitleIcons) {
                const RECT closeRect = NativeHostCloseRect(rc);
                if (PtInRect(&closeRect, pt)) {
                    return HTCLIENT;
                }
            }
            if (pt.y >= 0 && pt.y < kNativeHostTitleBarHeight) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        break;
    case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_MOUSELEAVE:
        InvalidateRect(hWnd, nullptr, FALSE);
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_LBUTTONDOWN:
        if (demo && widget) {
            const auto& theme = df::CurrentTheme();
            const bool drawTitleIcons = theme.drawTitleBarIcons && widget->visualOptions().drawTitleBarIcons;
            RECT rc{};
            GetClientRect(hWnd, &rc);
            if (drawTitleIcons) {
                const RECT closeRect = NativeHostCloseRect(rc);
                const POINT p{
                    GET_X_LPARAM(lParam),
                    GET_Y_LPARAM(lParam)
                };
                if (PtInRect(&closeRect, p)) {
                    demo->pendingNativeHostClose_.push_back(widget);
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
            }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_ENTERSIZEMOVE:
        if (demo && widget) {
            if (auto* frame = df::WindowManager::instance().findWindowByContent(widget)) {
                auto* host = findHost();
                const DFPoint localMouse = cursorToMainClient();
                auto& mgr = df::DockManager::instance();
                // WM_ENTERSIZEMOVE is raised for both move and resize.
                // Only caption-move should enter docking drag mode.
                POINT cursor{};
                LRESULT hit = HTCLIENT;
                if (GetCursorPos(&cursor)) {
                    hit = SendMessageW(
                        hWnd,
                        WM_NCHITTEST,
                        0,
                        MAKELPARAM(cursor.x, cursor.y));
                }
                const bool isCaptionMove = (hit == HTCAPTION);
                if (host) {
                    host->dockDragActive = false;
                }
                if (mgr.isFloatingDragging() && mgr.floatingDragWindow() != frame) {
                    mgr.cancelFloatingDrag();
                }
                if (isCaptionMove && !mgr.isFloatingDragging()) {
                    mgr.startFloatingDrag(frame, localMouse);
                    if (host) {
                        host->dockDragActive = true;
                    }
                }
                demo->activeWindow_ = frame;
                demo->activeAction_ = ActionOwner::FloatingWindow;
                demo->statusDirty_ = true;
                InvalidateRect(demo->hwnd_, nullptr, FALSE);
            }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_CLOSE:
        if (demo && widget) {
            demo->pendingNativeHostClose_.push_back(widget);
            return 0;
        }
        break;
    case WM_MOVING:
        if (demo && widget) {
            auto* host = findHost();
            auto* frame = df::WindowManager::instance().findWindowByContent(widget);
            auto& mgr = df::DockManager::instance();
            if (!frame) {
                mgr.cancelFloatingDrag();
                return DefWindowProcW(hWnd, msg, wParam, lParam);
            }
            // Ignore resize loops: WM_MOVING should only drive dock-preview when
            // a caption drag explicitly started dock drag mode.
            if (!host || !host->dockDragActive) {
                return DefWindowProcW(hWnd, msg, wParam, lParam);
            }
            if (mgr.isFloatingDragging() && mgr.floatingDragWindow() != frame) {
                mgr.cancelFloatingDrag();
            }
            if (!mgr.isFloatingDragging()) {
                const DFPoint localMouse = cursorToMainClient();
                mgr.startFloatingDrag(frame, localMouse);
            }
            const DFPoint localMouse = cursorToMainClient();
            mgr.updateFloatingDrag(localMouse);
            demo->statusDirty_ = true;
            InvalidateRect(demo->hwnd_, nullptr, FALSE);
            // Force immediate redraw during native window move loop so edge hints
            // remain visible while dragging external floating hosts.
            if (!demo->liveNativeMoveRenderInProgress_) {
                demo->liveNativeMoveRenderInProgress_ = true;
                try {
                    demo->renderFrame();
                } catch (const std::exception& e) {
                    // Keep host window responsive even if a frame fails.
                    AppendRuntimeError("WM_MOVING", e.what());
                }
                demo->liveNativeMoveRenderInProgress_ = false;
            }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_EXITSIZEMOVE:
        if (demo && widget) {
            auto* host = findHost();
            demo->onNativeFloatingHostMovedOrSized(widget, hWnd);
            if (auto* frame = df::WindowManager::instance().findWindowByContent(widget)) {
                auto& mgr = df::DockManager::instance();
                if (mgr.isFloatingDragging() && mgr.floatingDragWindow() != frame) {
                    mgr.cancelFloatingDrag();
                }
            }
            if (host && host->dockDragActive) {
                host->dockDragActive = false;
                if (demo->tryDockNativeFloatingHost(widget, hWnd)) {
                    return 0;
                }
            }
            // Ensure drag lifecycle is closed even when docking did not occur.
            df::DockManager::instance().cancelFloatingDrag();
            demo->activeWindow_ = nullptr;
            demo->activeAction_ = ActionOwner::None;
            demo->statusDirty_ = true;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_MOVE:
    case WM_SIZE:
        if (demo && widget) {
            demo->onNativeFloatingHostMovedOrSized(widget, hWnd);
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    case WM_PAINT:
        if (demo) {
            demo->paintNativeFloatingHost(hWnd);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemovePropW(hWnd, L"DF_FLOAT_WIDGET");
        break;
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    try {
        InstallCrashReporter();
        DX12Demo demo(hInstance);
        return demo.run();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "DX12 Demo Error", MB_ICONERROR | MB_OK);
        return -1;
    }
}

// Provide a console-friendly entry point so the binary links even when built
// as a console subsystem target.
int main(int /*argc*/, char** /*argv*/)
{
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT);
}

LRESULT DX12Demo::handleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    Event e;
    e.x = static_cast<float>(GET_X_LPARAM(lParam));
    e.y = static_cast<float>(GET_Y_LPARAM(lParam));
    lastMousePos_ = {e.x, e.y};

    switch (msg) {
    case WM_LBUTTONDOWN:
        e.type = Event::Type::MouseDown;
        SetCapture(hwnd_);
        captureActive_ = true;
        leftMouseDown_ = true;
        break;
    case WM_LBUTTONDBLCLK:
        e.type = Event::Type::MouseDoubleClick;
        SetCapture(hwnd_);
        captureActive_ = true;
        leftMouseDown_ = true;
        break;
    case WM_LBUTTONUP:
        e.type = Event::Type::MouseUp;
        if (captureActive_) {
            ReleaseCapture();
            captureActive_ = false;
        }
        leftMouseDown_ = false;
        break;
    case WM_MOUSEMOVE:
        e.type = Event::Type::MouseMove;
        break;
    default: break;
    }

    statusDirty_ = true;
    processEvent(e);
    return e.handled ? 0 : DefWindowProc(hwnd_, msg, wParam, lParam);
}

void DX12Demo::processEvent(Event& event)
{
    const bool pointerEvent =
        event.type == Event::Type::MouseDown ||
        event.type == Event::Type::MouseUp ||
        event.type == Event::Type::MouseMove ||
        event.type == Event::Type::MouseDoubleClick ||
        event.type == Event::Type::MouseDrag;

    if (resizing_ || viewport_.Width <= 0.0f || viewport_.Height <= 0.0f) {
        event.handled = true;
        lastDispatchHandler_ = "resize_blocked";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        return;
    }

    if (pointerEvent) {
        event.x = SafeClamp(event.x, 0.0f, viewport_.Width);
        event.y = SafeClamp(event.y, 0.0f, viewport_.Height);
        lastMousePos_ = {event.x, event.y};
        updateHoverState(lastMousePos_);
        if (event.type == Event::Type::MouseDown || event.type == Event::Type::MouseDoubleClick) {
            leftMouseDown_ = true;
        } else if (event.type == Event::Type::MouseUp) {
            leftMouseDown_ = false;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const bool anyDrag = df::DockManager::instance().isDragging() ||
        df::DockManager::instance().isFloatingDragging() ||
        splitter_.isDragging() ||
        df::WindowManager::instance().hasDraggingWindow();
    eventConsole_.logIncoming(event, anyDrag);

    lastDispatchHandler_.clear();
    if (pointerEvent) {
        dispatchMouseEvent(event);
    } else if (Widget* focused = df::WindowManager::instance().focusedWidget()) {
        focused->handleEvent(event);
        if (event.handled) {
            lastDispatchHandler_ = "focused_widget";
            eventConsole_.logHandled(event, lastDispatchHandler_);
        }
    }

    if (!event.handled) {
        lastDispatchHandler_ = "none";
        eventConsole_.logHandled(event, "none");
    } else if (lastDispatchHandler_.empty()) {
        lastDispatchHandler_ = "accepted_unclassified";
        eventConsole_.logHandled(event, lastDispatchHandler_);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    eventDurationsMs_.push_back(ms);
    statusDirty_ = true;
    updateStatusCaption();
}

void DX12Demo::dispatchMouseEvent(Event& event)
{
    const bool isPress = event.type == Event::Type::MouseDown ||
        event.type == Event::Type::MouseDoubleClick;
    const bool isRelease = event.type == Event::Type::MouseUp;

    if (event.x < 0.0f || event.y < 0.0f || event.x > viewport_.Width || event.y > viewport_.Height) {
        event.handled = true;
        lastDispatchHandler_ = "bounds_reject";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        statusDirty_ = true;
        return;
    }

    auto& mgr = df::DockManager::instance();

    if (isPress) {
        refreshLayoutState();
        updateHoverState({event.x, event.y});
    }

    if (activeAction_ != ActionOwner::None && !isPress) {
        if (handleActiveAction(event)) {
            return;
        }
    }

    const DFPoint p{event.x, event.y};
    const bool hitFloating = df::WindowManager::instance().findWindowAtPoint(p) != nullptr;
    const bool hitSplitter = splitter_.splitterAtPoint(p) != nullptr;
    int widgetHits = 0;
    for (const auto& w : widgets_) {
        if (!IsRenderableDockWidget(w.get())) {
            continue;
        }
        if (w->bounds().contains(p)) {
            ++widgetHits;
        }
    }
    const int hitGroups = (hitFloating ? 1 : 0) + (hitSplitter ? 1 : 0) + (widgetHits > 0 ? 1 : 0);
    if (hitGroups > 1) {
        eventConsole_.logConflict(event, hitFloating, hitSplitter, widgetHits);
    }

    // 1) Active floating drag (capture can route outside original window).
    if (!event.handled && mgr.isFloatingDragging() && mgr.handleEvent(event)) {
        lastDispatchHandler_ = "floating_drag";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        if (isRelease) {
            activeWindow_ = nullptr;
            clearActiveAction();
            refreshLayoutState();
        }
        statusDirty_ = true;
        return;
    }

    // 2) Floating windows first (top-most semantics).
    if (auto* win = df::WindowManager::instance().findWindowAtPoint(p)) {
        if (isPress) {
            df::WindowManager::instance().bringToFront(win);
        }
        if (win->handleEvent(event)) {
            if (win->consumeCloseRequest()) {
                df::DockManager::instance().closeWidget(win->content());
                if (win == floatingWindow_) {
                    floatingWindow_ = nullptr;
                }
                if (win == activeWindow_) {
                    activeWindow_ = nullptr;
                    activeAction_ = ActionOwner::None;
                }
                refreshLayoutState();
                lastDispatchHandler_ = "floating_close";
                eventConsole_.logHandled(event, lastDispatchHandler_);
                statusDirty_ = true;
                return;
            }
            const bool startedFloatingDrag = mgr.isFloatingDragging();
            lastDispatchHandler_ = startedFloatingDrag ? "floating_drag_start" : "floating_window";
            eventConsole_.logHandled(event, lastDispatchHandler_);
            if (isPress) {
                activeWindow_ = win;
                activeAction_ = ActionOwner::FloatingWindow;
            } else if (isRelease) {
                clearActiveAction();
            }
            statusDirty_ = true;
            return;
        }
    }

    // 3) Tab strip interactions (currently disabled).
    if (kEnableTabUi && !event.handled && isPress && beginTabGesture(event)) {
        statusDirty_ = true;
        return;
    }

    // 4) Active drags via DockManager (widget drag). Only if not already handled.
    if (!event.handled && mgr.handleEvent(event)) {
        lastDispatchHandler_ = "dock_drag";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        if (isPress) {
            activeAction_ = ActionOwner::DockWidgetDrag;
        } else if (isRelease) {
            clearActiveAction();
        }
        statusDirty_ = true;
        return;
    }

    // 5) Docked widgets under cursor.
    for (auto& w : widgets_) {
        if (!IsRenderableDockWidget(w.get())) {
            continue;
        }
        if (w->bounds().contains(p)) {
            w->handleEvent(event);
            if (event.handled) {
                lastDispatchHandler_ = std::string("widget:") + w->title();
                eventConsole_.logHandled(event, lastDispatchHandler_);
                if (isPress) {
                    if (mgr.isFloatingDragging()) {
                        activeAction_ = ActionOwner::FloatingWindow;
                        activeWindow_ = df::WindowManager::instance().findWindowAtPoint(p);
                    } else if (mgr.isDragging()) {
                        activeAction_ = ActionOwner::DockWidgetDrag;
                    }
                }
                refreshLayoutState();
                statusDirty_ = true;
                return;
            }
        }
    }

    // 6) Splitters last so title/tab/widget interactions win near overlaps.
    if (!event.handled && splitter_.handleEvent(event)) {
        lastDispatchHandler_ = "splitter";
        eventConsole_.logHandled(event, lastDispatchHandler_);
        if (isPress) {
            activeAction_ = ActionOwner::SplitterDrag;
        } else if (isRelease) {
            clearActiveAction();
        }
        refreshLayoutState();
        statusDirty_ = true;
        return;
    }
}

bool DX12Demo::injectEvent(Event::Type type, float x, float y, const char* expectedPrefix, const char* label)
{
    Event event(type);
    event.x = x;
    event.y = y;
    processEvent(event);

    if (eventSleepMs_ > 0) {
        Sleep(static_cast<DWORD>(eventSleepMs_));
    }

    bool prefixMatch = (expectedPrefix == nullptr);
    if (!prefixMatch && expectedPrefix) {
        std::string expected = expectedPrefix;
        size_t start = 0;
        while (start <= expected.size()) {
            const size_t delim = expected.find('|', start);
            const std::string token = expected.substr(start, (delim == std::string::npos) ? std::string::npos : (delim - start));
            if (!token.empty() && lastDispatchHandler_.rfind(token, 0) == 0) {
                prefixMatch = true;
                break;
            }
            if (delim == std::string::npos) break;
            start = delim + 1;
        }
    }
    std::ostringstream oss;
    oss << label << " expected=" << (expectedPrefix ? expectedPrefix : "<any>")
        << " actual=" << lastDispatchHandler_;
    if (!prefixMatch) {
        oss << " [FAIL]";
    }
    eventConsole_.logAutomation(oss.str());
    return prefixMatch;
}

bool DX12Demo::runAutomatedEventChecks()
{
    eventConsole_.logAutomation("automation mode enabled");
    if (widgets_.size() < 2 || !layout_.root()) {
        eventConsole_.logAutomation("framework not initialized [FAIL]");
        return false;
    }
    refreshLayoutState();

    const char* scenarioEnv = std::getenv("DF_AUTOMATION_SCENARIO");
    const std::string scenario = scenarioEnv && scenarioEnv[0] ? scenarioEnv : "baseline";
    const int fuzzIterations = std::clamp(EnvInt("DF_AUTOMATION_FUZZ_ITERS", 28), 4, 256);
    unsigned int fuzzSeed = static_cast<unsigned int>(std::max(1, EnvInt("DF_AUTOMATION_SEED", 1337)));
    const unsigned int initialFuzzSeed = fuzzSeed;
    eventConsole_.logAutomation(std::string("scenario=") + scenario);
    eventConsole_.logAutomation(
        std::string("fuzz_config iterations=") + std::to_string(fuzzIterations) +
        " seed=" + std::to_string(initialFuzzSeed));

    // Build deterministic coordinates from current layout.
    const DFRect left = widgets_[0]->bounds();

    int failures = 0;
    auto dragAnchorForWidget = [&](df::DockWidget* widget) -> DFPoint {
        if (!widget) {
            return {};
        }
        if (widget->isDocked() && widget->isTabified()) {
            if (auto* tabNode = FindParentTabOfWidget(layout_.root(), widget)) {
                const size_t count = tabNode->children.size();
                for (size_t i = 0; i < count; ++i) {
                    const auto& child = tabNode->children[i];
                    if (!child || child->type != df::DockLayout::Node::Type::Widget || child->widget != widget) {
                        continue;
                    }
                    const DFRect tabRect = df::DockLayout::TabRectForIndex(*tabNode, tabNode->bounds, i, count);
                    if (tabRect.width > 1.0f && tabRect.height > 1.0f) {
                        return {
                            tabRect.x + tabRect.width * 0.5f,
                            tabRect.y + tabRect.height * 0.5f
                        };
                    }
                }
            }
        }
        const DFRect b = widget->bounds();
        return {
            b.x + SafeClamp(50.0f, 10.0f, b.width - 10.0f),
            b.y + SafeClamp(10.0f, 5.0f, df::DX12DockWidget::TITLE_BAR_HEIGHT - 2.0f)
        };
    };
    auto pickDockedWidgetForInteraction = [&]() -> df::DockWidget* {
        const auto windows = df::WindowManager::instance().windowsSnapshot();
        auto isPointCoveredByFloatingWindow = [&](const DFPoint& p) -> bool {
            for (const auto* window : windows) {
                if (!window) {
                    continue;
                }
                if (window->bounds().contains(p)) {
                    return true;
                }
            }
            return false;
        };
        auto isCoveredByFloatingWindow = [&](const DFRect& b) -> bool {
            const DFPoint center{b.x + b.width * 0.5f, b.y + b.height * 0.5f};
            for (const auto* window : windows) {
                if (!window) {
                    continue;
                }
                if (window->bounds().contains(center)) {
                    return true;
                }
            }
            return false;
        };

        for (const auto& candidate : widgets_) {
            if (!candidate || candidate->isFloating()) {
                continue;
            }
            const DFRect b = candidate->bounds();
            const DFPoint titlePoint = dragAnchorForWidget(candidate.get());
            if (b.width > 16.0f && b.height > 16.0f &&
                !isCoveredByFloatingWindow(b) &&
                !isPointCoveredByFloatingWindow(titlePoint)) {
                return candidate.get();
            }
        }
        // Fallback to any visible docked widget if all are partially covered.
        for (const auto& candidate : widgets_) {
            if (!candidate || candidate->isFloating()) {
                continue;
            }
            const DFRect b = candidate->bounds();
            if (b.width > 16.0f && b.height > 16.0f) {
                return candidate.get();
            }
        }
        return nullptr;
    };
    auto validatePanelSizes = [&](const char* label) {
        const DFRect viewportRect{0.0f, 0.0f, viewport_.Width, viewport_.Height};
        df::DockLayout::Node* root = layout_.root();
        const bool strictWidth = root && root->calculatedMinWidth <= viewportRect.width + 1.0f;
        const bool strictHeight = root && root->calculatedMinHeight <= viewportRect.height + 1.0f;

        auto validateNodeRecursive =
            [&](auto&& self, df::DockLayout::Node* node, const DFRect& parentRect, bool hasParent) -> void {
            if (!node) return;

            const DFRect b = node->bounds;
            if (!std::isfinite(b.x) || !std::isfinite(b.y) ||
                !std::isfinite(b.width) || !std::isfinite(b.height)) {
                std::ostringstream oss;
                oss << label << " node has non-finite bounds [FAIL]"
                    << " node=(" << b.x << "," << b.y << "," << b.width << "," << b.height << ")";
                eventConsole_.logAutomation(oss.str());
                ++failures;
                return;
            }

            if (b.width < -0.5f || b.height < -0.5f) {
                std::ostringstream oss;
                oss << label << " node has negative size [FAIL]"
                    << " node=(" << b.x << "," << b.y << "," << b.width << "," << b.height << ")";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }

            if (b.x < viewportRect.x - 1.0f ||
                b.y < viewportRect.y - 1.0f ||
                b.x + b.width > viewportRect.x + viewportRect.width + 1.0f ||
                b.y + b.height > viewportRect.y + viewportRect.height + 1.0f) {
                std::ostringstream oss;
                oss << label << " node out of viewport bounds [FAIL]"
                    << " node=(" << b.x << "," << b.y << "," << b.width << "," << b.height << ")"
                    << " viewport=(" << viewportRect.x << "," << viewportRect.y << "," << viewportRect.width << "," << viewportRect.height << ")";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }

            if (hasParent &&
                (b.width > 1.0f || b.height > 1.0f) &&
                (b.x < parentRect.x - 2.0f ||
                    b.y < parentRect.y - 2.0f ||
                    b.x + b.width > parentRect.x + parentRect.width + 2.0f ||
                    b.y + b.height > parentRect.y + parentRect.height + 2.0f)) {
                std::ostringstream oss;
                oss << label << " child escapes parent bounds [FAIL]"
                    << " child=(" << b.x << "," << b.y << "," << b.width << "," << b.height << ")"
                    << " parent=(" << parentRect.x << "," << parentRect.y << "," << parentRect.width << "," << parentRect.height << ")";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }

            if (node->type == df::DockLayout::Node::Type::Split && node->first && node->second) {
                const float total = node->vertical ? b.width : b.height;
                const float firstSize = node->vertical ? node->first->bounds.width : node->first->bounds.height;
                const float secondSize = node->vertical ? node->second->bounds.width : node->second->bounds.height;
                if (std::abs((firstSize + secondSize) - total) > 2.5f) {
                    std::ostringstream oss;
                    oss << label << " split size sum mismatch [FAIL] total=" << total
                        << " first=" << firstSize << " second=" << secondSize;
                    eventConsole_.logAutomation(oss.str());
                    ++failures;
                }

                if (node->splitSizing != df::DockLayout::Node::SplitSizing::Ratio) {
                    const float fixedActual = (node->splitSizing == df::DockLayout::Node::SplitSizing::FixedFirst) ? firstSize : secondSize;
                    if (std::abs(fixedActual - node->fixedSize) > 2.5f) {
                        std::ostringstream oss;
                        oss << label << " fixed split drift [FAIL] expected=" << node->fixedSize
                            << " actual=" << fixedActual;
                        eventConsole_.logAutomation(oss.str());
                        ++failures;
                    }
                }

                float minFirst = std::clamp(node->minFirstSize, 0.0f, std::max(0.0f, total));
                float minSecond = std::clamp(node->minSecondSize, 0.0f, std::max(0.0f, total));
                const float minSum = minFirst + minSecond;
                if (total > 0.0f && minSum > total) {
                    const float scale = total / minSum;
                    minFirst *= scale;
                    minSecond *= scale;
                }
                float maxFirst = std::max(0.0f, total - minSecond);
                if (maxFirst < minFirst) {
                    maxFirst = minFirst;
                }
                if (firstSize + 2.5f < minFirst || firstSize - 2.5f > maxFirst) {
                    std::ostringstream oss;
                    oss << label << " split violates min clamps [FAIL]"
                        << " first=" << firstSize
                        << " minFirst=" << minFirst
                        << " maxFirst=" << maxFirst
                        << " total=" << total;
                    eventConsole_.logAutomation(oss.str());
                    ++failures;
                }

                if (total > 1.0f) {
                    const float expectedRatio = firstSize / total;
                    if (std::abs(node->ratio - expectedRatio) > 0.08f) {
                        std::ostringstream oss;
                        oss << label << " split ratio drift [FAIL] expected=" << expectedRatio
                            << " actual=" << node->ratio;
                        eventConsole_.logAutomation(oss.str());
                        ++failures;
                    }
                }

                if (node->vertical) {
                    const float seam = node->first->bounds.x + node->first->bounds.width;
                    if (std::abs(seam - node->second->bounds.x) > 2.5f) {
                        std::ostringstream oss;
                        oss << label << " vertical split seam mismatch [FAIL] seam=" << seam
                            << " second.x=" << node->second->bounds.x;
                        eventConsole_.logAutomation(oss.str());
                        ++failures;
                    }
                } else {
                    const float seam = node->first->bounds.y + node->first->bounds.height;
                    if (std::abs(seam - node->second->bounds.y) > 2.5f) {
                        std::ostringstream oss;
                        oss << label << " horizontal split seam mismatch [FAIL] seam=" << seam
                            << " second.y=" << node->second->bounds.y;
                        eventConsole_.logAutomation(oss.str());
                        ++failures;
                    }
                }
            }

            if (node->type == df::DockLayout::Node::Type::Tab && !node->children.empty()) {
                float maxChildMinW = 0.0f;
                float maxChildMinH = 0.0f;
                const int activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
                const bool verticalStrip = df::DockLayout::UseVerticalTabStrip(*node, b);
                const DFRect strip = df::DockLayout::TabStripRect(*node, b);
                const DFRect content = verticalStrip
                    ? DFRect{
                        b.x + strip.width,
                        b.y,
                        std::max(0.0f, b.width - strip.width),
                        b.height
                    }
                    : DFRect{
                        b.x,
                        b.y + strip.height,
                        b.width,
                        std::max(0.0f, b.height - strip.height)
                    };

                for (size_t i = 0; i < node->children.size(); ++i) {
                    const auto& child = node->children[i];
                    if (!child) {
                        continue;
                    }
                    maxChildMinW = std::max(maxChildMinW, child->calculatedMinWidth);
                    maxChildMinH = std::max(maxChildMinH, child->calculatedMinHeight);
                    const DFRect cb = child->bounds;
                    if (static_cast<int>(i) == activeTab) {
                        if (std::abs(cb.x - content.x) > 2.5f ||
                            std::abs(cb.y - content.y) > 2.5f ||
                            std::abs(cb.width - content.width) > 2.5f ||
                            std::abs(cb.height - content.height) > 2.5f) {
                            std::ostringstream oss;
                            oss << label << " active tab content bounds mismatch [FAIL]"
                                << " child=(" << cb.x << "," << cb.y << " " << cb.width << "x" << cb.height << ")"
                                << " expected=(" << content.x << "," << content.y << " " << content.width << "x" << content.height << ")";
                            eventConsole_.logAutomation(oss.str());
                            ++failures;
                        }
                    } else {
                        // Inactive tab children are collapsed by layout.
                        if (cb.width > 1.0f || cb.height > 1.0f) {
                            std::ostringstream oss;
                            oss << label << " inactive tab child should be collapsed [FAIL]"
                                << " child=(" << cb.x << "," << cb.y << " " << cb.width << "x" << cb.height << ")";
                            eventConsole_.logAutomation(oss.str());
                            ++failures;
                        }
                    }
                }
                if (node->calculatedMinWidth + 1.0f < maxChildMinW) {
                    std::ostringstream oss;
                    oss << label << " tab min width too small [FAIL]"
                        << " tabMin=" << node->calculatedMinWidth
                        << " childMax=" << maxChildMinW;
                    eventConsole_.logAutomation(oss.str());
                    ++failures;
                }
                const float tabBarMin = std::max(0.0f, node->tabBarHeight);
                const float expectedTabMinH = maxChildMinH + tabBarMin;
                if (node->calculatedMinHeight + 1.0f < expectedTabMinH) {
                    std::ostringstream oss;
                    oss << label << " tab min height too small [FAIL]"
                        << " tabMin=" << node->calculatedMinHeight
                        << " expected=" << expectedTabMinH;
                    eventConsole_.logAutomation(oss.str());
                    ++failures;
                }
            }

            if (node->type == df::DockLayout::Node::Type::Widget && node->widget) {
                const DFRect wb = node->widget->bounds();
                if (std::abs(wb.x - b.x) > 2.5f ||
                    std::abs(wb.y - b.y) > 2.5f ||
                    std::abs(wb.width - b.width) > 2.5f ||
                    std::abs(wb.height - b.height) > 2.5f) {
                    std::ostringstream oss;
                    oss << label << " widget/node bounds diverged [FAIL]";
                    eventConsole_.logAutomation(oss.str());
                    ++failures;
                }
            }

            self(self, node->first.get(), b, true);
            self(self, node->second.get(), b, true);
            for (auto& child : node->children) {
                self(self, child.get(), b, true);
            }
        };

        validateNodeRecursive(validateNodeRecursive, root, viewportRect, false);

        for (const auto& widget : widgets_) {
            if (!widget || widget->isFloating()) {
                continue;
            }
            const DFRect b = widget->bounds();
            // Hidden/placeholder panels can transiently report zero bounds.
            if (b.width <= 1.0f || b.height <= 1.0f) {
                continue;
            }
            const DFSize widgetMin = widget->minimumSize();
            if (strictWidth && b.width + 1.0f < widgetMin.width) {
                std::ostringstream oss;
                oss << label << " panel width below minimum: " << widget->title()
                    << " width=" << b.width << " min=" << widgetMin.width << " [FAIL]";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }
            if (strictHeight && b.height + 1.0f < widgetMin.height) {
                std::ostringstream oss;
                oss << label << " panel height below minimum: " << widget->title()
                    << " height=" << b.height << " min=" << widgetMin.height << " [FAIL]";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }
            const float minVisualHeight = kEnableTabUi ? 72.0f : 48.0f;
            if (b.width < 96.0f || b.height < minVisualHeight) {
                std::ostringstream oss;
                oss << label << " panel squashed: " << widget->title()
                    << " size=" << b.width << "x" << b.height << " [FAIL]";
                eventConsole_.logAutomation(oss.str());
                ++failures;
            }
        }
        // No fixed "left panel" invariant: panels can undock/re-dock and tabify.
    };
    auto collectSplitNodes =
        [&](auto&& self, df::DockLayout::Node* node, std::vector<df::DockLayout::Node*>& out) -> void {
        if (!node) return;
        if (node->type == df::DockLayout::Node::Type::Split && node->first && node->second) {
            out.push_back(node);
        }
        self(self, node->first.get(), out);
        self(self, node->second.get(), out);
        for (auto& child : node->children) {
            self(self, child.get(), out);
        }
    };
    auto runWidgetDrag = [&](int idx, float moveX, float moveY) {
        const DFPoint anchor = dragAnchorForWidget(widgets_[0].get());
        const float startX = anchor.x;
        const float startY = anchor.y;
        const std::string downLabel = "widget_drag_down_" + std::to_string(idx);
        const std::string moveLabel = "widget_drag_move_" + std::to_string(idx);
        const std::string upLabel = "widget_drag_up_" + std::to_string(idx);
        failures += injectEvent(Event::Type::MouseDown, startX, startY, "widget:|dock_drag", downLabel.c_str()) ? 0 : 1;
        failures += injectEvent(Event::Type::MouseMove, moveX, moveY, "dock_drag|floating_drag", moveLabel.c_str()) ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, moveX, moveY, "dock_drag|floating_drag", upLabel.c_str()) ? 0 : 1;
    };

    auto runSplitterDrag = [&](int idx, float moveX) {
        refreshLayoutState();
        if (!layout_.root() || layout_.root()->type != df::DockLayout::Node::Type::Split) {
            eventConsole_.logAutomation("splitter drag skipped (no split root)");
            return;
        }
        const DFRect currentLeft = widgets_[0]->bounds();
        const float splitterX = currentLeft.x + currentLeft.width;
        const float splitterY = SafeClamp(currentLeft.y + 120.0f, currentLeft.y + 30.0f, currentLeft.y + currentLeft.height - 30.0f);
        const std::string downLabel = "splitter_drag_down_" + std::to_string(idx);
        const std::string moveLabel = "splitter_drag_move_" + std::to_string(idx);
        const std::string upLabel = "splitter_drag_up_" + std::to_string(idx);
        if (!injectEvent(Event::Type::MouseDown, splitterX, splitterY, "splitter", downLabel.c_str())) {
            eventConsole_.logAutomation("splitter drag skipped (splitter hit test miss)");
            return;
        }
        failures += injectEvent(Event::Type::MouseMove, moveX, splitterY, "splitter", moveLabel.c_str()) ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, moveX, splitterY, "splitter", upLabel.c_str()) ? 0 : 1;
    };

    auto runTabCheck = [&]() {
        refreshLayoutState();
        if (tabVisuals_.empty()) {
            eventConsole_.logAutomation("tab_visuals missing [FAIL]");
            ++failures;
            return;
        }
        const auto tab = tabVisuals_.front();
        if (tab.tabRects.size() < 2) {
            eventConsole_.logAutomation("tab_visuals requires at least 2 tabs [FAIL]");
            ++failures;
            return;
        }
        const float tx = tab.tabRects[1].x + tab.tabRects[1].width * 0.5f;
        const float ty = tab.tabRects[1].y + tab.tabRects[1].height * 0.5f;
        failures += injectEvent(Event::Type::MouseDown, tx, ty, "tab:", "tab_switch_down") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, tx, ty, "tab:", "tab_switch_up") ? 0 : 1;

        refreshLayoutState();
        if (tab.node && tab.node->activeTab != 1) {
            eventConsole_.logAutomation("tab active index did not switch [FAIL]");
            ++failures;
        }

        // Drag active tab out of the strip; should undock to floating window.
        int floatingBefore = 0;
        for (const auto& widget : widgets_) {
            if (widget->isFloating()) {
                ++floatingBefore;
            }
        }

        const float dragStartX = tx;
        const float dragStartY = ty;
        const float dragMoveX = tx + 120.0f;
        const float dragMoveY = ty - 100.0f;
        failures += injectEvent(Event::Type::MouseDown, dragStartX, dragStartY, "tab:", "tab_undock_down") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseMove, dragMoveX, dragMoveY, "tab:", "tab_undock_move") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, dragMoveX, dragMoveY, "floating_drag", "tab_undock_up") ? 0 : 1;

        int floatingCount = 0;
        for (const auto& widget : widgets_) {
            if (widget->isFloating()) {
                ++floatingCount;
            }
        }
        if (floatingCount < floatingBefore + 1) {
            // With inner-split docking, a fast release can undock and immediately
            // redock into a local split target. Treat this as valid behavior.
            eventConsole_.logAutomation("tab undock auto-docked during release (accepted by recursive docking)");
        }
    };

    auto runFloatingDragCheck = [&]() {
        if (!floatingWindow_) {
            eventConsole_.logAutomation("floating window missing [FAIL]");
            ++failures;
            return;
        }
        const DFRect before = floatingWindow_->bounds();
        const float startX = before.x + 25.0f;
        const float startY = before.y + 10.0f;
        const float moveX = startX + 65.0f;
        const float moveY = startY + 35.0f;
        failures += injectEvent(Event::Type::MouseDown, startX, startY, "floating_", "floating_drag_down") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseMove, moveX, moveY, "floating_", "floating_drag_move") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, moveX, moveY, "floating_", "floating_drag_up") ? 0 : 1;

        if (!floatingWindow_) {
            eventConsole_.logAutomation("floating window docked during move (accepted by recursive docking)");
            return;
        }
        const DFRect after = floatingWindow_->bounds();
        const DFRect work = df::WindowManager::instance().workArea();
        if (after.x < work.x || after.y < work.y ||
            after.x + after.width > work.x + work.width ||
            after.y + after.height > work.y + work.height) {
            eventConsole_.logAutomation("floating window moved out of bounds [FAIL]");
            ++failures;
        }
    };

    auto runFloatingRedockCheck = [&]() {
        refreshLayoutState();
        if (!floatingWindow_ || widgets_.size() < 5) {
            eventConsole_.logAutomation("floating redock skipped (no base floating window)");
            return;
        }

        const DFRect before = floatingWindow_->bounds();
        const float startX = before.x + 20.0f;
        const float startY = before.y + 10.0f;
        // Edge-triggered docking: move near the container edge to show hint.
        const float dropX = std::max(0.5f, viewport_.Width * 0.001f);
        const float dropY = SafeClamp(viewport_.Height * 0.5f, 8.0f, viewport_.Height - 8.0f);

        failures += injectEvent(Event::Type::MouseDown, startX, startY, "floating_", "floating_redock_down") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseMove, dropX, dropY, "floating_", "floating_redock_move") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, dropX, dropY, "floating_", "floating_redock_up") ? 0 : 1;

        if (widgets_[4]->isFloating()) {
            eventConsole_.logAutomation("floating redock did not dock profiler widget [FAIL]");
            ++failures;
        }
    };

    auto runDockBoundsCheck = [&]() {
        refreshLayoutState();
        df::DockWidget* target = pickDockedWidgetForInteraction();
        if (!target) {
            eventConsole_.logAutomation("dock_bounds no docked widget available [FAIL]");
            ++failures;
            return;
        }
        const DFPoint anchor = dragAnchorForWidget(target);
        const float startX = anchor.x;
        const float startY = anchor.y;
        failures += injectEvent(Event::Type::MouseDown, startX, startY, "widget:|dock_drag", "dock_bounds_down") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseMove, -600.0f, -400.0f, "dock_drag|floating_drag", "dock_bounds_move") ? 0 : 1;
        failures += injectEvent(Event::Type::MouseUp, -600.0f, -400.0f, "dock_drag|floating_drag", "dock_bounds_up") ? 0 : 1;

        if (target->isFloating()) {
            eventConsole_.logAutomation("dock_bounds target floated (accepted)");
            return;
        }

        const DFRect after = target->bounds();
        if (after.x < 0.0f || after.y < 0.0f ||
            after.x + after.width > viewport_.Width ||
            after.y + after.height > viewport_.Height) {
            eventConsole_.logAutomation("dock widget out of bounds after drag [FAIL]");
            ++failures;
        }
    };

    auto runResizeSyncCheck = [&]() {
        const UINT oldW = static_cast<UINT>(viewport_.Width);
        const UINT oldH = static_cast<UINT>(viewport_.Height);
        const UINT testW = std::max<UINT>(640, oldW > 220 ? oldW - 220 : oldW);
        const UINT testH = std::max<UINT>(420, oldH > 180 ? oldH - 180 : oldH);

        handleResize(testW, testH);
        refreshLayoutState();
        validatePanelSizes("resize_sync");

        if (static_cast<UINT>(viewport_.Width) != testW || static_cast<UINT>(viewport_.Height) != testH) {
            eventConsole_.logAutomation("resize sync failed to update viewport [FAIL]");
            ++failures;
        }

        df::DockWidget* target = pickDockedWidgetForInteraction();
        if (!target) {
            eventConsole_.logAutomation("resize_sync no docked widget available [FAIL]");
            ++failures;
            handleResize(oldW, oldH);
            refreshLayoutState();
            return;
        }
        const DFRect b = target->bounds();
        if (b.x < 0.0f || b.y < 0.0f ||
            b.x + b.width > viewport_.Width ||
            b.y + b.height > viewport_.Height) {
            eventConsole_.logAutomation("resize sync produced out-of-bounds dock widget [FAIL]");
            ++failures;
        }

        const DFPoint anchor = dragAnchorForWidget(target);
        const float sx = anchor.x;
        const float sy = anchor.y;
        if (!injectEvent(Event::Type::MouseDown, sx, sy, "widget:|dock_drag", "resize_sync_down")) {
            eventConsole_.logAutomation("resize_sync drag check skipped (widget obscured)");
        } else {
            failures += injectEvent(Event::Type::MouseMove, sx + 25.0f, sy + 15.0f, "dock_drag|floating_drag", "resize_sync_move") ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, sx + 25.0f, sy + 15.0f, "dock_drag|floating_drag", "resize_sync_up") ? 0 : 1;
        }

        handleResize(oldW, oldH);
        refreshLayoutState();
        validatePanelSizes("resize_sync_restore");
    };

    auto runResizeDuringActionCheck = [&]() {
        const UINT oldW = static_cast<UINT>(viewport_.Width);
        const UINT oldH = static_cast<UINT>(viewport_.Height);
        const UINT testW = std::max<UINT>(640, oldW > 180 ? oldW - 180 : oldW);
        const UINT testH = std::max<UINT>(420, oldH > 140 ? oldH - 140 : oldH);

        refreshLayoutState();
        df::DockWidget* target = pickDockedWidgetForInteraction();
        if (!target) {
            eventConsole_.logAutomation("resize_action no docked widget available [FAIL]");
            ++failures;
            return;
        }
        const DFPoint anchor = dragAnchorForWidget(target);
        const float sx = anchor.x;
        const float sy = anchor.y;

        if (!injectEvent(Event::Type::MouseDown, sx, sy, "widget:|dock_drag", "resize_action_down")) {
            eventConsole_.logAutomation("resize_action drag check skipped (widget obscured)");
            handleResize(oldW, oldH);
            refreshLayoutState();
            validatePanelSizes("resize_action_restore");
            return;
        }
        handleResize(testW, testH);
        refreshLayoutState();
        validatePanelSizes("resize_action");

        if (activeAction_ != ActionOwner::None ||
            df::DockManager::instance().isDragging() ||
            splitter_.isDragging() ||
            df::WindowManager::instance().hasDraggingWindow()) {
            eventConsole_.logAutomation("resize during action left stale drag state [FAIL]");
            ++failures;
        }

        // Recovery check: use post-resize coordinates to avoid stale-cursor assumptions.
        const DFPoint recoverAnchor = dragAnchorForWidget(target);
        const float rsx = recoverAnchor.x;
        const float rsy = recoverAnchor.y;
        if (!injectEvent(Event::Type::MouseDown, rsx, rsy, "widget:|dock_drag", "resize_action_recover_down")) {
            eventConsole_.logAutomation("resize action recovery skipped (widget obscured)");
        } else {
            failures += injectEvent(Event::Type::MouseMove, rsx + 28.0f, rsy + 14.0f, "dock_drag|floating_drag", "resize_action_recover_move") ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, rsx + 28.0f, rsy + 14.0f, "dock_drag|floating_drag", "resize_action_recover_up") ? 0 : 1;
        }

        const DFRect after = target->bounds();
        if (after.x < 0.0f || after.y < 0.0f ||
            after.x + after.width > viewport_.Width ||
            after.y + after.height > viewport_.Height) {
            eventConsole_.logAutomation("resize during action left dock widget out of bounds [FAIL]");
            ++failures;
        }

        handleResize(oldW, oldH);
        refreshLayoutState();
        validatePanelSizes("resize_action_restore");
    };

    auto runLiveResizeFlowCheck = [&]() {
        const UINT oldW = static_cast<UINT>(viewport_.Width);
        const UINT oldH = static_cast<UINT>(viewport_.Height);
        const UINT testW = std::max<UINT>(700, oldW > 190 ? oldW - 190 : oldW);
        const UINT testH = std::max<UINT>(460, oldH > 130 ? oldH - 130 : oldH);

        inSizeMove_ = true;
        resizing_ = true;
        hasPendingResize_ = true;
        pendingResizeW_ = testW;
        pendingResizeH_ = testH;

        Event blockedMove(Event::Type::MouseMove);
        blockedMove.x = SafeClamp(lastMousePos_.x + 14.0f, 0.0f, viewport_.Width);
        blockedMove.y = SafeClamp(lastMousePos_.y + 8.0f, 0.0f, viewport_.Height);
        processEvent(blockedMove);
        if (lastDispatchHandler_ != "resize_blocked") {
            eventConsole_.logAutomation("live resize flow did not block event [FAIL]");
            ++failures;
        }

        inSizeMove_ = false;
        resizing_ = false;
        if (hasPendingResize_) {
            const UINT w = pendingResizeW_;
            const UINT h = pendingResizeH_;
            hasPendingResize_ = false;
            handleResize(w, h);
        }
        refreshLayoutState();
        validatePanelSizes("live_resize_flow");

        if (static_cast<UINT>(viewport_.Width) != testW || static_cast<UINT>(viewport_.Height) != testH) {
            eventConsole_.logAutomation("live resize flow failed to apply pending size [FAIL]");
            ++failures;
        }

        handleResize(oldW, oldH);
        refreshLayoutState();
        validatePanelSizes("live_resize_flow_restore");
    };

    auto runGlobalFloatingSyncCheck = [&]() {
        df::WindowFrame* probeWindow = floatingWindow_;
        if (!probeWindow) {
            for (const auto& widget : widgets_) {
                if (!widget || !widget->isFloating()) {
                    continue;
                }
                probeWindow = df::WindowManager::instance().findWindowByContent(widget.get());
                if (probeWindow) {
                    break;
                }
            }
        }
        if (!probeWindow) {
            eventConsole_.logAutomation("global_sync skipped (no floating window)");
            return;
        }
        if (probeWindow->content() &&
            probeWindow->content()->hostType() != df::DockWidget::HostType::FloatingWindow) {
            eventConsole_.logAutomation("global_sync invalid floating host type [FAIL]");
            ++failures;
        }

        const DFPoint beforeOrigin = df::WindowManager::instance().clientOriginScreen();
        const DFRect beforeGlobal = probeWindow->globalBounds();

        // Simulate host-window client-origin movement and verify floating global stays stable.
        const DFPoint shiftedOrigin{beforeOrigin.x + 73.0f, beforeOrigin.y + 41.0f};
        df::WindowManager::instance().setClientOriginScreen(shiftedOrigin);
        const DFRect shiftedGlobal = probeWindow->globalBounds();
        const float globalDrift = std::fabs(shiftedGlobal.x - beforeGlobal.x) +
            std::fabs(shiftedGlobal.y - beforeGlobal.y);
        if (globalDrift > 0.5f) {
            eventConsole_.logAutomation("global sync drift while origin changed [FAIL]");
            ++failures;
        }

        // Restore real origin for subsequent checks.
        df::WindowManager::instance().setClientOriginScreen(beforeOrigin);
        const DFRect restoredGlobal = probeWindow->globalBounds();
        const float restoreDrift = std::fabs(restoredGlobal.x - beforeGlobal.x) +
            std::fabs(restoredGlobal.y - beforeGlobal.y);
        if (restoreDrift > 0.5f) {
            eventConsole_.logAutomation("global sync restore drift [FAIL]");
            ++failures;
        }

        // Docked widgets must stay docked-layout hosted after sync operations.
        bool foundDockedWidget = false;
        for (const auto& widget : widgets_) {
            if (!widget || widget->isFloating()) {
                continue;
            }
            const DFRect b = widget->bounds();
            if (b.width <= 1.0f || b.height <= 1.0f) {
                continue;
            }
            foundDockedWidget = true;
            if (widget->hostType() != df::DockWidget::HostType::DockedLayout) {
                eventConsole_.logAutomation("global_sync invalid docked host type [FAIL]");
                ++failures;
            }
            break;
        }
    };

    auto runHostTransferStressCheck = [&]() {
        refreshLayoutState();
        if (widgets_.empty() || !widgets_[0]) {
            eventConsole_.logAutomation("host_transfer missing base widget [FAIL]");
            ++failures;
            return;
        }

        auto pickDockTargetPoint = [&](df::DockWidget* exclude) -> DFPoint {
            for (const auto& candidate : widgets_) {
                if (!candidate || candidate.get() == exclude || candidate->isFloating()) {
                    continue;
                }
                const DFRect b = candidate->bounds();
                if (b.width <= 16.0f || b.height <= 16.0f) {
                    continue;
                }
                const float headerY = b.y + std::min(12.0f, std::max(1.0f, b.height * 0.5f));
                return {
                    SafeClamp(b.x + b.width * 0.5f, 8.0f, viewport_.Width - 8.0f),
                    SafeClamp(headerY, 8.0f, viewport_.Height - 8.0f)
                };
            }
            return {
                SafeClamp(viewport_.Width * 0.5f, 8.0f, viewport_.Width - 8.0f),
                SafeClamp(12.0f, 8.0f, viewport_.Height - 8.0f)
            };
        };
        auto attemptEdgeRedock = [&](df::DockWidget* widget, const std::string& tag) -> bool {
            if (!widget || !widget->isFloating() || !widget->parentWindow()) {
                return widget && !widget->isFloating();
            }
            auto* win = widget->parentWindow();
            const DFRect wb = win->bounds();
            const float sx = wb.x + 18.0f;
            const float sy = wb.y + 10.0f;
            const float edgeX = std::max(0.5f, viewport_.Width * 0.001f);
            const float edgeY = SafeClamp(viewport_.Height * 0.5f, 8.0f, viewport_.Height - 8.0f);
            failures += injectEvent(Event::Type::MouseDown, sx, sy, "floating_", (tag + "_edge_down").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseMove, edgeX, edgeY, "floating_", (tag + "_edge_move").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, edgeX, edgeY, "floating_", (tag + "_edge_up").c_str()) ? 0 : 1;
            refreshLayoutState();
            return !widget->isFloating();
        };

        auto runTransferFor = [&](df::DockWidget* testWidget, const char* tag) {
            if (!testWidget) {
                eventConsole_.logAutomation(std::string(tag) + " missing widget [FAIL]");
                ++failures;
                return;
            }

            if (testWidget->isFloating()) {
                eventConsole_.logAutomation(std::string(tag) + " precondition floating: attempting re-dock");
                if (auto* win = df::WindowManager::instance().findWindowByContent(testWidget)) {
                    const DFRect wb = win->bounds();
                    const float sx = wb.x + 18.0f;
                    const float sy = wb.y + 10.0f;
                    const DFPoint target = pickDockTargetPoint(testWidget);
                    const float dx = target.x;
                    const float dy = target.y;
                    failures += injectEvent(Event::Type::MouseDown, sx, sy, "floating_", (std::string(tag) + "_predock_down").c_str()) ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseMove, dx, dy, "floating_", (std::string(tag) + "_predock_move").c_str()) ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseUp, dx, dy, "floating_", (std::string(tag) + "_predock_up").c_str()) ? 0 : 1;
                    refreshLayoutState();
                }
            }

            // 1) Undock from docked layout by dragging a tab-aware anchor.
            const DFPoint anchor = dragAnchorForWidget(testWidget);
            const float dragX = anchor.x + 160.0f;
            const float dragY = SafeClamp(anchor.y + 80.0f, 0.0f, viewport_.Height);
            failures += injectEvent(Event::Type::MouseDown, anchor.x, anchor.y, "widget:|dock_drag", (std::string(tag) + "_undock_down").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseMove, dragX, dragY, "dock_drag|floating_drag", (std::string(tag) + "_undock_move").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, dragX, dragY, "dock_drag|floating_drag", (std::string(tag) + "_undock_up").c_str()) ? 0 : 1;

            if (!testWidget->isFloating()) {
                eventConsole_.logAutomation(std::string(tag) + " undock failed [FAIL]");
                ++failures;
                return;
            }
            if (testWidget->hostType() != df::DockWidget::HostType::FloatingWindow) {
                eventConsole_.logAutomation(std::string(tag) + " invalid host type after undock [FAIL]");
                ++failures;
            }
            if (testWidget->parentWindow() == nullptr) {
                eventConsole_.logAutomation(std::string(tag) + " missing parent window after undock [FAIL]");
                ++failures;
                return;
            }

            // 2) Re-dock by dragging floating title into main area center.
            auto* win = testWidget->parentWindow();
            const DFRect wb = win->bounds();
            const float sx = wb.x + 18.0f;
            const float sy = wb.y + 10.0f;
            const DFPoint target = pickDockTargetPoint(testWidget);
            const float dx = target.x;
            const float dy = target.y;
            failures += injectEvent(Event::Type::MouseDown, sx, sy, "floating_", (std::string(tag) + "_redock_down").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseMove, dx, dy, "floating_", (std::string(tag) + "_redock_move").c_str()) ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, dx, dy, "floating_", (std::string(tag) + "_redock_up").c_str()) ? 0 : 1;
            refreshLayoutState();
            if (testWidget->isFloating()) {
                eventConsole_.logAutomation(std::string(tag) + " center redock missed, retrying edge dock");
                attemptEdgeRedock(testWidget, std::string(tag));
            }

            if (testWidget->isFloating()) {
                eventConsole_.logAutomation(std::string(tag) + " redock failed [FAIL]");
                ++failures;
            }
            if (testWidget->hostType() != df::DockWidget::HostType::DockedLayout) {
                eventConsole_.logAutomation(std::string(tag) + " invalid host type after redock [FAIL]");
                ++failures;
            }
            if (testWidget->parentWindow() != nullptr) {
                eventConsole_.logAutomation(std::string(tag) + " stale parent window after redock [FAIL]");
                ++failures;
            }
        };

        // Validate a full undock/redock cycle on a normal docked widget.
        runTransferFor(widgets_[0].get(), "host_transfer_hierarchy");

        // Validate Profiler can dock back from floating state.
        if (widgets_.size() > 4) {
            df::DockWidget* profilerWidget = widgets_[4].get();
            if (profilerWidget && profilerWidget->isFloating()) {
                auto* win = profilerWidget->parentWindow();
                if (win) {
                    const DFRect wb = win->bounds();
                    const float sx = wb.x + 18.0f;
                    const float sy = wb.y + 10.0f;
                    const DFPoint target = pickDockTargetPoint(profilerWidget);
                    const float dx = target.x;
                    const float dy = target.y;
                    failures += injectEvent(Event::Type::MouseDown, sx, sy, "floating_", "host_transfer_profiler_redock_down") ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseMove, dx, dy, "floating_", "host_transfer_profiler_redock_move") ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseUp, dx, dy, "floating_", "host_transfer_profiler_redock_up") ? 0 : 1;
                    refreshLayoutState();
                    if (profilerWidget->isFloating()) {
                        eventConsole_.logAutomation("host_transfer_profiler center redock missed, retrying edge dock");
                        attemptEdgeRedock(profilerWidget, "host_transfer_profiler");
                    }
                }
            }

            if (profilerWidget && profilerWidget->isFloating()) {
                eventConsole_.logAutomation("host_transfer_profiler redock failed [FAIL]");
                ++failures;
            }
            if (profilerWidget && profilerWidget->hostType() != df::DockWidget::HostType::DockedLayout) {
                eventConsole_.logAutomation("host_transfer_profiler invalid host type after redock [FAIL]");
                ++failures;
            }
            if (profilerWidget && profilerWidget->parentWindow() != nullptr) {
                eventConsole_.logAutomation("host_transfer_profiler stale parent window after redock [FAIL]");
                ++failures;
            }
        }
    };

    auto runCloseAllWidgetsCheck = [&]() {
        clearActiveAction();
        for (auto& widget : widgets_) {
            if (!widget) continue;
            df::DockManager::instance().closeWidget(widget.get());
        }
        floatingWindow_ = nullptr;
        activeWindow_ = nullptr;
        refreshLayoutState();

        if (layout_.root() != nullptr) {
            eventConsole_.logAutomation("close_all root not cleared [FAIL]");
            ++failures;
        }
        if (!tabVisuals_.empty()) {
            eventConsole_.logAutomation("close_all tab visuals still present [FAIL]");
            ++failures;
        }

        const DFPoint probe{
            SafeClamp(viewport_.Width * 0.5f, 0.0f, viewport_.Width),
            SafeClamp(viewport_.Height * 0.5f, 0.0f, viewport_.Height)
        };
        if (splitter_.splitterAtPoint(probe) != nullptr) {
            eventConsole_.logAutomation("close_all splitter hit remains [FAIL]");
            ++failures;
        }

        Event probeEvent(Event::Type::MouseMove);
        probeEvent.x = probe.x;
        probeEvent.y = probe.y;
        processEvent(probeEvent);
        if (lastDispatchHandler_ == "splitter") {
            eventConsole_.logAutomation("close_all dispatch still routed to splitter [FAIL]");
            ++failures;
        }

        int visibleDocked = 0;
        for (const auto& widget : widgets_) {
            if (!widget || widget->isFloating()) continue;
            const DFRect b = widget->bounds();
            if (b.width > 1.0f && b.height > 1.0f) {
                ++visibleDocked;
            }
        }
        if (visibleDocked > 0) {
            eventConsole_.logAutomation("close_all visible docked widgets remain [FAIL]");
            ++failures;
        }
    };
    auto runRecursiveConstraintsStressCheck = [&]() {
        eventConsole_.logAutomation("recursive_constraints_stress begin");
        const UINT baseW = static_cast<UINT>(viewport_.Width);
        const UINT baseH = static_cast<UINT>(viewport_.Height);
        auto nextUnit = [&]() -> float {
            fuzzSeed = fuzzSeed * 1664525u + 1013904223u;
            return static_cast<float>(fuzzSeed & 0x00FFFFFFu) / 16777215.0f;
        };

        for (int i = 0; i < fuzzIterations; ++i) {
            const float widthScale = (i % 5 == 0)
                ? (0.42f + 0.12f * nextUnit()) // force periodic compression paths
                : (0.62f + 0.36f * nextUnit());
            const float heightScale = 0.56f + 0.40f * nextUnit();
            const UINT w = std::max<UINT>(420, static_cast<UINT>(baseW * widthScale));
            const UINT h = std::max<UINT>(320, static_cast<UINT>(baseH * heightScale));

            handleResize(w, h);
            refreshLayoutState();
            validatePanelSizes("recursive_constraints_resize");

            // Random tab activation to exercise tab min propagation and collapsed siblings.
            if (!tabVisuals_.empty()) {
                const size_t start = static_cast<size_t>(nextUnit() * tabVisuals_.size()) % tabVisuals_.size();
                for (size_t attempt = 0; attempt < tabVisuals_.size(); ++attempt) {
                    const size_t visualIndex = (start + attempt) % tabVisuals_.size();
                    const auto& visual = tabVisuals_[visualIndex];
                    if (visual.tabRects.size() < 2) {
                        continue;
                    }
                    const size_t tabIndex =
                        static_cast<size_t>(nextUnit() * visual.tabRects.size()) % visual.tabRects.size();
                    const DFRect tabRect = visual.tabRects[tabIndex];
                    const float tx = tabRect.x + tabRect.width * 0.5f;
                    const float ty = tabRect.y + tabRect.height * 0.5f;
                    const std::string base = "recursive_constraints_tab_" + std::to_string(i);
                    failures += injectEvent(Event::Type::MouseDown, tx, ty, "tab:", (base + "_down").c_str()) ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseUp, tx, ty, "tab:", (base + "_up").c_str()) ? 0 : 1;
                    refreshLayoutState();
                    validatePanelSizes("recursive_constraints_tab");
                    break;
                }
            }

            // Random splitter drags across deep split trees.
            refreshLayoutState();
            std::vector<df::DockLayout::Node*> splitNodes;
            collectSplitNodes(collectSplitNodes, layout_.root(), splitNodes);
            if (!splitNodes.empty()) {
                df::DockLayout::Node* split =
                    splitNodes[static_cast<size_t>(nextUnit() * splitNodes.size()) % splitNodes.size()];
                float downX = 0.0f;
                float downY = 0.0f;
                float moveX = 0.0f;
                float moveY = 0.0f;

                if (split->vertical) {
                    const float seamX = split->first
                        ? (split->first->bounds.x + split->first->bounds.width)
                        : (split->bounds.x + split->bounds.width * 0.5f);
                    downX = seamX;
                    const float lowY = split->bounds.y + std::min(32.0f, std::max(4.0f, split->bounds.height * 0.25f));
                    downY = SafeClamp(
                        split->bounds.y + split->bounds.height * (0.10f + 0.80f * nextUnit()),
                        lowY,
                        split->bounds.y + std::max(4.0f, split->bounds.height - 4.0f));
                    moveX = split->bounds.x + split->bounds.width * (0.02f + 0.96f * nextUnit());
                    moveY = downY;
                } else {
                    const float seamY = split->first
                        ? (split->first->bounds.y + split->first->bounds.height)
                        : (split->bounds.y + split->bounds.height * 0.5f);
                    const float lowX = split->bounds.x + std::min(32.0f, std::max(4.0f, split->bounds.width * 0.25f));
                    downX = SafeClamp(
                        split->bounds.x + split->bounds.width * (0.10f + 0.80f * nextUnit()),
                        lowX,
                        split->bounds.x + std::max(4.0f, split->bounds.width - 4.0f));
                    downY = seamY;
                    moveX = downX;
                    moveY = split->bounds.y + split->bounds.height * (0.02f + 0.96f * nextUnit());
                }

                const std::string base = "recursive_constraints_split_" + std::to_string(i);
                auto* hitSplitter = splitter_.splitterAtPoint({downX, downY});
                if (!hitSplitter || hitSplitter->node != split) {
                    eventConsole_.logAutomation(base + " hit-test miss (skip)");
                    continue;
                }
                if (injectEvent(Event::Type::MouseDown, downX, downY, "splitter", (base + "_down").c_str())) {
                    failures += injectEvent(Event::Type::MouseMove, moveX, moveY, "splitter", (base + "_move").c_str()) ? 0 : 1;
                    failures += injectEvent(Event::Type::MouseUp, moveX, moveY, "splitter", (base + "_up").c_str()) ? 0 : 1;
                    refreshLayoutState();
                    validatePanelSizes("recursive_constraints_split");
                } else {
                    eventConsole_.logAutomation(base + " down missed splitter (layout may have shifted)");
                }
            }
        }

        handleResize(baseW, baseH);
        refreshLayoutState();
        validatePanelSizes("recursive_constraints_restore");
    };

    bool runStandardSuite = true;
    if (scenario == "baseline") {
        runWidgetDrag(0, left.x + 80.0f, left.y + 30.0f);
        runSplitterDrag(0, left.x + left.width + 40.0f);
    } else if (scenario == "splitter_stress") {
        for (int i = 0; i < 8; ++i) {
            const float step = (i % 2 == 0) ? 30.0f : -20.0f;
            const DFRect currentLeft = widgets_[0]->bounds();
            runSplitterDrag(i, currentLeft.x + currentLeft.width + step);
        }
    } else if (scenario == "widget_drag_stress") {
        for (int i = 0; i < 8; ++i) {
            const DFRect current = widgets_[0]->bounds();
            const float targetX = current.x + 35.0f + static_cast<float>(i * 6);
            const float targetY = current.y + 18.0f + static_cast<float>(i * 2);
            runWidgetDrag(i, targetX, targetY);
        }
    } else if (scenario == "mixed") {
        for (int i = 0; i < 4; ++i) {
            runWidgetDrag(i, left.x + 60.0f + static_cast<float>(i * 10), left.y + 30.0f + static_cast<float>(i * 5));
            const DFRect currentLeft = widgets_[0]->bounds();
            runSplitterDrag(i, currentLeft.x + currentLeft.width + ((i % 2 == 0) ? 25.0f : -15.0f));
        }
    } else if (scenario == "resize_stress") {
        for (int i = 0; i < 4; ++i) {
            runResizeSyncCheck();
            runResizeDuringActionCheck();
        }
    } else if (scenario == "resize_crash_stress") {
        const UINT baseW = static_cast<UINT>(viewport_.Width);
        const UINT baseH = static_cast<UINT>(viewport_.Height);
        const UINT minW = 640;
        const UINT minH = 420;
        for (int i = 0; i < 16; ++i) {
            const UINT w = minW + static_cast<UINT>((i * 53) % std::max(1u, baseW - minW + 1u));
            const UINT h = minH + static_cast<UINT>((i * 37) % std::max(1u, baseH - minH + 1u));
            handleResize(w, h);
            refreshLayoutState();
            validatePanelSizes("resize_crash");

            const DFPoint anchor = dragAnchorForWidget(widgets_[0].get());
            const float sx = anchor.x;
            const float sy = anchor.y;
            failures += injectEvent(Event::Type::MouseDown, sx, sy, "widget:|dock_drag", "resize_crash_down") ? 0 : 1;
            failures += injectEvent(Event::Type::MouseMove, sx + 12.0f + static_cast<float>(i), sy + 6.0f, "dock_drag|floating_drag", "resize_crash_move") ? 0 : 1;
            failures += injectEvent(Event::Type::MouseUp, sx + 12.0f + static_cast<float>(i), sy + 6.0f, "dock_drag|floating_drag", "resize_crash_up") ? 0 : 1;
        }
        handleResize(baseW, baseH);
        refreshLayoutState();
    } else if (scenario == "recursive_constraints_stress") {
        runStandardSuite = false;
        runRecursiveConstraintsStressCheck();
        runGlobalFloatingSyncCheck();
    } else if (scenario == "close_all") {
        runCloseAllWidgetsCheck();
    } else if (scenario == "global_float_sync") {
        runStandardSuite = false;
        runGlobalFloatingSyncCheck();
    } else if (scenario == "host_transfer_stress") {
        runStandardSuite = false;
        runHostTransferStressCheck();
        runGlobalFloatingSyncCheck();
    } else {
        eventConsole_.logAutomation(std::string("unknown scenario=") + scenario + " [FAIL]");
        ++failures;
    }

    if (scenario != "close_all" && runStandardSuite) {
        if (kEnableTabUi) {
            runTabCheck();
        }
        runFloatingDragCheck();
        runFloatingRedockCheck();
        runDockBoundsCheck();
        runResizeSyncCheck();
        runResizeDuringActionCheck();
        runLiveResizeFlowCheck();
        runGlobalFloatingSyncCheck();
        validatePanelSizes("final");
    }

    if (renderFramesPostAutomation_ > 0) {
        for (int i = 0; i < renderFramesPostAutomation_; ++i) {
            renderFrame();
            if (eventSleepMs_ > 0) {
                Sleep(static_cast<DWORD>(eventSleepMs_));
            }
        }
    }

    int splitNodes = 0;
    int tabNodes = 0;
    int widgetNodes = 0;
    auto countNodes = [&](auto&& self, df::DockLayout::Node* node) -> void {
        if (!node) return;
        switch (node->type) {
        case df::DockLayout::Node::Type::Split: ++splitNodes; break;
        case df::DockLayout::Node::Type::Tab: ++tabNodes; break;
        case df::DockLayout::Node::Type::Widget: ++widgetNodes; break;
        }
        self(self, node->first.get());
        self(self, node->second.get());
        for (auto& child : node->children) {
            self(self, child.get());
        }
    };
    countNodes(countNodes, layout_.root());
    const float rootMinW = layout_.root() ? layout_.root()->calculatedMinWidth : 0.0f;
    const float rootMinH = layout_.root() ? layout_.root()->calculatedMinHeight : 0.0f;

    const EventConsole::Stats stats = eventConsole_.stats();
    std::ostringstream summary;
    summary << "summary scenario=" << scenario
            << " conflicts=" << stats.conflicts
            << " none=" << stats.handledNone
            << " splitter=" << stats.handledSplitter
            << " dock_drag=" << stats.handledDockDrag
            << " floating=" << stats.handledFloatingWindow
            << " tab=" << stats.handledTab
            << " widget=" << stats.handledWidget
            << " nodes=" << (splitNodes + tabNodes + widgetNodes)
            << " split_nodes=" << splitNodes
            << " tab_nodes=" << tabNodes
            << " widget_nodes=" << widgetNodes
            << " root_min=" << rootMinW << "x" << rootMinH;
    eventConsole_.logAutomation(summary.str());

    if (!eventDurationsMs_.empty()) {
        const double avg = std::accumulate(eventDurationsMs_.begin(), eventDurationsMs_.end(), 0.0) / eventDurationsMs_.size();
        const double p95 = Percentile(eventDurationsMs_, 0.95);
        std::ostringstream perf;
        perf << "perf_events count=" << eventDurationsMs_.size()
             << " avg_ms=" << std::fixed << std::setprecision(3) << avg
             << " p95_ms=" << std::fixed << std::setprecision(3) << p95;
        eventConsole_.logAutomation(perf.str());
    }

    if (!frameTimesMs_.empty()) {
        const double avg = std::accumulate(frameTimesMs_.begin(), frameTimesMs_.end(), 0.0) / frameTimesMs_.size();
        const double p95 = Percentile(frameTimesMs_, 0.95);
        const double avgFps = avg > 0.0 ? 1000.0 / avg : 0.0;
        std::ostringstream perf;
        perf << "perf_frames count=" << frameTimesMs_.size()
             << " avg_ms=" << std::fixed << std::setprecision(3) << avg
             << " p95_ms=" << std::fixed << std::setprecision(3) << p95
             << " avg_fps=" << std::fixed << std::setprecision(1) << avgFps;
        eventConsole_.logAutomation(perf.str());
    }

    if (stats.handledNone > 8) {
        eventConsole_.logAutomation("too many unhandled events detected [FAIL]");
        ++failures;
    }

    if (failures > 0) {
        eventConsole_.logAutomation("automation checks failed");
        return false;
    }

    eventConsole_.logAutomation("automation checks passed");
    return true;
}
