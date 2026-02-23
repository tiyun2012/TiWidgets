#include "dock_framework.h"
#include "dock_layout.h"
#include "dock_splitter.h"
#include "window_manager.h"
#include "dock_widget_impl.h"
#include <iostream>
#include <memory>

// Simple widget that draws a DFColored DFRect
class DFColoredWidget : public Widget {
public:
    DFColoredWidget(const DFColor& DFColor, const std::string& name = "")
        : DFColor_(DFColor), name_(name) {}

    void paint(Canvas& canvas) override {
        canvas.drawRectangle(bounds(), DFColor_);
        std::cout << "Rendering " << name_
                  << " at (" << bounds().x << ", " << bounds().y
                  << ") size " << bounds().width << "x" << bounds().height << "\n";
    }

private:
    DFColor DFColor_;
    std::string name_;
};

int main() {
    std::cout << "=== Docking Framework Demo ===\n\n";

    auto w1 = std::make_unique<df::BasicDockWidget>("Hierarchy");
    w1->setContent(std::make_unique<DFColoredWidget>(DFColor{0.2f,0.3f,0.4f,1.0f}, "Hierarchy"));

    auto w2 = std::make_unique<df::BasicDockWidget>("Viewport");
    w2->setContent(std::make_unique<DFColoredWidget>(DFColor{0.1f,0.1f,0.15f,1.0f}, "Viewport"));

    auto w3 = std::make_unique<df::BasicDockWidget>("Inspector");
    w3->setContent(std::make_unique<DFColoredWidget>(DFColor{0.3f,0.2f,0.3f,1.0f}, "Inspector"));

    auto w4 = std::make_unique<df::BasicDockWidget>("Console");
    w4->setContent(std::make_unique<DFColoredWidget>(DFColor{0.25f,0.25f,0.2f,1.0f}, "Console"));

    auto* p1 = w1.get();
    auto* p2 = w2.get();
    auto* p3 = w3.get();
    auto* p4 = w4.get();

    auto& manager = df::DockManager::instance();
    manager.registerWidget(p1);
    manager.registerWidget(p2);
    manager.registerWidget(p3);
    manager.registerWidget(p4);

    auto root = std::make_unique<df::DockLayout::Node>();
    root->type = df::DockLayout::Node::Type::Split;
    root->vertical = true;
    root->ratio = 0.2f;
    root->first = std::make_unique<df::DockLayout::Node>();
    root->first->type = df::DockLayout::Node::Type::Widget;
    root->first->widget = p1;
    root->second = std::make_unique<df::DockLayout::Node>();
    root->second->type = df::DockLayout::Node::Type::Split;
    root->second->vertical = false;
    root->second->ratio = 0.7f;
    root->second->first = std::make_unique<df::DockLayout::Node>();
    root->second->first->type = df::DockLayout::Node::Type::Widget;
    root->second->first->widget = p2;
    root->second->second = std::make_unique<df::DockLayout::Node>();
    root->second->second->type = df::DockLayout::Node::Type::Split;
    root->second->second->vertical = true;
    root->second->second->ratio = 0.6f;
    root->second->second->first = std::make_unique<df::DockLayout::Node>();
    root->second->second->first->type = df::DockLayout::Node::Type::Widget;
    root->second->second->first->widget = p3;
    root->second->second->second = std::make_unique<df::DockLayout::Node>();
    root->second->second->second->type = df::DockLayout::Node::Type::Widget;
    root->second->second->second->widget = p4;

    df::DockLayout layout;
    layout.setRoot(std::move(root));
    layout.update({0,0,1920,1080});

    df::WindowManager& wm = df::WindowManager::instance();
    auto floating = std::make_unique<df::BasicDockWidget>("Floating");
    floating->setContent(std::make_unique<DFColoredWidget>(DFColor{0.4f,0.2f,0.2f,1.0f}, "Floating"));
    auto* floatingPtr = floating.get();
    manager.registerWidget(floatingPtr);
    wm.createFloatingWindow(floatingPtr, {100,100,400,300});

    Canvas canvas;
    p1->paint(canvas); p2->paint(canvas); p3->paint(canvas); p4->paint(canvas);
    wm.renderAllWindows(canvas);

    std::cout << "\n=== Demo Complete ===\n";
    return 0;
}

