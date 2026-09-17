#include "ui_config.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

df::UiConfig Parse(const std::string& text)
{
    df::UiConfig config;
    std::string error;
    std::istringstream input(text);
    Require(df::ParseUiConfig(input, config, error), error);
    Require(error.empty(), "successful parsing must clear a previous error");
    return config;
}

void CheckInvalid(const std::string& text, const std::string& expectedDiagnostic)
{
    df::UiConfig retained = Parse("[theme]\npreset=forest\n[metrics]\nbuttonHeight=42\n");
    std::istringstream input(text);
    std::string error;
    Require(!df::ParseUiConfig(input, retained, error), "invalid configuration was accepted: " + text);
    Require(error.find(expectedDiagnostic) != std::string::npos, "unexpected diagnostic: " + error);
    Require(retained.preset == "forest", "failed parse changed selected theme");
    Require(df::ThemeFromUiConfig(retained).buttonHeight == 42.0f, "failed parse changed retained overrides");
}

void CheckProfiles(const std::string& configDirectory)
{
    for (const char* file : {"ui.ini", "compact.ini", "rounded.ini"}) {
        df::UiConfig config;
        std::string error;
        Require(df::LoadUiConfig(configDirectory + "/" + file, config, error), error);
        Require(!config.preset.empty(), "profile must choose a preset");
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto config = Parse(
            "\xEF\xBB\xBF; UTF-8 BOM and CRLF are supported\r\n"
            "[theme]\r\npreset = OcEaN\r\n"
            "[metrics]\nbuttonHeight=44 ; taller action buttons\n"
            "tabCornerRadius=3\ncornerRadius=11\ncontrolHeight=38\n"
            "[colors]\ntabAccent=#12aBef\noverlayAccentSoft=#123456\n"
            "[features]\ndrawTabAccent=TRUE\nsmoothFont=0\n");
        const auto ocean = df::ThemeFromUiConfig(config);
        Require(config.preset == "ocean", "preset should normalize case");
        Require(ocean.buttonHeight == 44.0f && ocean.controlHeight == 38.0f, "metrics were not applied");
        Require(ocean.buttonCornerRadius == 11.0f && ocean.controlCornerRadius == 11.0f &&
                ocean.clientAreaCornerRadius == 11.0f && ocean.tabCornerRadius == 3.0f,
                "specific radius must override common radius regardless of file order");
        Require(ocean.tabAccent.r == DFColorFromHex(0x12ABEF).r &&
                ocean.tabAccent.g == DFColorFromHex(0x12ABEF).g, "RGB value was not parsed");
        Require(ocean.overlayAccentSoft.a == df::MakeOceanTheme().overlayAccentSoft.a,
                "RGB edits must preserve overlay transparency");
        Require(ocean.drawTabAccent && !ocean.smoothFont, "feature overrides were not applied");

        const auto light = df::ApplyUiConfigOverrides(config, df::MakeLightTheme());
        Require(light.buttonHeight == 44.0f, "cycling must retain metric overrides");
        Require(light.titleBar.r == df::MakeLightTheme().titleBar.r, "cycling must inherit the next palette");
        Require(light.tabAccent.g == ocean.tabAccent.g, "cycling must retain explicit color overrides");
        const auto reversed = df::ThemeFromUiConfig(Parse("[metrics]\ncornerRadius=11\ntabCornerRadius=3\n"));
        Require(reversed.tabCornerRadius == ocean.tabCornerRadius, "radius precedence depends on input order");

        const auto defaults = df::ThemeFromUiConfig(Parse("# only comments\n"), df::MakeRoseTheme());
        Require(defaults.titleBar.g == df::MakeRoseTheme().titleBar.g, "empty config must inherit fallback theme");
        for (const auto* name : df::ThemePresetNames()) {
            Require(df::IsThemePresetName(name), "shared preset list contains an invalid preset");
            Require(Parse(std::string("[theme]\npreset=") + name).preset == name, "preset was rejected");
        }
        Require(df::ThemePresetNames().size() == 6, "expected six distinct cycling palettes");
        Require(Parse("[theme]\npreset=template\n").preset == "template", "template must remain configurable");

        CheckInvalid("[metrics]\nbuttonHeight=40\nrowHeight=-1\n", "line 3:");
        CheckInvalid("[metrics]\nbuttonHeight=nan\n", "finite number");
        CheckInvalid("[metrics]\nbuttonHeight=inf\n", "finite number");
        CheckInvalid("[metrics]\nbuttonHeight=30px\n", "finite number");
        CheckInvalid("[metrics]\nbuttonHeight=1e90\n", "finite number");
        CheckInvalid("[metrics]\nfontPixelScale=0.5\n", "between 1 and 4");
        CheckInvalid("[metrics]\ncornerRadius=49\n", "between 0 and 48");
        CheckInvalid("[metrics]\nbuttonheight=30\n", "case-sensitive");
        CheckInvalid("[colors]\ntabAccent=#12345G\n", "#RRGGBB");
        CheckInvalid("[colors]\ntabAccent=#ABC\n", "#RRGGBB");
        CheckInvalid("[colors]\nmissing=#123456\n", "unknown [colors]");
        CheckInvalid("[features]\nsmoothFont=yes\n", "true, false, 1, or 0");
        CheckInvalid("[features]\nmissing=true\n", "unknown [features]");
        CheckInvalid("[theme]\npreset=unknown\n", "unknown theme");
        CheckInvalid("[theme]\nname=dark\n", "expected preset");
        CheckInvalid("[metrics]\nbuttonHeight=30\nbuttonHeight=32\n", "duplicate");
        CheckInvalid("[unknown]\n", "unknown section");
        CheckInvalid("[metrics\n", "expected a section");
        CheckInvalid("[metrics]\nbuttonHeight 30\n", "expected key = value");
        CheckInvalid("buttonHeight=30\n", "inside a");
        CheckInvalid("[theme]\npreset=\n", "nonempty");

        // A file error must preserve the caller's active theme/configuration.
        df::DockTheme retainedTheme = df::MakeForestTheme();
        retainedTheme.buttonHeight = 42.0f;
        std::string error;
        Require(!df::LoadUiConfig("__missing_ui_config_for_test__.ini", df::MakeDarkTheme(), retainedTheme, error),
                "missing file unexpectedly loaded");
        Require(retainedTheme.buttonHeight == 42.0f, "file error replaced active theme");
        Require(error.find("__missing_ui_config_for_test__.ini") != std::string::npos, "file error lacks path");

        if (argc > 1) CheckProfiles(argv[1]);
        std::cout << "UI configuration: ALL CHECKS PASSED\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "UI configuration test failed: " << exception.what() << '\n';
        return 1;
    }
}
