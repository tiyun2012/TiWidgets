#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include "core_types.h"

namespace df {

struct DockTheme {
    // Single title-bar color used everywhere (docked + floating).
    DFColor titleBar{DFColorFromHex(0x2D2D30)};
    DFColor dockBackground{DFColorFromHex(0x37353E)};
    DFColor dockBorder{0.40f, 0.40f, 0.45f, 1.0f};

    DFColor floatingFrame{DFColorFromHex(0x37353E)};
    DFColor floatingCloseButton{DFColorFromHex(0x2D2D30)};

    DFColor tabStrip{DFColorFromHex(0x3A3A3E)};
    DFColor tabActive{DFColorFromHex(0x414148)};
    DFColor tabInactive{DFColorFromHex(0x323238)};
    DFColor tabOutline{DFColorFromHex(0x202024)};
    DFColor tabAccent{DFColorFromHex(0xC48A3D)};
    DFColor tabTextActive{DFColorFromHex(0xE6E6EA)};
    DFColor tabTextInactive{DFColorFromHex(0xA2A2AA)};
    float tabBarHeight = 16.0f;
    float tabCornerRadius = 4.0f;
    float tabShoulderWidth = 6.0f;
    float tabLiftPx = 4.0f;
    float tabFontScale = 0.5f;
    // Use an even pixel scale so half-size tab text stays crisp (0.5 * 2.0 = 1px glyph pixels).
    float fontPixelScale = 2.0f;
    bool smoothFont = true;

    DFColor splitter{0.50f, 0.50f, 0.50f, 1.0f};
    DFColor splitterHover{0.75f, 0.75f, 0.82f, 1.0f};
    DFColor splitterDrag{0.30f, 0.60f, 1.00f, 1.0f};

    DFColor overlayPanel{0.06f, 0.06f, 0.09f, 0.85f};
    DFColor overlayAccent{0.30f, 0.78f, 1.00f, 0.95f};
    DFColor overlayAccentSoft{0.15f, 0.55f, 0.85f, 0.20f};

    DFColor clientAreaFill{DFColorFromHex(0x413E49)};
    DFColor clientAreaBorder{DFColorFromHex(0x5C3E94)};
    float clientAreaPadding = 2.0f;
    float tabClientAreaExtraPadding = 1.5f;
    float clientAreaCornerRadius = 3.333f;
    float clientAreaBorderThickness = 0.75f;

