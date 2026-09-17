# widgetsBase

Independent C++17 docking framework with a portable `Canvas`/`Widget` interface
and a Windows DirectX 12 demo. It does not require ImGui or Qt.

## Quick usage
```cpp
#include "widgetsBase/dock_layout.h"
#include "widgetsBase/dock_widget_impl.h"

void CreatePanel()
{
    df::BasicDockWidget panel("Inspector");
    panel.setMinimumSize(280, 200);
    df::DockLayout layout;
    auto node = std::make_unique<df::DockLayout::Node>();
    node->widget = &panel;
    layout.setRoot(std::move(node));
    layout.update({0, 0, 800, 600});
    // Keep panel and layout alive in your application; paint through Canvas.
}
```

## CMake
Link the implementation libraries for docking, windows, splitters and rendering:

```cmake
add_subdirectory(widgetsBase)
target_link_libraries(your_app PRIVATE dock_framework dock_components)
```

The `widgets_base` interface target only provides include paths and C++17
requirements. See `simple_demo.cpp` for portable usage and `dx12_demo.cpp` for
the native showcase. See [UI milestones](../docs/UI_MILESTONES.md) for previews,
checks and debugger entry points.

## DX12 demo UX tuning
- The DX12 demo (`dx12_demo`) now uses split sizing constraints so key panels
  keep usable size when the main window is resized.
- Left hierarchy panel is pinned to a fixed width and bottom tab region is
  pinned to a fixed height (with min limits), avoiding squash/stretch behavior.
- `resize_stress` and `resize_crash_stress` automation scenarios validate
  resize synchronization, fixed split sizing, and crash resistance.
- Set `DF_RESIZE_DEBUG=1` to emit detailed resize diagnostics and panel bounds
  into `event_conflicts.log`.
- Live OS resize now defers swap-chain resize until `WM_EXITSIZEMOVE` to avoid
  unstable end-of-resize crashes; input events are blocked while sizing.

## Theme presets and template
- Edit [`config/ui.ini`](../config/ui.ini) for UI metrics, colors and features
  without recompiling. `DF_UI_CONFIG` selects an alternative file.
- The docking renderer reads `DF_THEME` (`dark`, `light`, `slate`, `ocean`,
  `forest`, `rose`, `template`). When set, this selects the palette instead of
  `[theme] preset`; configured metrics and other overrides still apply.
- Example:
  `$env:DF_THEME = 'ocean'` in PowerShell, then run `dx12_demo.exe`.
- Optional native title-bar color override (for floating host windows):
  `set DF_TITLE_BAR_COLOR=#2D2D30`
- Optional fast-visual preset (reduce extra effects for performance):
  `set DF_FAST_VISUALS=1`
- Presets and editable template live in `widgetsBase/dock_theme.h`.
  Use `MakeTemplateTheme()` as your custom theme template.
- Active tabs share their workspace fill and outline. Theme defaults disable the
  extra inner border and colored tab-top stripe; the sample configuration enables
  the stripe with `drawTabAccent`.
- `fontPixelScale = 1.4` sets text to 70% of the previous scale. DX12 rasterizes
  each font size directly and aligns glyphs to pixels; native hosts use the same
  medium-weight Consolas font.
- `splitterHandleScale = 1.1` makes grips 10% thicker; `splitterHandle`,
  `splitterHover` and `splitterDrag` control their neutral contrast.

## Configuration and inheritance

Configuration sections map to theme settings: `[theme]` chooses a `preset`,
`[metrics]` sets sizes, `[colors]` accepts hex colors, and `[features]` controls
boolean rendering options. For example:

```ini
[theme]
preset = ocean

[metrics]
controlHeight = 30
buttonHeight = 34
tabBarHeight = 38
cornerRadius = 8
tabCornerRadius = 5
spacing = 8
scrollbarWidth = 10
scrollStep = 36

[colors]
tabAccent = #70C8FF

[features]
drawRoundedClientArea = true
```

