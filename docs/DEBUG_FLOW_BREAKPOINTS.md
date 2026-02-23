# Debug Flow Breakpoint Guide

This guide lists practical breakpoint sets for the current docking code so developers can follow behavior end-to-end.

## Prerequisites

- Build Debug config:
  - `cmake --build build_dx12 --target dx12_demo --config Debug`
- Open VS Code with CMake configured to `build_dx12`.
- Run with symbols:
  - `build_dx12/bin/Debug/dx12_demo.exe`

## VS Code Target Selection (Long-Term)

For many executables, use these built-in options:

- `Debug Target (Target + Config)` in `.vscode/launch.json`
  - Prompts for build folder, config, and CMake target name.
  - Builds the selected target before launch.
- `Debug EXE Path (Prompt)` in `.vscode/launch.json`
  - Use any executable path, even outside standard target naming.
- `Attach To Running Process` in `.vscode/launch.json`
  - Pair with interactive launcher task/script for dynamic target pick.

Helper script:

- `powershell -ExecutionPolicy Bypass -File .\scripts\launch_pws.ps1 -BuildDir build_dx12 -Config Debug -ListOnly`
- `powershell -ExecutionPolicy Bypass -File .\scripts\launch_pws.ps1 -BuildDir build_dx12 -Config Debug`

## Breakpoint Pack 1: App Startup and Main Loop

Set these first to understand initialization order:

- `widgetsBase/dx12_demo.cpp:2298` `wWinMain`
- `widgetsBase/dx12_demo.cpp:673` `DX12Demo::DX12Demo`
- `widgetsBase/dx12_demo.cpp:698` `DX12Demo::initWindow`
- `widgetsBase/dx12_demo.cpp:730` `DX12Demo::initD3D12`
- `widgetsBase/dx12_demo.cpp:796` `DX12Demo::initDocking`
- `widgetsBase/dx12_demo.cpp:880` `DX12Demo::refreshLayoutState`
- `widgetsBase/dx12_demo.cpp:2073` `DX12Demo::run`
- `widgetsBase/dx12_demo.cpp:1912` `DX12Demo::renderFrame`

Expected flow:

`wWinMain -> ctor -> initWindow -> initD3D12 -> initDocking -> run -> renderFrame`

## Breakpoint Pack 2: Mouse Input Dispatch Pipeline

Use this pack to trace how a Win32 mouse message becomes framework action routing:

- `widgetsBase/dx12_demo.cpp:2103` `DX12Demo::WndProc`
- `widgetsBase/dx12_demo.cpp:2317` `DX12Demo::handleMouseMessage`
- `widgetsBase/dx12_demo.cpp:2351` `DX12Demo::processEvent`
- `widgetsBase/dx12_demo.cpp:2395` `DX12Demo::dispatchMouseEvent`
- `widgetsBase/dx12_demo.cpp:474` `EventConsole::logHandled`

Useful watch expressions:

- `lastDispatchHandler_`
- `activeAction_`
- `leftMouseDown_`
- `event.handled`

## Breakpoint Pack 3: Docked Widget Drag and Undock

This is the primary dock drag path from title bar interaction to floating drag:

- `widgetsBase/dx12_dock_widget.h:83` `DX12DockWidget::handleEvent`
- `widgetsBase/dx12_dock_widget.h:103` `DockManager::startDrag` call site
- `widgetsBase/dx12_dock_widget.h:99` `DockManager::startUndockDrag` call site
- `widgetsBase/dock_framework.cpp:371` `DockManager::startDrag`
- `widgetsBase/dock_framework.cpp:381` `DockManager::updateDrag`
- `widgetsBase/dock_framework.cpp:488` `DockManager::startUndockDrag`
- `widgetsBase/dock_framework.cpp:564` `DockManager::startFloatingDrag`
- `widgetsBase/dock_framework.cpp:580` `DockManager::updateFloatingDrag`
- `widgetsBase/dock_framework.cpp:822` `DockManager::endFloatingDrag`

Expected flow (title drag crossing threshold):

`DX12DockWidget::handleEvent -> startDrag -> updateDrag -> startUndockDrag -> startFloatingDrag -> updateFloatingDrag -> endFloatingDrag`

## Breakpoint Pack 4: Splitter Drag and Constraint Clamping

Use this pack when split ratios or min-size behavior look wrong:

- `widgetsBase/dock_splitter.cpp:183` `DockSplitter::handleEvent`
- `widgetsBase/dock_splitter.cpp:92` `DockSplitter::startDrag`
- `widgetsBase/dock_splitter.cpp:108` `DockSplitter::updateDrag`
- `widgetsBase/dock_splitter.cpp:127` clamp `minFirst/minSecond`
- `widgetsBase/dock_splitter.cpp:151` clamp `firstSize`
- `widgetsBase/dock_layout.h:41` `DockLayout::update`
- `widgetsBase/dock_layout.h:61` `recalculateMinSizes`
- `widgetsBase/dock_layout.h:224` `updateNode`
- `widgetsBase/dock_layout.h:230` `computeSizes` lambda

