#pragma once

#include <cmath>
#include <fstream>
#include <istream>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dock_theme.h"

namespace df {

// Configuration contains only explicit overrides. This lets theme switching
// retain user sizing/features while inheriting colors from the next preset.
struct UiConfig {
    std::string preset;
    std::optional<float> cornerRadius;
    std::vector<std::pair<float DockTheme::*, float>> metrics;
    std::vector<std::pair<DFColor DockTheme::*, DFColor>> colors;
    std::vector<std::pair<bool DockTheme::*, bool>> features;
};

namespace ui_config_detail {

inline std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

struct MetricDefinition {
    const char* name;
    float DockTheme::* field;
    float minimum;
    float maximum;
};

inline const std::vector<MetricDefinition>& Metrics()
{
    static const std::vector<MetricDefinition> definitions{
        {"buttonHeight", &DockTheme::buttonHeight, 18.0f, 96.0f},
        {"buttonCornerRadius", &DockTheme::buttonCornerRadius, 0.0f, 48.0f},
        {"controlHeight", &DockTheme::controlHeight, 18.0f, 96.0f},
        {"controlCornerRadius", &DockTheme::controlCornerRadius, 0.0f, 48.0f},
        {"rowHeight", &DockTheme::rowHeight, 18.0f, 128.0f},
        {"spacing", &DockTheme::spacing, 0.0f, 40.0f},
        {"scrollbarWidth", &DockTheme::scrollbarWidth, 6.0f, 32.0f},
        {"scrollStep", &DockTheme::scrollStep, 8.0f, 240.0f},
        {"tabBarHeight", &DockTheme::tabBarHeight, 20.0f, 96.0f},
        {"tabCornerRadius", &DockTheme::tabCornerRadius, 0.0f, 48.0f},
        {"tabShoulderWidth", &DockTheme::tabShoulderWidth, 0.0f, 32.0f},
        {"tabLiftPx", &DockTheme::tabLiftPx, 0.0f, 20.0f},
        {"tabFontScale", &DockTheme::tabFontScale, 0.5f, 2.0f},
        {"fontPixelScale", &DockTheme::fontPixelScale, 1.0f, 4.0f},
        {"splitterHandleScale", &DockTheme::splitterHandleScale, 0.5f, 3.0f},
        {"clientAreaPadding", &DockTheme::clientAreaPadding, 0.0f, 32.0f},
        {"tabClientAreaExtraPadding", &DockTheme::tabClientAreaExtraPadding, 0.0f, 32.0f},
        {"clientAreaCornerRadius", &DockTheme::clientAreaCornerRadius, 0.0f, 48.0f},
        {"clientAreaBorderThickness", &DockTheme::clientAreaBorderThickness, 0.0f, 8.0f}
    };
    return definitions;
}

inline const std::vector<std::pair<const char*, DFColor DockTheme::*>>& Colors()
{
    static const std::vector<std::pair<const char*, DFColor DockTheme::*>> definitions{
        {"titleBar", &DockTheme::titleBar},
        {"dockBackground", &DockTheme::dockBackground},
        {"dockBorder", &DockTheme::dockBorder},
        {"floatingFrame", &DockTheme::floatingFrame},
        {"floatingCloseButton", &DockTheme::floatingCloseButton},
        {"tabStrip", &DockTheme::tabStrip},
        {"tabActive", &DockTheme::tabActive},
        {"tabInactive", &DockTheme::tabInactive},
        {"tabOutline", &DockTheme::tabOutline},
        {"tabAccent", &DockTheme::tabAccent},
        {"tabTextActive", &DockTheme::tabTextActive},
        {"tabTextInactive", &DockTheme::tabTextInactive},
        {"splitter", &DockTheme::splitter},
        {"splitterHandle", &DockTheme::splitterHandle},
        {"splitterHover", &DockTheme::splitterHover},
        {"splitterDrag", &DockTheme::splitterDrag},
        {"overlayPanel", &DockTheme::overlayPanel},
        {"overlayAccent", &DockTheme::overlayAccent},
        {"overlayAccentSoft", &DockTheme::overlayAccentSoft},
        {"clientAreaFill", &DockTheme::clientAreaFill},
        {"clientAreaBorder", &DockTheme::clientAreaBorder},
        {"text", &DockTheme::text},
        {"mutedText", &DockTheme::mutedText},
        {"controlFill", &DockTheme::controlFill},
        {"selection", &DockTheme::selection},
        {"success", &DockTheme::success}
    };
    return definitions;
}

inline const std::vector<std::pair<const char*, bool DockTheme::*>>& Features()
{
    static const std::vector<std::pair<const char*, bool DockTheme::*>> definitions{
        {"smoothFont", &DockTheme::smoothFont},
        {"drawClientArea", &DockTheme::drawClientArea},
        {"drawClientAreaBorder", &DockTheme::drawClientAreaBorder},
        {"drawRoundedClientArea", &DockTheme::drawRoundedClientArea},
        {"drawSplitter", &DockTheme::drawSplitter},
        {"drawSplitterStateColors", &DockTheme::drawSplitterStateColors},
        {"drawSplitterGuideLines", &DockTheme::drawSplitterGuideLines},
        {"drawTitleBarIcons", &DockTheme::drawTitleBarIcons},
        {"drawUndockIcon", &DockTheme::drawUndockIcon},
        {"drawWidgetHoverOutline", &DockTheme::drawWidgetHoverOutline},
        {"drawTabAccent", &DockTheme::drawTabAccent},
        {"drawSteppedTabShape", &DockTheme::drawSteppedTabShape}
    };
    return definitions;
}

inline bool ParseNumber(const std::string& text, float& value)
{
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return !input.fail() && input.eof() && std::isfinite(value);
}

inline bool ParseColor(const std::string& text, DFColor& color)
{
    if (text.size() != 7 || text.front() != '#') return false;
    unsigned int rgb = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        const int digit = c >= '0' && c <= '9' ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return false;
        rgb = (rgb << 4) | static_cast<unsigned int>(digit);
    }
    color = DFColorFromHex(rgb);
    return true;
}

} // namespace ui_config_detail

