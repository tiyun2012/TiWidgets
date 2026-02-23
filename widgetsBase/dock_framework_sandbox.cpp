#include "dock_framework.h"
#include "dock_layout.h"

int main()
{
    // Basic compile-time/ run-time smoke test of the docking framework skeleton.
    df::DockContainer container;
    auto* left = container.addDockArea(df::DockArea::Position::Left);
    auto* right = container.addDockArea(df::DockArea::Position::Right);

    df::DockWidget w1("Hierarchy");
    df::DockWidget w2("Inspector");
    left->addDockWidget(&w1);
    right->addDockWidget(&w2);

    // Layout update (no real rendering)
    container.updateLayout({0, 0, 1920, 1080});

    // Save/restore placeholders
    auto state = df::DockManager::instance().saveState();
    df::DockManager::instance().restoreState(state);

    return 0;
}

