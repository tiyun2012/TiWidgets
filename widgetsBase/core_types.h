// Engine-agnostic placeholder types; rename avoids Win32 name clashes.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct DFPoint {
    float x = 0;
    float y = 0;
};

struct DFRect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    bool contains(const DFPoint& p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
};

class Event {
public:
    enum class Type {
        Unknown,
        MouseDown,
        MouseUp,
        MouseMove,
        MouseDoubleClick,
        MouseDrag,
        KeyDown,
        KeyUp,
        TextInput,
        Close
    };
    explicit Event(Type t = Type::Unknown) : type(t) {}
    Type type = Type::Unknown;
    float x = 0.0f;
    float y = 0.0f;
    int key = 0;
    std::string textUtf8{};
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool handled = false;
};

struct DFColor {
    float r = 1, g = 1, b = 1, a = 1;
};

inline constexpr DFColor DFColorFromHex(uint32_t rgb, float alpha = 1.0f)
{
    return DFColor{
        static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(rgb & 0xFFu) / 255.0f,
        alpha
    };
}

struct DFSize {
    float width = 0;
    float height = 0;
};

struct DFMargins {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class SizePolicy {
    Fixed,
    Minimum,
    Expanding
};

struct WidgetSizePolicy {
    SizePolicy horizontal = SizePolicy::Expanding;
    SizePolicy vertical = SizePolicy::Expanding;
};

class Widget;

class Layout {
public:
    virtual ~Layout() = default;
    virtual void addWidget(Widget* widget, float stretch = 0.0f) = 0;
    virtual void clear() = 0;
    virtual void updateBounds(const DFRect& containerBounds) = 0;
    virtual DFSize minimumSize() const = 0;
};

inline float& DFMutableTextPixelScale()
{
    static float scale = 1.8f;
    return scale;
}

inline bool& DFMutableTextSmooth()
{
    static bool smooth = true;
    return smooth;
}

inline float DFTextPixelScale()
{
    return std::clamp(DFMutableTextPixelScale(), 1.0f, 4.0f);
}

inline void DFSetTextPixelScale(float scale)
{
    DFMutableTextPixelScale() = std::clamp(scale, 1.0f, 4.0f);
}

inline bool DFTextSmooth()
{
    return DFMutableTextSmooth();
}

inline void DFSetTextSmooth(bool smooth)
{
    DFMutableTextSmooth() = smooth;
}

inline float DFGlyphAdvancePx(float scaleMul = 1.0f)
{
    const float s = std::clamp(scaleMul, 0.2f, 4.0f);
    return DFTextPixelScale() * s * 6.0f;
}

inline float DFGlyphHeightPx(float scaleMul = 1.0f)
{
    const float s = std::clamp(scaleMul, 0.2f, 4.0f);
    return DFTextPixelScale() * s * 7.0f;
}

inline int DFMaxCharsForWidth(float maxWidthPx, float scaleMul = 1.0f)
{
    if (maxWidthPx <= 0.0f) {
        return 0;
    }
    return static_cast<int>(maxWidthPx / DFGlyphAdvancePx(scaleMul));
}

inline std::string DFClipTextToWidth(const std::string& text, float maxWidthPx, bool withEllipsis = true, float scaleMul = 1.0f)
{
    const int maxChars = DFMaxCharsForWidth(maxWidthPx, scaleMul);
    if (maxChars <= 0) {
        return {};
    }
    if (static_cast<int>(text.size()) <= maxChars) {
        return text;
    }
    // For very narrow slots, plain clipping is more readable than "X..."
    if (!withEllipsis || maxChars <= 6) {
        return text.substr(0, static_cast<size_t>(maxChars));
    }
    return text.substr(0, static_cast<size_t>(maxChars - 3)) + "...";
}

inline float DFTextBaselineYForRect(const DFRect& rect, float scaleMul = 1.0f)
{
    return rect.y + (rect.height - DFGlyphHeightPx(scaleMul)) * 0.5f;
}

inline const uint8_t* DFGlyph5x7(char c)
{
    static constexpr uint8_t kUnknown[7] = {0x0E, 0x11, 0x02, 0x04, 0x04, 0x00, 0x04};
    static constexpr uint8_t kSpace[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr uint8_t kDot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
    static constexpr uint8_t kDash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static constexpr uint8_t kUnderscore[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};

    static constexpr uint8_t k0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static constexpr uint8_t k1[7] = {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
    static constexpr uint8_t k2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static constexpr uint8_t k3[7] = {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E};
    static constexpr uint8_t k4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static constexpr uint8_t k5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
    static constexpr uint8_t k6[7] = {0x07, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static constexpr uint8_t k7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static constexpr uint8_t k8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static constexpr uint8_t k9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C};

    static constexpr uint8_t kA[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static constexpr uint8_t kB[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static constexpr uint8_t kC[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static constexpr uint8_t kD[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
    static constexpr uint8_t kE[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static constexpr uint8_t kF[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static constexpr uint8_t kG[7] = {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E};
    static constexpr uint8_t kH[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static constexpr uint8_t kI[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static constexpr uint8_t kJ[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    static constexpr uint8_t kK[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static constexpr uint8_t kL[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static constexpr uint8_t kM[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static constexpr uint8_t kN[7] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
    static constexpr uint8_t kO[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static constexpr uint8_t kP[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static constexpr uint8_t kQ[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static constexpr uint8_t kR[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static constexpr uint8_t kS[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static constexpr uint8_t kT[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static constexpr uint8_t kU[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static constexpr uint8_t kV[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static constexpr uint8_t kW[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static constexpr uint8_t kX[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static constexpr uint8_t kY[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static constexpr uint8_t kZ[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static constexpr uint8_t ka[7] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F};
    static constexpr uint8_t kb[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E};
    static constexpr uint8_t kc[7] = {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E};
    static constexpr uint8_t kd[7] = {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F};
    static constexpr uint8_t ke[7] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E};
    static constexpr uint8_t kf[7] = {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08};
    static constexpr uint8_t kg[7] = {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E};
    static constexpr uint8_t kh[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static constexpr uint8_t ki[7] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E};
    static constexpr uint8_t kj[7] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C};
    static constexpr uint8_t kk[7] = {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12};
    static constexpr uint8_t kl[7] = {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static constexpr uint8_t km[7] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15};
    static constexpr uint8_t kn[7] = {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static constexpr uint8_t ko[7] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E};
    static constexpr uint8_t kp[7] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10};
    static constexpr uint8_t kq[7] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01};
    static constexpr uint8_t kr[7] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10};
    static constexpr uint8_t ks[7] = {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E};
    static constexpr uint8_t kt[7] = {0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06};
    static constexpr uint8_t ku[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D};
    static constexpr uint8_t kv[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static constexpr uint8_t kw[7] = {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static constexpr uint8_t kx[7] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11};
    static constexpr uint8_t ky[7] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E};
    static constexpr uint8_t kz[7] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F};

    switch (c) {
    case ' ': return kSpace;
    case '.': return kDot;
    case '-': return kDash;
    case '_': return kUnderscore;
    case '0': return k0;
    case '1': return k1;
    case '2': return k2;
    case '3': return k3;
    case '4': return k4;
    case '5': return k5;
    case '6': return k6;
    case '7': return k7;
    case '8': return k8;
    case '9': return k9;
    case 'A': return kA;
    case 'B': return kB;
    case 'C': return kC;
    case 'D': return kD;
    case 'E': return kE;
    case 'F': return kF;
    case 'G': return kG;
    case 'H': return kH;
    case 'I': return kI;
    case 'J': return kJ;
    case 'K': return kK;
    case 'L': return kL;
    case 'M': return kM;
    case 'N': return kN;
    case 'O': return kO;
    case 'P': return kP;
    case 'Q': return kQ;
    case 'R': return kR;
    case 'S': return kS;
    case 'T': return kT;
    case 'U': return kU;
    case 'V': return kV;
    case 'W': return kW;
    case 'X': return kX;
    case 'Y': return kY;
    case 'Z': return kZ;
    case 'a': return ka;
    case 'b': return kb;
    case 'c': return kc;
    case 'd': return kd;
    case 'e': return ke;
    case 'f': return kf;
    case 'g': return kg;
    case 'h': return kh;
    case 'i': return ki;
    case 'j': return kj;
    case 'k': return kk;
    case 'l': return kl;
    case 'm': return km;
    case 'n': return kn;
    case 'o': return ko;
    case 'p': return kp;
    case 'q': return kq;
    case 'r': return kr;
    case 's': return ks;
    case 't': return kt;
    case 'u': return ku;
    case 'v': return kv;
    case 'w': return kw;
    case 'x': return kx;
    case 'y': return ky;
    case 'z': return kz;
    default:
        return kUnknown;
    }
}

template <typename PixelDrawer>
inline void DFDrawBitmapTextPixels(float x, float y, const std::string& text, PixelDrawer&& drawPixel, float scaleMul = 1.0f)
{
    const float s = std::clamp(scaleMul, 0.2f, 4.0f);
    const float px = DFTextPixelScale() * s;
    const float advance = DFGlyphAdvancePx(scaleMul);
    float cursorX = x;
    for (char ch : text) {
        const uint8_t* glyph = DFGlyph5x7(ch);
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph[row];
            for (int col = 0; col < 5; ++col) {
                const uint8_t mask = static_cast<uint8_t>(1u << (4 - col));
                if ((bits & mask) != 0u) {
                    drawPixel(cursorX + col * px, y + row * px, px, px);
                }
            }
        }
        cursorX += advance;
    }
}

class Canvas;

inline void DFDrawText(Canvas& canvas,
                       float x,
                       float y,
                       const std::string& text,
                       const DFColor& color,
                       float scaleMul = 1.0f,
                       bool smooth = false);

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void drawRectangle(const DFRect&, const DFColor&) {}
    virtual void drawRoundedRectangle(const DFRect& rect, float /*radius*/, const DFColor& color) { drawRectangle(rect, color); }
    virtual void drawRoundedRectangleOutline(const DFRect& rect, float /*radius*/, const DFColor& color, float thickness = 1.0f)
    {
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            return;
        }
        const float t = std::max(0.5f, thickness);
        drawRectangle({rect.x, rect.y, rect.width, t}, color);
        drawRectangle({rect.x, rect.y + std::max(0.0f, rect.height - t), rect.width, t}, color);
        drawRectangle({rect.x, rect.y, t, rect.height}, color);
        drawRectangle({rect.x + std::max(0.0f, rect.width - t), rect.y, t, rect.height}, color);
    }
    virtual void drawLine(const DFPoint&, const DFPoint&, const DFColor&, float /*thickness*/ = 1.0f) {}
    virtual void drawText(float x, float y, const std::string& text, const DFColor& color)
    {
        DFDrawText(*this, x, y, text, color, 1.0f, DFTextSmooth());
    }
    virtual void drawTextScaled(float x, float y, const std::string& text, const DFColor& color, float scale, bool smooth)
    {
        DFDrawBitmapTextPixels(x, y, text, [&](float px, float py, float w, float h) {
            if (smooth) drawRoundedRectangle({px, py, w, h}, std::max(0.2f, w * 0.35f), color);
            else drawRectangle({px, py, w, h}, color);
        }, scale);
    }
};

inline void DFDrawText(Canvas& canvas,
                       float x,
                       float y,
                       const std::string& text,
                       const DFColor& color,
                       float scaleMul,
                       bool smooth)
{
    canvas.drawTextScaled(x, y, text, color, scaleMul, smooth);
}

class Widget {
public:
    virtual ~Widget() = default;
    virtual void setBounds(const DFRect& r)
    {
        bounds_ = r;
        if (layout_) {
            layout_->updateBounds(bounds_);
        }
    }
    virtual void paint(Canvas& /*canvas*/) {}
    virtual void handleEvent(Event& /*event*/) {}
    virtual DFSize minimumSize() const
    {
        DFSize min = minimumSize_;
        if (layout_) {
            const DFSize layoutMin = layout_->minimumSize();
            min.width = std::max(min.width, layoutMin.width);
            min.height = std::max(min.height, layoutMin.height);
        }
        return min;
    }

    void setMinimumSize(float width, float height)
    {
        minimumSize_.width = std::max(0.0f, width);
        minimumSize_.height = std::max(0.0f, height);
        if (layout_) {
            layout_->updateBounds(bounds_);
        }
    }

    void setSizePolicy(SizePolicy horizontal, SizePolicy vertical)
    {
        sizePolicy_.horizontal = horizontal;
        sizePolicy_.vertical = vertical;
    }

    const WidgetSizePolicy& sizePolicy() const { return sizePolicy_; }

    void setLayout(std::unique_ptr<Layout> layout)
    {
        layout_ = std::move(layout);
        if (layout_) {
            layout_->updateBounds(bounds_);
        }
    }

    Layout* layout() const { return layout_.get(); }

    void addLayoutWidget(Widget* widget, float stretch = 0.0f)
    {
        if (!layout_ || !widget) {
            return;
        }
        layout_->addWidget(widget, stretch);
        layout_->updateBounds(bounds_);
    }

    void clearLayout()
    {
        if (!layout_) {
            return;
        }
        layout_->clear();
    }

    void setAcceptsFocus(bool acceptsFocus)
    {
        acceptsFocus_ = acceptsFocus;
        if (!acceptsFocus_ && focused_) {
            setFocused(false);
        }
    }

    bool acceptsFocus() const { return acceptsFocus_; }

    void setFocused(bool focused)
    {
        if (focused_ == focused) {
            return;
        }
        focused_ = focused;
        onFocusChanged(focused_);
    }

    bool isFocused() const { return focused_; }

    const DFRect& bounds() const { return bounds_; }

protected:
    virtual void onFocusChanged(bool /*focused*/) {}

    DFRect bounds_{};
    std::unique_ptr<Layout> layout_{};
    DFSize minimumSize_{};
    WidgetSizePolicy sizePolicy_{};
    bool acceptsFocus_ = false;
    bool focused_ = false;
};

class BoxLayout : public Layout {
public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    explicit BoxLayout(Orientation orientation)
        : orientation_(orientation)
    {
    }

    void addWidget(Widget* widget, float stretch = 0.0f) override
    {
        if (!widget) {
            return;
        }
        for (auto& item : items_) {
            if (item.widget == widget) {
                item.stretch = std::max(0.0f, stretch);
                return;
            }
        }
        items_.push_back({widget, std::max(0.0f, stretch)});
    }

    void clear() override
    {
        items_.clear();
    }

    void setSpacing(float spacing)
    {
        spacing_ = std::max(0.0f, spacing);
    }

    float spacing() const
    {
        return spacing_;
    }

    void setMargins(const DFMargins& margins)
    {
        margins_.left = std::max(0.0f, margins.left);
        margins_.top = std::max(0.0f, margins.top);
        margins_.right = std::max(0.0f, margins.right);
        margins_.bottom = std::max(0.0f, margins.bottom);
    }

    const DFMargins& margins() const
    {
        return margins_;
    }

    DFSize minimumSize() const override
    {
        const size_t count = items_.size();
        if (count == 0) {
            return {
                margins_.left + margins_.right,
                margins_.top + margins_.bottom
            };
        }

        float minMain = 0.0f;
        float minCross = 0.0f;
        for (const Item& item : items_) {
            if (!item.widget) {
                continue;
            }
            const DFSize min = item.widget->minimumSize();
            const float childMain = mainSize(min);
            const float childCross = crossSize(min);
            minMain += childMain;
            minCross = std::max(minCross, childCross);
        }

        if (count > 1) {
            minMain += spacing_ * static_cast<float>(count - 1);
        }

        if (orientation_ == Orientation::Vertical) {
            return {
                minCross + margins_.left + margins_.right,
                minMain + margins_.top + margins_.bottom
            };
        }

        return {
            minMain + margins_.left + margins_.right,
            minCross + margins_.top + margins_.bottom
        };
    }

    void updateBounds(const DFRect& containerBounds) override
    {
        if (items_.empty()) {
            return;
        }

        const DFRect inner{
            containerBounds.x + margins_.left,
            containerBounds.y + margins_.top,
            std::max(0.0f, containerBounds.width - margins_.left - margins_.right),
            std::max(0.0f, containerBounds.height - margins_.top - margins_.bottom)
        };

        const size_t count = items_.size();
        std::vector<float> baseMain(count, 0.0f);
        std::vector<float> minCross(count, 0.0f);
        std::vector<float> weights(count, 0.0f);
        float sumBaseMain = 0.0f;
        float totalWeight = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            Item& item = items_[i];
            if (!item.widget) {
                continue;
            }
            const DFSize min = item.widget->minimumSize();
            const WidgetSizePolicy policy = item.widget->sizePolicy();

            baseMain[i] = std::max(0.0f, mainSize(min));
            minCross[i] = std::max(0.0f, crossSize(min));
            sumBaseMain += baseMain[i];

            const SizePolicy axisPolicy = (orientation_ == Orientation::Vertical)
                ? policy.vertical
                : policy.horizontal;
            const bool axisExpands = axisPolicy == SizePolicy::Expanding;
            const float weight = (item.stretch > 0.0f)
                ? item.stretch
                : (axisExpands ? 1.0f : 0.0f);
            weights[i] = weight;
            totalWeight += weight;
        }

        const float gapTotal = (count > 1) ? spacing_ * static_cast<float>(count - 1) : 0.0f;
        const float availableMain = std::max(0.0f, (orientation_ == Orientation::Vertical) ? inner.height : inner.width);
        const float availableForWidgets = std::max(0.0f, availableMain - gapTotal);

        float compression = 1.0f;
        if (sumBaseMain > 0.0f && availableForWidgets < sumBaseMain) {
            compression = availableForWidgets / sumBaseMain;
        }
        for (size_t i = 0; i < count; ++i) {
            baseMain[i] *= compression;
        }

        float usedMain = 0.0f;
        for (float v : baseMain) {
            usedMain += v;
        }
        const float extraMain = std::max(0.0f, availableForWidgets - usedMain);

        const float innerCross = (orientation_ == Orientation::Vertical) ? inner.width : inner.height;
        float cursor = (orientation_ == Orientation::Vertical) ? inner.y : inner.x;

        for (size_t i = 0; i < count; ++i) {
            Item& item = items_[i];
            if (!item.widget) {
                cursor += spacing_;
                continue;
            }

            float main = baseMain[i];
            if (extraMain > 0.0f && totalWeight > 0.0f && weights[i] > 0.0f) {
                main += extraMain * (weights[i] / totalWeight);
            }

            const WidgetSizePolicy policy = item.widget->sizePolicy();
            const SizePolicy crossPolicy = (orientation_ == Orientation::Vertical)
                ? policy.horizontal
                : policy.vertical;

            float cross = innerCross;
            if (crossPolicy != SizePolicy::Expanding) {
                cross = std::min(innerCross, minCross[i]);
            }

            DFRect childBounds{};
            if (orientation_ == Orientation::Vertical) {
                childBounds = {
                    inner.x,
                    cursor,
                    std::max(0.0f, cross),
                    std::max(0.0f, main)
                };
                cursor += main + spacing_;
            } else {
                childBounds = {
                    cursor,
                    inner.y,
                    std::max(0.0f, main),
                    std::max(0.0f, cross)
                };
                cursor += main + spacing_;
            }
            item.widget->setBounds(childBounds);
        }
    }

protected:
    struct Item {
        Widget* widget = nullptr;
        float stretch = 0.0f;
    };

    float mainSize(const DFSize& size) const
    {
        return (orientation_ == Orientation::Vertical) ? size.height : size.width;
    }

    float crossSize(const DFSize& size) const
    {
        return (orientation_ == Orientation::Vertical) ? size.width : size.height;
    }

    Orientation orientation_ = Orientation::Vertical;
    std::vector<Item> items_{};
    float spacing_ = 4.0f;
    DFMargins margins_{};
};

class VBoxLayout final : public BoxLayout {
public:
    VBoxLayout() : BoxLayout(Orientation::Vertical) {}
};

class HBoxLayout final : public BoxLayout {
public:
    HBoxLayout() : BoxLayout(Orientation::Horizontal) {}
};

class GridLayout : public Layout {
public:
    struct Item {
        Widget* widget = nullptr;
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
    };

    void addWidget(Widget* widget, float /*stretch*/ = 0.0f) override
    {
        if (!widget) {
            return;
        }
        const int row = nextIndex_ / std::max(1, columnCount_);
        const int column = nextIndex_ % std::max(1, columnCount_);
        addWidget(widget, row, column, 1, 1);
        ++nextIndex_;
    }

    void addWidget(Widget* widget, int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        if (!widget) {
            return;
        }
        Item item{};
        item.widget = widget;
        item.row = std::max(0, row);
        item.column = std::max(0, column);
        item.rowSpan = std::max(1, rowSpan);
        item.columnSpan = std::max(1, columnSpan);

        for (Item& existing : items_) {
            if (existing.widget == widget) {
                existing = item;
                return;
            }
        }
        items_.push_back(item);
    }

    void clear() override
    {
        items_.clear();
        nextIndex_ = 0;
    }

    void setColumnCount(int columns)
    {
        columnCount_ = std::max(1, columns);
        nextIndex_ = 0;
    }

    void setSpacing(float horizontal, float vertical)
    {
        horizontalSpacing_ = std::max(0.0f, horizontal);
        verticalSpacing_ = std::max(0.0f, vertical);
    }

    void setMargins(const DFMargins& margins)
    {
        margins_.left = std::max(0.0f, margins.left);
        margins_.top = std::max(0.0f, margins.top);
        margins_.right = std::max(0.0f, margins.right);
        margins_.bottom = std::max(0.0f, margins.bottom);
    }

    DFSize minimumSize() const override
    {
        if (items_.empty()) {
            return {
                margins_.left + margins_.right,
                margins_.top + margins_.bottom
            };
        }

        int rowCount = 0;
        int colCount = 0;
        computeGridShape(rowCount, colCount);
        if (rowCount <= 0 || colCount <= 0) {
            return {
                margins_.left + margins_.right,
                margins_.top + margins_.bottom
            };
        }

        std::vector<float> rowMins(static_cast<size_t>(rowCount), 0.0f);
        std::vector<float> colMins(static_cast<size_t>(colCount), 0.0f);
        fillCellMinimums(rowMins, colMins);

        float totalWidth = 0.0f;
        float totalHeight = 0.0f;
        for (float w : colMins) {
            totalWidth += w;
        }
        for (float h : rowMins) {
            totalHeight += h;
        }
        if (colCount > 1) {
            totalWidth += horizontalSpacing_ * static_cast<float>(colCount - 1);
        }
        if (rowCount > 1) {
            totalHeight += verticalSpacing_ * static_cast<float>(rowCount - 1);
        }

        return {
            totalWidth + margins_.left + margins_.right,
            totalHeight + margins_.top + margins_.bottom
        };
    }

    void updateBounds(const DFRect& containerBounds) override
    {
        if (items_.empty()) {
            return;
        }

        int rowCount = 0;
        int colCount = 0;
        computeGridShape(rowCount, colCount);
        if (rowCount <= 0 || colCount <= 0) {
            return;
        }

        std::vector<float> rowSizes(static_cast<size_t>(rowCount), 0.0f);
        std::vector<float> colSizes(static_cast<size_t>(colCount), 0.0f);
        std::vector<bool> rowExpand(static_cast<size_t>(rowCount), false);
        std::vector<bool> colExpand(static_cast<size_t>(colCount), false);

        fillCellMinimums(rowSizes, colSizes);
        markExpandableAxes(rowExpand, colExpand);

        const DFRect inner{
            containerBounds.x + margins_.left,
            containerBounds.y + margins_.top,
            std::max(0.0f, containerBounds.width - margins_.left - margins_.right),
            std::max(0.0f, containerBounds.height - margins_.top - margins_.bottom)
        };

        const float gapW = (colCount > 1) ? horizontalSpacing_ * static_cast<float>(colCount - 1) : 0.0f;
        const float gapH = (rowCount > 1) ? verticalSpacing_ * static_cast<float>(rowCount - 1) : 0.0f;

        const float innerW = std::max(0.0f, inner.width - gapW);
        const float innerH = std::max(0.0f, inner.height - gapH);

        distributeAxisSpace(colSizes, colExpand, innerW);
        distributeAxisSpace(rowSizes, rowExpand, innerH);

        std::vector<float> rowOffsets(static_cast<size_t>(rowCount), inner.y);
        std::vector<float> colOffsets(static_cast<size_t>(colCount), inner.x);

        for (int c = 1; c < colCount; ++c) {
            colOffsets[static_cast<size_t>(c)] =
                colOffsets[static_cast<size_t>(c - 1)] +
                colSizes[static_cast<size_t>(c - 1)] +
                horizontalSpacing_;
        }
        for (int r = 1; r < rowCount; ++r) {
            rowOffsets[static_cast<size_t>(r)] =
                rowOffsets[static_cast<size_t>(r - 1)] +
                rowSizes[static_cast<size_t>(r - 1)] +
                verticalSpacing_;
        }

        for (const Item& item : items_) {
            if (!item.widget) {
                continue;
            }
            const int row = std::clamp(item.row, 0, rowCount - 1);
            const int col = std::clamp(item.column, 0, colCount - 1);
            const int rowSpan = std::max(1, std::min(item.rowSpan, rowCount - row));
            const int colSpan = std::max(1, std::min(item.columnSpan, colCount - col));

            float cellW = 0.0f;
            float cellH = 0.0f;
            for (int c = 0; c < colSpan; ++c) {
                cellW += colSizes[static_cast<size_t>(col + c)];
            }
            for (int r = 0; r < rowSpan; ++r) {
                cellH += rowSizes[static_cast<size_t>(row + r)];
            }
            if (colSpan > 1) {
                cellW += horizontalSpacing_ * static_cast<float>(colSpan - 1);
            }
            if (rowSpan > 1) {
                cellH += verticalSpacing_ * static_cast<float>(rowSpan - 1);
            }

            const DFSize min = item.widget->minimumSize();
            const WidgetSizePolicy policy = item.widget->sizePolicy();

            float w = cellW;
            float h = cellH;
            if (policy.horizontal != SizePolicy::Expanding) {
                w = std::min(cellW, std::max(0.0f, min.width));
            }
            if (policy.vertical != SizePolicy::Expanding) {
                h = std::min(cellH, std::max(0.0f, min.height));
            }

            item.widget->setBounds({
                colOffsets[static_cast<size_t>(col)],
                rowOffsets[static_cast<size_t>(row)],
                std::max(0.0f, w),
                std::max(0.0f, h)
            });
        }
    }

private:
    void computeGridShape(int& rows, int& columns) const
    {
        rows = 0;
        columns = 0;
        for (const Item& item : items_) {
            rows = std::max(rows, item.row + item.rowSpan);
            columns = std::max(columns, item.column + item.columnSpan);
        }
        rows = std::max(0, rows);
        columns = std::max(0, columns);
    }

    void fillCellMinimums(std::vector<float>& rowMins, std::vector<float>& colMins) const
    {
        for (const Item& item : items_) {
            if (!item.widget) {
                continue;
            }
            const int rowCount = static_cast<int>(rowMins.size());
            const int colCount = static_cast<int>(colMins.size());
            if (rowCount <= 0 || colCount <= 0) {
                continue;
            }

            const int row = std::clamp(item.row, 0, rowCount - 1);
            const int col = std::clamp(item.column, 0, colCount - 1);
            const int rowSpan = std::max(1, std::min(item.rowSpan, rowCount - row));
            const int colSpan = std::max(1, std::min(item.columnSpan, colCount - col));

            const DFSize min = item.widget->minimumSize();
            const float minW = std::max(0.0f, min.width) / static_cast<float>(colSpan);
            const float minH = std::max(0.0f, min.height) / static_cast<float>(rowSpan);

            for (int c = 0; c < colSpan; ++c) {
                colMins[static_cast<size_t>(col + c)] = std::max(colMins[static_cast<size_t>(col + c)], minW);
            }
            for (int r = 0; r < rowSpan; ++r) {
                rowMins[static_cast<size_t>(row + r)] = std::max(rowMins[static_cast<size_t>(row + r)], minH);
            }
        }
    }

    void markExpandableAxes(std::vector<bool>& rowExpand, std::vector<bool>& colExpand) const
    {
        for (const Item& item : items_) {
            if (!item.widget) {
                continue;
            }
            const int rowCount = static_cast<int>(rowExpand.size());
            const int colCount = static_cast<int>(colExpand.size());
            if (rowCount <= 0 || colCount <= 0) {
                continue;
            }

            const int row = std::clamp(item.row, 0, rowCount - 1);
            const int col = std::clamp(item.column, 0, colCount - 1);
            const int rowSpan = std::max(1, std::min(item.rowSpan, rowCount - row));
            const int colSpan = std::max(1, std::min(item.columnSpan, colCount - col));

            const WidgetSizePolicy policy = item.widget->sizePolicy();
            if (policy.horizontal == SizePolicy::Expanding) {
                for (int c = 0; c < colSpan; ++c) {
                    colExpand[static_cast<size_t>(col + c)] = true;
                }
            }
            if (policy.vertical == SizePolicy::Expanding) {
                for (int r = 0; r < rowSpan; ++r) {
                    rowExpand[static_cast<size_t>(row + r)] = true;
                }
            }
        }
    }

    static void distributeAxisSpace(std::vector<float>& sizes, const std::vector<bool>& expandable, float available)
    {
        if (sizes.empty()) {
            return;
        }

        float minTotal = 0.0f;
        for (float size : sizes) {
            minTotal += size;
        }
        if (minTotal > 0.0f && available < minTotal) {
            const float scale = available / minTotal;
            for (float& size : sizes) {
                size *= scale;
            }
            return;
        }

        const float extra = std::max(0.0f, available - minTotal);
        int expandableCount = 0;
        for (bool axis : expandable) {
            if (axis) {
                ++expandableCount;
            }
        }
        if (extra <= 0.0f || expandableCount <= 0) {
            return;
        }
        const float perAxis = extra / static_cast<float>(expandableCount);
        for (size_t i = 0; i < sizes.size(); ++i) {
            if (expandable[i]) {
                sizes[i] += perAxis;
            }
        }
    }

    std::vector<Item> items_{};
    int columnCount_ = 1;
    int nextIndex_ = 0;
    float horizontalSpacing_ = 4.0f;
    float verticalSpacing_ = 4.0f;
    DFMargins margins_{};
};

