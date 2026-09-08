#pragma once
#include <windows.h>

// Used only by the native repaint regression scenario. Observe real Win32
// messages without changing how the application's window procedures handle them.
class DemoRepaintProbe {
public:
    explicit DemoRepaintProbe(HWND window) : window_(window)
    {
        previous_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window_, GWLP_WNDPROC));
        SetPropW(window_, L"DF_REPAINT_PROBE", this);
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure));
    }
    ~DemoRepaintProbe()
    {
        if (IsWindow(window_)) {
            SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_));
            RemovePropW(window_, L"DF_REPAINT_PROBE");
        }
    }
    DemoRepaintProbe(const DemoRepaintProbe&) = delete;
    DemoRepaintProbe& operator=(const DemoRepaintProbe&) = delete;
    void reset() { captions = positions = paints = 0; }
    unsigned captions = 0;
    unsigned positions = 0;
    unsigned paints = 0;
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* probe = static_cast<DemoRepaintProbe*>(GetPropW(window, L"DF_REPAINT_PROBE"));
        if (!probe) return DefWindowProcW(window, message, wParam, lParam);
        if (message == WM_SETTEXT) ++probe->captions;
        if (message == WM_WINDOWPOSCHANGING) ++probe->positions;
        if (message == WM_PAINT) ++probe->paints;
        return CallWindowProcW(probe->previous_, window, message, wParam, lParam);
    }
    HWND window_;
    WNDPROC previous_ = nullptr;
};
