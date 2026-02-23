// Engine-agnostic placeholder types; rename avoids Win32 name clashes.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

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
    enum class Type { Unknown, MouseDown, MouseUp, MouseMove, KeyDown, KeyUp, Close };
    explicit Event(Type t = Type::Unknown) : type(t) {}
    Type type = Type::Unknown;
    float x = 0.0f;
    float y = 0.0f;
    int key = 0;
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
};

inline void DFDrawText(Canvas& canvas,
                       float x,
                       float y,
                       const std::string& text,
                       const DFColor& color,
                       float scaleMul,
                       bool smooth)
{
    DFDrawBitmapTextPixels(
        x,
        y,
        text,
        [&](float px, float py, float w, float h) {
            if (!smooth) {
                canvas.drawRectangle({px, py, w, h}, color);
                return;
            }
            // Soft mode: rounded glyph pixels reduce blocky appearance.
            const float radius = std::max(0.2f, w * 0.35f);
            canvas.drawRoundedRectangle({px, py, w, h}, radius, color);
        },
        scaleMul);
}

class Widget {
public:
    virtual ~Widget() = default;
    virtual void setBounds(const DFRect& r) { bounds_ = r; }
    virtual void paint(Canvas& /*canvas*/) {}
    virtual void handleEvent(Event& /*event*/) {}
    virtual DFSize minimumSize() const { return {}; }
    const DFRect& bounds() const { return bounds_; }
protected:
    DFRect bounds_{};
};

