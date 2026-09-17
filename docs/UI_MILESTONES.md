# Modern workspace milestones

The original workspace milestones were completed on 2026-09-09. The configuration
and UI Gallery update is documented separately below. This remains an independent C++17/DX12 framework.
Qt's [main window](https://doc.qt.io/qt-6/qmainwindow.html) and
[Fluent palette patterns](https://doc.qt.io/qt-6/qtquickcontrols-fluentwinui3.html)
provided the UI reference; no Qt dependency was added.

## Development progress

| Milestone | Status | Outcome |
| --- | --- | --- |
| 1. Inspect and preview baseline | Complete | Built with MSVC; recorded the original UI and 9/11 passing tests. |
| 2. Modernize the workspace | Complete | Restored tab input, refreshed palettes/typography, added working workspace actions and sample content. |
| 3. Debug and developer handoff | Complete | Added repeatable milestone gates, isolated test artifacts, before/after previews and debugger checkpoints. |
| 4. Drag/repaint stability | Complete | Buffered native painting, removed repeated window-chrome updates, and added a native move-loop regression. |
| 5. Appearance refinement | Complete | Joined active tabs to their workspace, reduced and sharpened text, strengthened resize grips, and unified neutral frame edges. |
| 6. Configurable UI and reusable controls | Complete | Added editable INI styles, six palettes, inherited controls, and a gallery with content tabs and scrolling. |
| 7. Tab drag position and release | Complete | Preserved the grab point and capture through undocking, allowed dragging outside the main window, recovered lost releases, and deferred native drag rendering. |

The initial failures were obsolete stacked-tab/splitter expectations and a
compression check comparing minimum sizes with the full viewport instead of the
available dock area. Enabling tab input also exposed stale drag, reorder and
floating-window assumptions in automation. These now use actual tab/frame bounds.

## Original workspace changes

- Dark, light and slate palettes cover content, tabs, icons and controls.
- Tabs have 32px strips, readable labels, close buttons and a continuous active-tab/workspace surface.
  Selection, reordering, drag-out and shortcuts are enabled. Reordering uses the
  same tab rectangles as painting. Vertical tabs use keyboard close.
- A toolbar offers workspace reset, theme cycling, profiler and diagnostics.
  A status bar shows current action or the hovered command's keyboard shortcut.
- Hierarchy selection updates the inspector and viewport. The inspector's grid
  switch changes the preview. Transform values, assets and timeline are labeled
  sample content. Profiler values come from actual rendered frames.
- The DX12 backend uses a Windows font atlas rasterized separately at each UI
  size, pixel-aligned glyphs, point sampling and alpha blending.
  Each GPU draw retains its upload buffer until the frame completes, fixing
  corruption when geometry exceeds one batch. Other Canvas implementations
  retain the portable bitmap text fallback.
- Native floating hosts paint and interact with the same sample content. Their
  title bars and close targets match the larger in-canvas floating frames.
- Diagnostics are opt-in with F1 and show live routing counters. Frame/input
  timing history is bounded during interactive use.

## Preview

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -Theme light
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -Theme ocean -Gallery -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -UiConfig config/rounded.ini -Gallery -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -NativeHosts -Profiler -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -Diagnostics
```

The helper captures the actual client area, restores its environment variables,
and closes the process unless `-LeaveOpen` is supplied. It needs an unobstructed
interactive Windows desktop. Its default uses in-canvas floating windows for
repeatable capture; `-NativeHosts` selects separate Win32 floating windows.
Relative `-UiConfig` paths resolve from the repository root. Omitting `-Theme`
uses the preset in that file; an explicit `-Theme` changes the palette while
preserving configured metrics and other overrides. The supported presets are
dark, light, slate, ocean, forest, rose and template.

| Before | Dark workspace | Light workspace | Compact workspace |
| --- | --- | --- | --- |
| [Original](previews/before.png) | [Dark](previews/workspace-dark.png) | [Light](previews/workspace-light.png) | [800 x 560](previews/workspace-compact.png) |

## Repeatable debugging gates

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_ui_milestones.ps1 -Config Debug -CapturePreviews
powershell -ExecutionPolicy Bypass -File .\scripts\check_ui_milestones.ps1 -Config Debug -CapturePreviews -CaptureGallery
powershell -ExecutionPolicy Bypass -File .\scripts\check_ui_milestones.ps1 -Config Release
```

VS Code tasks: **UI: Check Milestones** and **UI: Preview Workspace**.

The validation runner reports four gates independently of the development
milestones above:

| Gate | Checks | Evidence |
| --- | --- | --- |
| M1 | Configure + build | M1.log |
| M2 | Portable layout and docking/resize scenarios | M2.log, core.xml |
| M3 | Workspace controls, theme/configuration cases, native hosts, drag/repaint stability and GPU batching | M3.log, workspace.xml, test-artifacts/ |
| M4 | Six theme/compact captures, optional gallery captures and normal-vs-batch pixel comparison | M4.log, preview-*.png |

`artifacts/milestones/latest-Debug.json` (or `latest-Release.json`) updates before
and after each gate. Timestamped folders retain Markdown/JSON reports, timing,
logs and JUnit results. States are **Pending**, **Running**, **Passed**, **Failed**
and **Skipped**. Failures return exit code 1 and retain later gates as Pending.
Without `-CapturePreviews`, M4 is explicitly Skipped. Screenshot creation is not
an automatic aesthetic approval; inspect the output images.
`-CaptureGallery` requires `-CapturePreviews` and adds `preview-gallery-<theme>.png`
for each of the six palettes. The normal and batch comparison both explicitly
select dark, so a different preset in the configuration file cannot invalidate
the comparison.

Individual scenario:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_event_automation.ps1 -Config Debug -Scenario ui_workspace
powershell -ExecutionPolicy Bypass -File .\scripts\run_event_automation.ps1 -Config Debug -Scenario native_repaint
ctest --test-dir build_dx12 -C Debug -L ui --output-on-failure
```

CTest gives each case its own `build_dx12/test-artifacts/<test-name>/` directory
so concurrent tests do not interleave logs. The event console appends sessions;
use the final session or the run's JUnit outcome when reviewing historical logs.

## Debugger checkpoints

Use function breakpoints or search by symbol; these survive source line changes.

| Investigate | Breakpoints | Expected state |
| --- | --- | --- |
| Toolbar / reset | `DX12Demo::handleWorkspaceChrome`, `resetWorkspace` | Registered panels restored; no active drag. |
| Keyboard actions | `DX12Demo::handleShortcutKey` | Ctrl+R reset, Ctrl+Shift+R reload config, Ctrl+T theme, Ctrl+G gallery, Ctrl+P profiler, F1 diagnostics. |
| Tab selection / reordering | `beginTabGesture`, `handleTabGesture`, `DockLayout::TabRectForIndex` | One active child; pointer hits the same rectangle that was painted. |
| Undock / redock | `undockActiveTab`, `DockManager::endFloatingDrag` | HostType and parent window agree with the layout. |
| Native content | `paintNativeFloatingHost`, `FloatingHostWndProc`, `DemoPanel::handleEvent` | Content receives coordinates relative to its client area. |
| Flicker during/after dragging | `syncNativeFloatingHosts`, `DemoBufferedPaint::present`, `runNativeRepaintChecks` | Unchanged titles/styles are skipped; the OS owns bounds during native moves; each native paint presents one complete buffer. |
| Text / corrupted draws | `DX12Canvas::drawTextScaled`, `flush`, `clear` | Distinct upload storage for each submitted batch; clear occurs after the frame fence. |
| Tab joins / frame appearance | `DrawConnectedTabShape`, `DX12DockWidget::paint`, `FloatingHostWndProc` | Body paints before tabs; the selected tab opens the shared border; native client area covers the entire custom frame. |
| Minimum sizes | `DockLayout::update`, `DockSplitter::updateDrag` | Splitter gaps and tab strips included; compression uses dock bounds. |
| Automated checks | `DX12Demo::runAutomatedEventChecks` | Search for `workspace ... [FAIL]` or inspect JUnit failure output. |

## Historical verification results (2026-09-09)

These results apply to the original workspace and its follow-ups, before the
configuration and UI Gallery update. Re-run the gates for current evidence.

| Check | Result |
| --- | --- |
| MSVC Debug build and CTest | 18/18 passed: 12 core/event tests + 6 UI tests, including native repaint. |
| MSVC Release build and CTest | 18/18 passed; visual capture explicitly skipped for this configuration. |
| DX12 debug validation | No error/corruption messages during Debug workspace rendering. Release reports the unavailable debug layer as skipped. |
| Visual review | Dark, light, slate, 800x560 compact, diagnostics and native profiler captures inspected. |
| GPU batch comparison | Normal and 12,000-extra-quad captures match byte for byte. |
| Legacy edge-case script | All 9 scenarios passed, including mixed, resize/crash, close-all and host transfer. |
| Resource script | Five mixed-event runs; average peak 74.36 MB, maximum 74.92 MB (600 MB threshold). |
| Crash reporter | Intentional access violation produced the expected nonzero exit, crash log and minidump. |
| Milestone failure path | Deliberate invalid build directory returned 1, marked M1 Failed and retained subsequent gates as Pending. |
| Drag/repaint regression | Zero redundant caption or position writes during the tested idle/drag intervals; no background erase; changed titles still update; 100 paints leave GDI handle usage unchanged. |
| Native custom frame | No system-frame inset; all eight resize directions, caption drag and close input pass. |

Local evidence:
- [Debug report](../artifacts/milestones/20260909_064226_636_Debug/report.md)
- [Release report](../artifacts/milestones/20260909_064256_344_Release/report.md)
- [Extended checks](../artifacts/extended-debug-checks.log)
- [Native profiler](../artifacts/visual/native-profiler.png) and [diagnostics](../artifacts/visual/diagnostics.png)

Generated run artifacts are ignored by Git. The representative previews under
`docs/previews/` are included with the source changes. Re-run the commands above
to generate current reports on another machine.

## Drag flicker follow-up

The regression probe reproduced 24 main-window and 48 floating-window title
writes across 24 idle frames, a background erase before native content painting,
and a model-driven position update during the native move loop. These caused
unnecessary window-chrome repainting and exposed intermediate paint states.

Native hosts now compose their client area in a memory bitmap and present it in
one blit. Both window procedures suppress background erasure because their
renderers supply complete frames. Window titles and native frame styles update
only when their values change. During a native move/size loop, the OS rectangle
remains authoritative; the final rectangle is synchronized on exit. Frame
recording also rejects synchronous reentry from window messages.

`dx12_native_repaint` checks those message-level regressions during idle, move
and post-move intervals, verifies that a real title change still updates, and
checks resource lifetime across 100 buffered paints. It passed in Debug and
Release. Screenshot comparison verifies rendered output but cannot by itself
prove the absence of temporal flicker during every interactive drag.

Evidence: [before-fix probe](../artifacts/repaint-before.log),
[Debug checks](../artifacts/flicker-debug-checks.log), and
[Release checks](../artifacts/flicker-release-checks.log).

## Appearance refinement

The reference captures `artifacts/visual/tab1.png` and `tab2.png` showed an
outlined tab sitting above a separately inset, outlined workspace. The active
tab now shares the workspace fill and opens its connecting border, with rounded
corners only on its outer side. The default inner frame and colored tab-top
stripe are disabled. Docked and floating frames use a single neutral 1px outline.

The UI uses one medium-weight Consolas family at 70% of the previous text scale
(`fontPixelScale = 1.4`, previously 2.0). Glyphs at 8–32px are rasterized at their
actual size and placed on whole pixels; the default labels use roughly 10–13px.
The native painter uses the same font, size rounding and character spacing.
Splitter grips use `splitterHandleScale = 1.1` and a stronger neutral color than
their track. Hover and drag shades remain in that neutral palette.

Native floating hosts own the complete client frame through
[`WM_NCCALCSIZE`](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize),
removing the bright system band above the title. Explicit hit tests retain the
eight resize directions, title dragging and the close target. The repaint probe
checks both frame coverage and those interactions, alongside the flicker checks.

Dark, light, slate, compact and native-host previews were reviewed. Debug and
Release each passed all 18 tests; normal and multi-batch previews still match
exactly. Evidence: [Debug checks](../artifacts/appearance-debug-checks.log),
[Release checks](../artifacts/appearance-release-checks.log), and
[native preview](../artifacts/visual/refined-native.png).

## Configurable UI and Gallery (2026-09-10)

[`config/ui.ini`](../config/ui.ini) supplies editable UI settings. `[theme]`
chooses the preset; `[metrics]` controls button/control/tab heights, corner radii,
row spacing, scrolling and text sizing; `[colors]` overrides named palette colors;
and `[features]` toggles rendering options. Omitted values inherit the selected
preset. A common `cornerRadius` applies to buttons, controls, tabs and client
areas; specific radius overrides win independent of file order. Component
heights are independent. See the
[framework configuration example](../widgetsBase/README.md#configuration-and-inheritance).
[`compact.ini`](../config/compact.ini) and [`rounded.ini`](../config/rounded.ini)
provide smaller square and larger rounded profiles for comparison.

The demo reads `DF_UI_CONFIG` when supplied, otherwise looks for `config/ui.ini`
in its working directory and then beside its executable. **Ctrl+Shift+R** reloads
the file. **Ctrl+T** cycles six palettes: dark, light, slate, ocean, forest and rose.
`DF_THEME` can override the file's preset at startup; configuration overrides
remain applied when the palette changes. `template` remains available as a
customizable C++ preset.

**Ctrl+G** opens the UI Gallery as a docking tab. Its Controls, Settings and Scroll
list pages demonstrate buttons, checkboxes, toggles, sliders, progress bars,
nested tabs and scrollable content. `DF_UI_GALLERY=1` opens it at startup;
the preview helper exposes this as `-Gallery`. The controls share an inherited
base for common interaction behavior and resolve styles from the theme through
parent containers to per-control overrides. Applications can compose or extend
the [reusable component library](../widgetsBase/README.md#reusable-controls).

The gallery includes over 60 sample components, including 40 selectable list
items. Settings update the scene highlight, selection label and grid; the slider
changes preview scale. Wheel scrolling, draggable scrollbars and keyboard focus
work in docked panels, in-canvas floating frames and native hosts. Content
coordinates are translated once, with capture retained through a drag and
cancelled when the window loses focus or capture.

| Ocean | Forest | Rose |
| --- | --- | --- |
| [Gallery](previews/gallery-ocean.png) | [Gallery](previews/gallery-forest.png) | [Gallery](previews/gallery-rose.png) |

Validation on 2026-09-10:

| Check | Result |
| --- | --- |
| Debug and Release | 25/25 tests passed in each configuration: 12 docking/event tests and 13 UI/configuration tests. |
| Configuration | Parser validation, transactional errors, corner inheritance, compact/rounded profiles and live reload passed. |
| Controls | Active-page routing, shared state, keyboard activation, rapid clicks, capture loss, scrolling/clipping and metric limits passed. |
| Floating content | Gallery button and wheel input passed in native hosts; in-canvas button input passed with a single coordinate translation. |
| Visual previews | Six workspace and six gallery palettes captured; dark, light, ocean, forest, rose and compact/rounded gallery previews inspected. |
| GPU batching | Normal and stress captures match byte for byte. |

Evidence: [Debug report](../artifacts/milestones/20260910_001821_108_Debug/report.md)
and [Release report](../artifacts/milestones/20260910_001914_718_Release/report.md).
Release visual capture was skipped; the Debug report contains all visual gates.
Reproduce the gallery captures with `check_ui_milestones.ps1 -Config Debug
-CapturePreviews -CaptureGallery`.

## Checkbox and toggle edge refinement

The Settings-page checkbox and toggle exposed aliased curve/checkmark edges.
DX12 now feathers rounded geometry and diagonal strokes, and draws continuous
inset outline rings. Native hosts use antialiased GDI+ paths for the same shapes.
Checkbox corners stay below a circular radius even with a rounded theme;
toggle geometry uses whole-pixel alignment and equal thumb insets.

[Updated Settings preview](previews/boolean-settings.png). Reproduce with:

```powershell
.\scripts\preview_ui.ps1 -Gallery -GalleryPage Settings -Theme slate -LeaveOpen
```

Debug and Release each passed all 25 tests after this refinement. Shape tests
cover control heights 18-96px, checkbox corner limits and toggle symmetry.
Slate, rounded/rose and compact Settings captures were inspected; the normal
and GPU batch-stress Settings captures match exactly. Results are in
`build_dx12/boolean-render-debug.xml` and `boolean-render-release.xml`.

## Tab drag position and release — 2026-09-17

The drag regression reproduced an arbitrary horizontal grab offset (35% of the
panel width), main-client and desktop clamps that detached the panel from the
cursor, and native-host activation that cleared capture during undocking. A
missed button-up left the docking drag active. Native `WM_MOVING` also rendered
and waited for presentation/GPU completion synchronously.

The implementation now retains the original tab grab point, keeps the source
window's capture while showing the new host without activation, and preserves
signed/outside pointer coordinates. Floating panels can extend beyond desktop
edges while the grabbed title point stays with the pointer. Native geometry
updates on mouse input before rendering. Button-free mouse movement cancels a
missed release; capture loss, focus loss, cancel mode and Escape close the drag.

Native move callbacks update docking candidates and queue a redraw. A 16ms timer
coalesces hint rendering and presents without a v-sync wait during the modal
move loop; ordinary frames keep display synchronization. The measured native
move callback dropped from 7.7ms to 0.1ms in the visible Debug probe. This is a
callback measurement, not an end-to-end input-latency benchmark.

| Check | Result |
| --- | --- |
| Debug and Release | 27/27 tests passed in each configuration. |
| New drag scenarios | Both in-canvas and native hosts retain the grab point; negative/outside movement, outside release, missed release, capture/focus loss, cancel mode and Escape pass. |
| Docking hints | Move callbacks do not submit frames; the timer presents queued hints once and stays idle without changes. |
| Visible native hosts | Undocking preserves source capture; the pre-fix visible probe lost it. |
| Real mouse input | Inspector followed an 860px/70px move outside the main window and stayed fixed after physical release and subsequent pointer movement. |

Re-run the regression with:

```powershell
ctest --test-dir build_dx12 -C Debug -R dx12_tab_drag --output-on-failure
powershell -ExecutionPolicy Bypass -File .\scripts\run_event_automation.ps1 -Config Debug -Scenario tab_drag -ShowWindow -SkipCleanLog -SkipCleanCrash
```

Evidence: [before-fix checks](../artifacts/tab-drag-before.log),
[Debug milestones](../artifacts/milestones/20260917_215418_512_Debug/report.md),
[Release milestones](../artifacts/milestones/20260917_215544_461_Release/report.md),
and [real mouse-input check](../artifacts/tab-drag-real-input.log).
Visual capture gates were skipped for this interaction-only change; the native
preview was opened and used for the real mouse-input check.

## Scope remaining

Workspace reset is implemented; serialized layout save/restore is a future
milestone. The sample property fields are read-only. Full Qt-style accessibility,
IME/Unicode shaping, richer editing controls and per-monitor DPI scaling remain
future framework work. The gallery supplies an initial reusable control library.
The atlas currently covers printable ASCII, matching the
existing demo's text scope.