Useful watch expressions:

- `activeNode_->ratio`
- `activeNode_->minFirstSize`
- `activeNode_->minSecondSize`
- `activeParentBounds_`

## Breakpoint Pack 5: Tab Click, Reorder, Undock

This pack follows tab interactions including close, reorder, and drag-out undock:

- `widgetsBase/dx12_demo.cpp:1630` `DX12Demo::beginTabGesture`
- `widgetsBase/dx12_demo.cpp:1184` `DX12Demo::closeTabNode`
- `widgetsBase/dx12_demo.cpp:1733` `DX12Demo::handleTabGesture`
- `widgetsBase/dx12_demo.cpp:1675` `DX12Demo::undockActiveTab`
- `widgetsBase/dock_framework.cpp:695` tab target collection in floating drag
- `widgetsBase/dock_framework.cpp:894` strict tab drop check in `endFloatingDrag`
- `widgetsBase/dock_layout.h:287` tab layout branch in `updateNode`

Useful watch expressions:

- `tabGesture_.active`
- `tabGesture_.tabIndex`
- `tabGesture_.undocked`
- `node->activeTab`

## Breakpoint Pack 6: Floating Window Lifecycle

Use this for floating host bugs, close behavior, and redock edge cases:

- `widgetsBase/window_manager.cpp:279` `WindowManager::createFloatingWindow`
- `widgetsBase/window_manager.cpp:127` `WindowFrame::handleEvent`
- `widgetsBase/dock_framework.cpp:580` `DockManager::updateFloatingDrag`
- `widgetsBase/dock_framework.cpp:632` `addCandidate` lambda
- `widgetsBase/dock_framework.cpp:772` collect targets from layout root
- `widgetsBase/dock_framework.cpp:822` `DockManager::endFloatingDrag`
- `widgetsBase/dock_framework.cpp:990` tab/center dock branch
- `widgetsBase/dock_framework.cpp:1058` split dock branch
- `widgetsBase/window_manager.cpp:296` `WindowManager::destroyWindow`
- `widgetsBase/dock_framework.cpp:1082` `DockManager::cancelFloatingDrag`

## Breakpoint Pack 7: Native Floating Host (Win32 child window path)

Use this when native floating hosts are enabled (`DF_NATIVE_FLOAT_HOSTS=1`):

- `widgetsBase/dx12_demo.cpp:2172` `DX12Demo::FloatingHostWndProc`
- `widgetsBase/dx12_demo.cpp:2239` `startFloatingDrag` call site in `WM_MOVING`
- `widgetsBase/dx12_demo.cpp:2242` `updateFloatingDrag` call site in `WM_MOVING`
- `widgetsBase/dx12_demo.cpp:2267` `tryDockNativeFloatingHost` call site in `WM_EXITSIZEMOVE`
- `widgetsBase/dx12_demo.cpp:1508` `DX12Demo::tryDockNativeFloatingHost`

## Breakpoint Pack 8: Automation Flow Debugging

Use this when automation scenarios fail or regress:

- `widgetsBase/dx12_demo.cpp:2578` `DX12Demo::runAutomatedEventChecks`
- `widgetsBase/dx12_demo.cpp:2542` `DX12Demo::injectEvent`
- `widgetsBase/dx12_demo.cpp:2613` `validatePanelSizes` lambda
- `widgetsBase/dx12_demo.cpp:3566` scenario switch
- `widgetsBase/dx12_demo.cpp:3676` summary generation

Run automation:

- `powershell -ExecutionPolicy Bypass -File .\scripts\run_event_automation.ps1 -Config Debug -Scenario baseline`

## Low-Noise Conditional Breakpoints

Recommended conditions to avoid stopping on every event:

- In `dispatchMouseEvent` (`widgetsBase/dx12_demo.cpp:2395`):
  - `event.type == Event::Type::MouseDown`
- In `DockManager::endFloatingDrag` (`widgetsBase/dock_framework.cpp:822`):
  - `candidate != nullptr`
- In `DockManager::endFloatingDrag` (`widgetsBase/dock_framework.cpp:990`):
  - `candidate && candidate->zone == DragOverlay::DropZone::Tab`
- In `DockSplitter::updateDrag` (`widgetsBase/dock_splitter.cpp:108`):
  - `activeNode_ && activeNode_->minFirstSize > 0.0f`

## Fast Triage Sequence

If behavior is unclear, use this order:

1. `widgetsBase/dx12_demo.cpp:2395` (`dispatchMouseEvent`)
2. `widgetsBase/dx12_dock_widget.h:83` (`DX12DockWidget::handleEvent`)
3. `widgetsBase/dock_framework.cpp:525` (`DockManager::handleEvent`)
4. `widgetsBase/dock_framework.cpp:580` (`updateFloatingDrag`) or `widgetsBase/dock_splitter.cpp:108` (`updateDrag`)
5. `widgetsBase/dock_layout.h:224` (`updateNode`)
6. `widgetsBase/dx12_demo.cpp:1912` (`renderFrame`)

This sequence shows routing -> state mutation -> layout recompute -> render.