// Parse and validate everything before publishing the result. On any error,
// result remains unchanged, including when valid entries preceded the error.
inline bool ParseUiConfig(std::istream& input, UiConfig& result, std::string& error)
{
    using namespace ui_config_detail;
    UiConfig parsed;
    std::string section;
    std::string line;
    std::set<std::string> seen;
    std::size_t lineNumber = 0;
    const auto fail = [&](const std::string& message) {
        error = "line " + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = Trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        // Semicolons start inline comments; '#' stays available to RGB values.
        const auto comment = line.find(';');
        if (comment != std::string::npos) line = Trim(line.substr(0, comment));
        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']') return fail("expected a section such as [metrics]");
            section = NormalizeThemeName(Trim(line.substr(1, line.size() - 2)));
            if (section != "theme" && section != "metrics" && section != "colors" && section != "features")
                return fail("unknown section [" + section + "]; use [theme], [metrics], [colors], or [features]");
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) return fail("expected key = value");
        if (section.empty()) return fail("put key = value entries inside a [theme], [metrics], [colors], or [features] section");
        const std::string key = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) return fail("key and value must both be nonempty");
        const std::string qualified = "[" + section + "] " + key;
        if (!seen.insert(qualified).second) return fail("duplicate " + qualified + "; keep one value per key");
        if (section == "theme") {
            if (key != "preset") return fail("unknown " + qualified + "; expected preset");
            if (!IsThemePresetName(value))
                return fail("unknown theme '" + value + "'; use dark, light, slate, ocean, forest, rose, or template");
            parsed.preset = NormalizeThemeName(value);
        } else if (section == "metrics") {
            const MetricDefinition* definition = nullptr;
            for (const auto& candidate : Metrics()) {
                if (key == candidate.name) { definition = &candidate; break; }
            }
            if (key != "cornerRadius" && !definition)
                return fail("unknown " + qualified + "; metric names are case-sensitive (for example buttonHeight or tabBarHeight)");
            float number = 0.0f;
            if (!ParseNumber(value, number)) return fail(qualified + " requires a finite number; got '" + value + "'");
            const float minimum = definition ? definition->minimum : 0.0f;
            const float maximum = definition ? definition->maximum : 48.0f;
            if (number < minimum || number > maximum) {
                std::ostringstream range;
                range << qualified << " must be between " << minimum << " and " << maximum << "; got " << value;
                return fail(range.str());
            }
            if (definition) parsed.metrics.emplace_back(definition->field, number);
            else parsed.cornerRadius = number;
        } else if (section == "colors") {
            DFColor DockTheme::* field = nullptr;
            for (const auto& candidate : Colors()) {
                if (key == candidate.first) { field = candidate.second; break; }
            }
            if (!field) return fail("unknown " + qualified + "; use a DockTheme color name such as tabAccent or controlFill");
            DFColor color;
            if (!ParseColor(value, color)) return fail(qualified + " requires #RRGGBB (six hexadecimal digits)");
            parsed.colors.emplace_back(field, color);
        } else {
            bool DockTheme::* field = nullptr;
            for (const auto& candidate : Features()) {
                if (key == candidate.first) { field = candidate.second; break; }
            }
            if (!field) return fail("unknown " + qualified + "; use a DockTheme feature name such as drawTabAccent or smoothFont");
            const std::string boolean = NormalizeThemeName(value);
            if (boolean != "true" && boolean != "false" && boolean != "1" && boolean != "0")
                return fail(qualified + " requires true, false, 1, or 0");
            parsed.features.emplace_back(field, boolean == "true" || boolean == "1");
        }
    }
    if (input.bad() || (input.fail() && !input.eof())) return fail("could not read configuration input");
    result = std::move(parsed);
    error.clear();
    return true;
}