    // Shared visual feature toggles for quality/performance tuning.
    bool drawClientArea = true;
    bool drawClientAreaBorder = true;
    bool drawRoundedClientArea = true;
    bool drawSplitter = true;
    bool drawSplitterStateColors = true;
    bool drawSplitterGuideLines = false;
    bool drawTitleBarIcons = true;
    bool drawUndockIcon = false;
    bool drawWidgetHoverOutline = false;
    bool drawTabAccent = false;
    bool drawSteppedTabShape = true;
};

inline DockTheme MakeDarkTheme()
{
    return DockTheme{};
}

inline DockTheme MakeLightTheme()
{
    DockTheme theme{};
    theme.titleBar = DFColorFromHex(0x2D2D30);
    theme.dockBackground = DFColorFromHex(0x37353E);
    theme.dockBorder = {0.66f, 0.68f, 0.72f, 1.0f};
    theme.floatingFrame = DFColorFromHex(0x37353E);
    theme.floatingCloseButton = DFColorFromHex(0x2D2D30);
    theme.tabStrip = {0.84f, 0.86f, 0.89f, 1.0f};
    theme.tabActive = {0.62f, 0.72f, 0.92f, 1.0f};
    theme.tabInactive = {0.75f, 0.77f, 0.82f, 1.0f};
    theme.tabOutline = {0.55f, 0.58f, 0.64f, 1.0f};
    theme.tabAccent = {0.77f, 0.48f, 0.18f, 1.0f};
    theme.tabTextActive = {0.08f, 0.09f, 0.10f, 1.0f};
    theme.tabTextInactive = {0.16f, 0.17f, 0.20f, 1.0f};
    theme.splitter = {0.58f, 0.60f, 0.64f, 1.0f};
    theme.splitterHover = {0.36f, 0.60f, 0.94f, 1.0f};
    theme.overlayPanel = {0.94f, 0.95f, 0.97f, 0.90f};
    theme.overlayAccent = {0.20f, 0.52f, 0.92f, 0.95f};
    theme.overlayAccentSoft = {0.16f, 0.47f, 0.85f, 0.26f};
    return theme;
}

inline DockTheme MakeSlateTheme()
{
    DockTheme theme{};
    theme.titleBar = DFColorFromHex(0x2D2D30);
    theme.dockBackground = DFColorFromHex(0x37353E);
    theme.dockBorder = {0.34f, 0.41f, 0.46f, 1.0f};
    theme.floatingFrame = DFColorFromHex(0x37353E);
    theme.floatingCloseButton = DFColorFromHex(0x2D2D30);
    theme.tabStrip = {0.16f, 0.20f, 0.23f, 1.0f};
    theme.tabActive = {0.24f, 0.41f, 0.54f, 1.0f};
    theme.tabInactive = {0.18f, 0.25f, 0.29f, 1.0f};
    theme.tabOutline = {0.10f, 0.14f, 0.17f, 1.0f};
    theme.tabAccent = {0.88f, 0.64f, 0.23f, 1.0f};
    theme.tabTextActive = {0.92f, 0.95f, 0.97f, 1.0f};
    theme.tabTextInactive = {0.73f, 0.80f, 0.84f, 1.0f};
    theme.splitter = {0.31f, 0.39f, 0.43f, 1.0f};
    theme.splitterHover = {0.47f, 0.67f, 0.78f, 1.0f};
    theme.splitterDrag = {0.34f, 0.78f, 0.97f, 1.0f};
    theme.overlayPanel = {0.08f, 0.11f, 0.14f, 0.88f};
    theme.overlayAccent = {0.34f, 0.78f, 0.97f, 0.95f};
    theme.overlayAccentSoft = {0.20f, 0.61f, 0.78f, 0.24f};
    return theme;
}

// Template preset intended for local customization.
inline DockTheme MakeTemplateTheme()
{
    DockTheme theme = MakeDarkTheme();
    // Example customization points:
    // theme.titleBar = {0.19f, 0.24f, 0.40f, 1.0f};
    // theme.tabActive = {0.18f, 0.54f, 0.82f, 1.0f};
    // theme.overlayAccent = {0.98f, 0.64f, 0.21f, 0.95f};
    return theme;
}

inline void ApplyFastVisualPreset(DockTheme& theme)
{
    // Keep layout legibility while reducing extra visual work.
    theme.drawRoundedClientArea = false;
    theme.drawClientAreaBorder = false;
    theme.drawSplitterStateColors = false;
    theme.drawWidgetHoverOutline = false;
}

inline std::string NormalizeThemeName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name;
}

inline DockTheme ThemeFromName(const std::string& name)
{
    const std::string key = NormalizeThemeName(name);
    if (key == "light") return MakeLightTheme();
    if (key == "slate") return MakeSlateTheme();
    if (key == "template") return MakeTemplateTheme();
    return MakeDarkTheme();
}

inline DockTheme& MutableTheme()
{
    static DockTheme theme = [] {
        DockTheme t = MakeDarkTheme();
        DFSetTextPixelScale(t.fontPixelScale);
        DFSetTextSmooth(t.smoothFont);
        return t;
    }();
    return theme;
}

inline const DockTheme& CurrentTheme()
{
    return MutableTheme();
}

inline void SetTheme(const DockTheme& theme)
{
    MutableTheme() = theme;
    DFSetTextPixelScale(theme.fontPixelScale);
    DFSetTextSmooth(theme.smoothFont);
}

inline void SetThemeByName(const std::string& name)
{
    SetTheme(ThemeFromName(name));
}

} // namespace df