Omitted values inherit the selected preset's defaults. `cornerRadius` supplies a
common radius for buttons, controls, tabs and client areas; specific values such
as `tabCornerRadius` win regardless of their order in the file. Heights are
independent: `controlHeight` does not change `buttonHeight`, `tabBarHeight` or
`rowHeight`. Theme cycling reapplies the configuration to the selected palette.
Keys are case-sensitive; invalid keys or out-of-range values report the file and
line while leaving the current theme unchanged. The default file lists the
supported keys and ranges.

Try [`compact.ini`](../config/compact.ini) for smaller square controls or
[`rounded.ini`](../config/rounded.ini) for spacious rounded controls:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -UiConfig config/rounded.ini -Gallery -LeaveOpen
```

The demo looks for `config/ui.ini` in its working directory, then beside the
executable. The build copies the default configuration beside the executable.
For a fixed file location, set `DF_UI_CONFIG` to an absolute path; the preview
helper does this automatically. Press `Ctrl+Shift+R` to load file edits.

## Reusable controls

[`ui_controls.h`](ui_controls.h) provides header-only `df::ui` controls:
`Label`, `Button`, `Checkbox`, `Toggle`, `Slider` and `ProgressBar`, with
`StackPanel`, `ScrollView` and `TabView` containers. They derive from `Control`,
which supplies enabled/visible state, focus, hover, press handling and clipping.
Containers own their children. Styles resolve from the active theme, through
the owning parent, then through each control's optional `styleOverrides()`.
Changing themes updates inherited values while explicit overrides remain.

```cpp
#include "widgetsBase/ui_controls.h"

auto page = std::make_unique<df::ui::StackPanel>();
page->styleOverrides().spacing = 12.0f;
auto& apply = page->emplace<df::ui::Button>("Apply", [] { /* action */ });
apply.styleOverrides().buttonHeight = 40.0f;

df::ui::TabView tabs;
tabs.addTab("Controls", std::make_unique<df::ui::ScrollView>(std::move(page)));
tabs.setBounds({0, 0, 400, 300});
// Keep tabs alive; call tabs.paint(canvas) and tabs.handleEvent(event).
// Event coordinates use the same coordinate space as the control bounds.
```

Extend `Control` and override `paintControl()` and `activate()` to add a control
that retains the shared behavior. The demo's UI Gallery has Controls, Settings
and Scroll list pages. Tab/Shift+Tab moves focus and reveals controls inside the
scroll view; Enter/Space activates buttons and toggles; arrow keys adjust sliders
or focused tabs. PageUp/PageDown scrolls, and Ctrl+PageUp/PageDown switches pages.

## Visual abstraction options
- Global feature flags live in `DockTheme` (`widgetsBase/dock_theme.h`):
  - `drawClientArea`, `drawRoundedClientArea`, `drawClientAreaBorder`
  - `drawSplitter`, `drawSplitterStateColors`
  - `drawTitleBarIcons`, `drawWidgetHoverOutline`
- Per-widget overrides now live in abstract `DockWidget` (`widgetsBase/dock_framework.h`):
  - `setVisualOptions(...)`
  - `setFastVisuals(true)` to disable rounded/border client-area extras for that widget.

## DX12 demo interaction UX
- Tabs now render hover feedback and have per-tab close hit targets.
- Drag/drop overlays highlight tab-docking drop zones while moving floating windows.
- Dock widgets draw a subtle hover outline when idle.
- Keyboard shortcuts:
  - `Ctrl+R`: restore the default workspace, including closed panels
  - `Ctrl+Shift+R`: reload the UI configuration
  - `Ctrl+T`: cycle dark, light, slate, ocean, forest and rose palettes
  - `Ctrl+G`: toggle the UI Gallery
  - `Ctrl+P`: toggle the live profiler
  - `F1`: toggle input diagnostics
  - `Esc`: cancel active action/drag
  - `Ctrl+Tab` / `Ctrl+Shift+Tab`: cycle active tab in hovered tab group
  - `Ctrl+W`: close current tab (or close floating window fallback)