inline bool LoadUiConfig(const std::string& path, UiConfig& result, std::string& error)
{
    std::ifstream input(path);
    if (!input) {
        error = path + ": could not open UI configuration file";
        return false;
    }
    if (!ParseUiConfig(input, result, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

inline DockTheme ApplyUiConfigOverrides(const UiConfig& config, DockTheme baseTheme)
{
    if (config.cornerRadius) {
        baseTheme.buttonCornerRadius = *config.cornerRadius;
        baseTheme.controlCornerRadius = *config.cornerRadius;
        baseTheme.tabCornerRadius = *config.cornerRadius;
        baseTheme.clientAreaCornerRadius = *config.cornerRadius;
    }
    // Specific radii win over the shared radius regardless of file order.
    for (const auto& overrideValue : config.metrics) baseTheme.*(overrideValue.first) = overrideValue.second;
    for (const auto& overrideValue : config.colors) {
        DFColor& destination = baseTheme.*(overrideValue.first);
        const float alpha = destination.a;
        destination = overrideValue.second;
        destination.a = alpha; // RGB edits retain overlay transparency.
    }
    for (const auto& overrideValue : config.features) baseTheme.*(overrideValue.first) = overrideValue.second;
    return baseTheme;
}

inline DockTheme ThemeFromUiConfig(const UiConfig& config, const DockTheme& fallbackTheme = MakeDarkTheme())
{
    return ApplyUiConfigOverrides(config, config.preset.empty() ? fallbackTheme : ThemeFromName(config.preset));
}

inline bool LoadUiConfig(const std::string& path, const DockTheme& baseTheme, DockTheme& result, std::string& error)
{
    UiConfig config;
    if (!LoadUiConfig(path, config, error)) return false;
    result = ThemeFromUiConfig(config, baseTheme);
    return true;
}

} // namespace df
