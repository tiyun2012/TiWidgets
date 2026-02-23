# widgetsBase

Seed code for an ImGui-driven UI framework with docking, intended for DirectX 12
projects. The utilities are header-only to simplify integration; include
`docking.h`, enable ImGui docking (`ImGuiConfigFlags_DockingEnable`), then
wrap your UI with `BeginDockspace()` / `EndDockspace()` and register panels via
`DrawPanels()`.

## Quick usage
```cpp
#include "widgetsBase/docking.h"

void RenderUI()
{
    wb::DockspaceConfig cfg{};
    wb::BeginDockspace(cfg);

    std::vector<wb::Panel> panels = {
        { "Viewport", [] { /* render content */ } },
        { "Inspector", [] { /* edit properties */ } },
    };
    wb::DrawPanels(panels);

    wb::EndDockspace(cfg.padding);
}
```

## CMake
`widgetsBase/CMakeLists.txt` declares an interface target `widgets_base` that
exposes the `widgetsBase` and root `imgui` include paths and requires C++17:

```cmake
add_subdirectory(widgetsBase)
target_link_libraries(your_app PRIVATE widgets_base)
```

> Note: You still need to compile/link Dear ImGui (and your preferred backend,
> e.g., `imgui_impl_dx12.cpp` and `imgui_impl_win32.cpp`) in your build.

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
  - `Esc`: cancel active action/drag
  - `Ctrl+Tab` / `Ctrl+Shift+Tab`: cycle active tab in hovered tab group
  - `Ctrl+W`: close current tab (or close floating window fallback)

