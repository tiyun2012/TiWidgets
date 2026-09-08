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
- The docking renderer reads `DF_THEME` (`dark`, `light`, `slate`, `template`).
- Example:
  `set DF_THEME=slate` then run `dx12_demo.exe`.
- Optional native title-bar color override (for floating host windows):
  `set DF_TITLE_BAR_COLOR=#2D2D30`
- Optional fast-visual preset (reduce extra effects for performance):
  `set DF_FAST_VISUALS=1`
- Presets and editable template live in `widgetsBase/dock_theme.h`.
  Use `MakeTemplateTheme()` as your custom theme template.
- Active tabs share their workspace fill and outline. Defaults disable the
  extra inner border and colored tab-top stripe.
- `fontPixelScale = 1.4` sets text to 70% of the previous scale. DX12 rasterizes
  each font size directly and aligns glyphs to pixels; native hosts use the same
  medium-weight Consolas font.
- `splitterHandleScale = 1.1` makes grips 10% thicker; `splitterHandle`,
  `splitterHover` and `splitterDrag` control their neutral contrast.

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
  - `Ctrl+T`: cycle dark, light and slate palettes
  - `Ctrl+P`: toggle the live profiler
  - `F1`: toggle input diagnostics
  - `Esc`: cancel active action/drag
  - `Ctrl+Tab` / `Ctrl+Shift+Tab`: cycle active tab in hovered tab group
  - `Ctrl+W`: close current tab (or close floating window fallback)

