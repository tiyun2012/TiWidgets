# TiWidgets

Native C++17 docking framework with a DirectX 12 workspace demo.

Preview the UI and check developer milestones:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_ui_milestones.ps1 -Config Debug -CapturePreviews
```

This builds the project, runs docking and workspace tests, captures six theme
presets and a compact preview, and compares normal rendering with a GPU batching stress
capture. Each milestone prints progress and writes logs, JUnit results and
`artifacts/milestones/latest-Debug.json`. Omit `-CapturePreviews` for unattended
checks; the report explicitly marks visual capture as skipped.

Open the interactive preview after building:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -Theme ocean -Gallery -LeaveOpen
powershell -ExecutionPolicy Bypass -File .\scripts\preview_ui.ps1 -UiConfig config/rounded.ini -Gallery -LeaveOpen
```

Edit [config/ui.ini](config/ui.ini) to change button and tab heights, corner
radii, spacing, colors and rendering features. Press **Ctrl+Shift+R** to reload
the file, **Ctrl+T** to cycle dark, light, slate, ocean, forest and rose themes,
and **Ctrl+G** to open the UI Gallery with reusable controls, tabs and scrolling.
The `template` preset remains available for C++ customization.
Try [compact.ini](config/compact.ini) for smaller square controls and
[rounded.ini](config/rounded.ini) for larger rounded controls.

Use `-UiConfig path/to/custom.ini` to preview another configuration. Relative
paths resolve from the repository root. The file selects the theme unless you
pass `-Theme`; changing the theme preserves configured sizes and corners.
Add `-CaptureGallery` to the milestone command alongside `-CapturePreviews`
to capture the gallery in each theme.

[Changes, verification and debug checkpoints](docs/UI_MILESTONES.md).

![Modern workspace](docs/previews/workspace-dark.png)

## Automation Quick Start

Canonical build directory: `build_dx12`

Debug guide:
`docs/DEBUG_FLOW_BREAKPOINTS.md`

VS Code multi-app debug setup:
- Launch config file: `.vscode/launch.json`
- App launcher scripts: `scripts/launch_pws.ps1` (alias), `scripts/launch_app.ps1`

### One-command build + checks
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\auto_build_and_check.ps1 -Config Debug -Reconfigure
```

### Full sweep (edge cases, resources, crash reporter)
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\auto_build_and_check.ps1 -Config Debug -RunEdgeCases -CheckResources -CheckCrashReporter
```

### Full sweep + JSON analysis report
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\auto_build_and_check.ps1 -Config Debug -RunEdgeCases -CheckResources -RunAnalysis
```

Optional IntelliSense context validation in the same run:
`-CheckIntelliSense`

### Nightly mode (full checks + archived artifacts)
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\auto_build_and_check.ps1 -Config Debug -Nightly
```

Optional nightly controls:
`-NightlyRoot artifacts -NightlyKeep 14`

### Event automation only
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_event_automation.ps1 -Config Debug -Scenario baseline
```

### Analyze latest logs
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\analyze_logs.ps1
```

### IntelliSense sanity check (CMake + key targets)
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_intellisense.ps1 -Config Debug -Reconfigure
```

### List available built apps (for debug target selection)
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\launch_pws.ps1 -BuildDir build_dx12 -Config Debug -ListOnly
```

### Launch app interactively (then use "Attach To Running Process")
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\launch_pws.ps1 -BuildDir build_dx12 -Config Debug
```

### Run edge-case scenarios only
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_edge_cases.ps1 -Config Debug
```

### Resource usage check
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_resources.ps1 -Config Debug
```

### Crash reporter smoke test
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_crash_reporter.ps1 -Config Debug
```
