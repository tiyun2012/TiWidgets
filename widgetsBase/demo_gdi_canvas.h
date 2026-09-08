#pragma once
#include "core_types.h"
#include <windows.h>
#include <cmath>
#include <vector>

// Paint into a private surface, then expose the finished client area in one
// blit. The paint DC is never used for intermediate backgrounds/text strokes.
class DemoBufferedPaint final {
public:
    explicit DemoBufferedPaint(HWND window) : window_(window)
    {
        target_ = BeginPaint(window_, &paint_);
        GetClientRect(window_, &bounds_);
        if (!target_ || bounds_.right <= 0 || bounds_.bottom <= 0) return;
        buffer_ = CreateCompatibleDC(target_);
        bitmap_ = CreateCompatibleBitmap(target_, bounds_.right, bounds_.bottom);
        if (buffer_ && bitmap_) previous_ = SelectObject(buffer_, bitmap_);
    }
    ~DemoBufferedPaint()
    {
        if (previous_ && previous_ != HGDI_ERROR) SelectObject(buffer_, previous_);
        if (bitmap_) DeleteObject(bitmap_);
        if (buffer_) DeleteDC(buffer_);
        if (target_) EndPaint(window_, &paint_);
    }
    DemoBufferedPaint(const DemoBufferedPaint&) = delete;
    DemoBufferedPaint& operator=(const DemoBufferedPaint&) = delete;
    HDC dc() const { return previous_ && previous_ != HGDI_ERROR ? buffer_ : nullptr; }
    void present()
    {
        if (dc()) BitBlt(target_, 0, 0, bounds_.right, bounds_.bottom, buffer_, 0, 0, SRCCOPY);
    }
private:
    HWND window_;
    PAINTSTRUCT paint_{};
    RECT bounds_{};
    HDC target_ = nullptr;
    HDC buffer_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

// Native floating hosts paint the same showcase content through Canvas.
class DemoGdiCanvas final : public Canvas {
public:
    explicit DemoGdiCanvas(HDC dc) : dc_(dc) {}
    void drawRectangle(const DFRect& r, const DFColor& c) override
    {
        if (r.width <= 0 || r.height <= 0) return;
        RECT rect{static_cast<LONG>(r.x), static_cast<LONG>(r.y),
            static_cast<LONG>(std::ceil(r.x + r.width)), static_cast<LONG>(std::ceil(r.y + r.height))};
        HBRUSH brush = CreateSolidBrush(color(c));
        FillRect(dc_, &rect, brush);
        DeleteObject(brush);
    }
    void drawRoundedRectangle(const DFRect& r, float radius, const DFColor& c) override
    {
        if (r.width <= 0 || r.height <= 0) return;
        HBRUSH brush = CreateSolidBrush(color(c));
        const auto oldBrush = SelectObject(dc_, brush);
        const auto oldPen = SelectObject(dc_, GetStockObject(NULL_PEN));
        RoundRect(dc_, static_cast<int>(r.x), static_cast<int>(r.y),
            static_cast<int>(r.x + r.width), static_cast<int>(r.y + r.height),
            static_cast<int>(radius * 2), static_cast<int>(radius * 2));
        SelectObject(dc_, oldPen); SelectObject(dc_, oldBrush); DeleteObject(brush);
    }
    void drawLine(const DFPoint& a, const DFPoint& b, const DFColor& c, float thickness = 1) override
    {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(thickness)), color(c));
        const auto oldPen = SelectObject(dc_, pen);
        MoveToEx(dc_, static_cast<int>(a.x), static_cast<int>(a.y), nullptr);
        LineTo(dc_, static_cast<int>(b.x), static_cast<int>(b.y));
        SelectObject(dc_, oldPen); DeleteObject(pen);
    }
    void drawTextScaled(float x, float y, const std::string& text, const DFColor& c, float scale, bool) override
    {
        const int pixels = std::max(8, static_cast<int>(std::lround(10 * DFTextPixelScale() * scale)));
        HFONT font = CreateFontW(-pixels, 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
        const auto oldFont = SelectObject(dc_, font);
        const int oldMode = SetBkMode(dc_, TRANSPARENT);
        const COLORREF oldColor = SetTextColor(dc_, color(c));
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc_, &metrics);
        MAT2 transform{};
        transform.eM11.value = transform.eM22.value = 1;
        GLYPHMETRICS capital{};
        GetGlyphOutlineW(dc_, 'H', GGO_METRICS, &capital, 0, nullptr, &transform);
        std::vector<int> advances(text.size());
        const float advance = DFGlyphAdvancePx(scale);
        for (size_t i = 0; i < text.size(); ++i)
            advances[i] = static_cast<int>(std::lround(x + (i + 1) * advance) - std::lround(x + i * advance));
        ExtTextOutA(dc_, static_cast<int>(std::lround(x)),
            static_cast<int>(std::lround(y)) - metrics.tmAscent + static_cast<int>(capital.gmBlackBoxY),
            0, nullptr, text.c_str(), static_cast<UINT>(text.size()), advances.data());
        SetTextColor(dc_, oldColor); SetBkMode(dc_, oldMode);
        SelectObject(dc_, oldFont); DeleteObject(font);
    }
private:
    static COLORREF color(const DFColor& c)
    {
        return RGB(static_cast<BYTE>(std::clamp(c.r, 0.0f, 1.0f) * 255),
            static_cast<BYTE>(std::clamp(c.g, 0.0f, 1.0f) * 255), static_cast<BYTE>(std::clamp(c.b, 0.0f, 1.0f) * 255));
    }
    HDC dc_;
};
