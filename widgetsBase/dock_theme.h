#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include "core_types.h"

namespace df {

struct DockTheme {
    // Single title-bar color used everywhere (docked + floating).
    DFColor titleBar{DFColorFromHex(0x252B34)};
    DFColor dockBackground{DFColorFromHex(0x20242B)};
    DFColor dockBorder{DFColorFromHex(0x3C434D)};

    DFColor floatingFrame{DFColorFromHex(0x252B34)};
    DFColor floatingCloseButton{DFColorFromHex(0x20242B)};

    DFColor tabStrip{DFColorFromHex(0x20242B)};
    DFColor tabActive{DFColorFromHex(0x252B34)};
    DFColor tabInactive{DFColorFromHex(0x20242B)};
    DFColor tabOutline{DFColorFromHex(0x3C434D)};
    DFColor tabAccent{DFColorFromHex(0x68B5FF)};
    DFColor tabTextActive{DFColorFromHex(0xEDF2F7)};
    DFColor tabTextInactive{DFColorFromHex(0xA6B2C2)};
    float tabBarHeight = 32.0f;
    float tabCornerRadius = 4.0f;
    float tabShoulderWidth = 6.0f;
    float tabLiftPx = 4.0f;
    float tabFontScale = 0.9f;
    // Base text metrics shared by the native atlas and portable bitmap fallback.
    float fontPixelScale = 1.4f;
    bool smoothFont = true;

    DFColor splitter{DFColorFromHex(0x3C434D)};
    DFColor splitterHandle{DFColorFromHex(0x69717C)};
    DFColor splitterHover{DFColorFromHex(0x818A96)};
    DFColor splitterDrag{DFColorFromHex(0xA2ACB9)};
    float splitterHandleScale = 1.1f;

    DFColor overlayPanel{0.06f, 0.06f, 0.09f, 0.85f};
    DFColor overlayAccent{0.30f, 0.78f, 1.00f, 0.95f};
    DFColor overlayAccentSoft{0.15f, 0.55f, 0.85f, 0.20f};

    DFColor clientAreaFill{DFColorFromHex(0x252B34)};
    DFColor clientAreaBorder{DFColorFromHex(0x3C434D)};
    float clientAreaPadding = 2.0f;
    float tabClientAreaExtraPadding = 0.0f;
    float clientAreaCornerRadius = 3.0f;
    float clientAreaBorderThickness = 1.0f;

    // Shared visual feature toggles for quality/performance tuning.
    bool drawClientArea = true;
    bool drawClientAreaBorder = false;
    bool drawRoundedClientArea = true;
    bool drawSplitter = true;
    bool drawSplitterStateColors = true;
    bool drawSplitterGuideLines = false;
    bool drawTitleBarIcons = true;
    bool drawUndockIcon = false;
    bool drawWidgetHoverOutline = false;
    bool drawTabAccent = false;
    bool drawSteppedTabShape = false;
    DFColor text{DFColorFromHex(0xEDF2F7)};
    DFColor mutedText{DFColorFromHex(0xA6B2C2)};
    DFColor controlFill{DFColorFromHex(0x303946)};
    DFColor selection{DFColorFromHex(0x234C70)};
    DFColor success{DFColorFromHex(0x66D6AD)};
};

inline DockTheme MakeDarkTheme()
{
    return DockTheme{};
}

inline DockTheme MakeLightTheme()
{
    DockTheme theme{};
    theme.titleBar = DFColorFromHex(0xFFFFFF);
    theme.dockBackground = DFColorFromHex(0xEDF1F6);
    theme.dockBorder = DFColorFromHex(0xBDC3CB);
    theme.floatingFrame = DFColorFromHex(0xFFFFFF);
    theme.floatingCloseButton = theme.titleBar;
    theme.tabStrip = theme.dockBackground;
    theme.tabActive = theme.floatingFrame;
    theme.tabInactive = theme.tabStrip;
    theme.tabOutline = theme.dockBorder;
    theme.tabAccent = DFColorFromHex(0x176FC1);
    theme.text = DFColorFromHex(0x202D3E);
    theme.mutedText = DFColorFromHex(0x53647B);
    theme.tabTextActive = theme.text;
    theme.tabTextInactive = theme.mutedText;
    theme.splitter = theme.dockBorder;
    theme.splitterHandle = DFColorFromHex(0x89939E);
    theme.splitterHover = DFColorFromHex(0x687582);
    theme.splitterDrag = DFColorFromHex(0x4B5865);
    theme.overlayPanel = {0.94f, 0.95f, 0.97f, 0.90f};
    theme.overlayAccent = theme.tabAccent;
    theme.overlayAccentSoft = {0.16f, 0.47f, 0.85f, 0.26f};
    theme.clientAreaFill = theme.floatingFrame;
    theme.clientAreaBorder = theme.dockBorder;
    theme.controlFill = DFColorFromHex(0xDDE5EF);
    theme.selection = DFColorFromHex(0xD6E9FC);
    theme.success = DFColorFromHex(0x147657);
    return theme;
}

inline DockTheme MakeSlateTheme()
{
    DockTheme theme{};
    theme.titleBar = DFColorFromHex(0x283640);
    theme.dockBackground = DFColorFromHex(0x202C36);
    theme.dockBorder = DFColorFromHex(0x495862);
    theme.floatingFrame = DFColorFromHex(0x283640);
    theme.floatingCloseButton = theme.titleBar;
    theme.tabStrip = theme.dockBackground;
    theme.tabActive = theme.floatingFrame;
    theme.tabInactive = theme.tabStrip;
    theme.tabOutline = theme.dockBorder;
    theme.tabAccent = DFColorFromHex(0x7DC9CC);
    theme.splitter = theme.dockBorder;
    theme.splitterHandle = DFColorFromHex(0x728692);
    theme.splitterHover = DFColorFromHex(0x8EA2AE);
    theme.splitterDrag = DFColorFromHex(0xAABEC9);
    theme.clientAreaFill = theme.floatingFrame;
    theme.clientAreaBorder = theme.dockBorder;
    theme.controlFill = DFColorFromHex(0x344955);
    theme.selection = DFColorFromHex(0x335D6B);
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

