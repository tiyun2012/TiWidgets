#include "dock_framework.h"
#include "dock_layout.h"
#include "dock_splitter.h"
#include "window_manager.h"
#include "dock_widget_impl.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <conio.h>  // Windows-only demo

// Simple widget that shows its state
class InteractiveWidget : public Widget {
public:
    InteractiveWidget(const DFColor& DFColor, const std::string& name)
        : DFColor_(DFColor), name_(name) {}

    void paint(Canvas& canvas) override {
        canvas.drawRectangle(bounds(), DFColor_);
        DFRect inner = bounds();
        inner.x += 10; inner.y += 10;
        inner.width -= 20; inner.height -= 20;
        canvas.drawRectangle(inner, {DFColor_.r * 1.2f, DFColor_.g * 1.2f, DFColor_.b * 1.2f, 1.0f});
        std::cout << "[RENDER] " << name_
                  << " at (" << bounds().x << ", " << bounds().y
                  << ") size " << bounds().width << "x" << bounds().height;
        if (isDragged_) std::cout << " [DRAGGING]";
        std::cout << "\n";
    }

    void setDragged(bool d) { isDragged_ = d; }

private:
    DFColor DFColor_;
    std::string name_;
    bool isDragged_ = false;
};

class InteractiveDemo {
public:
    void run() {
        std::cout << "=== Interactive Docking Framework Demo ===\n\n";
        std::cout << "Controls:\n"
                  << "  1-4: Select widget (1=Hierarchy, 2=Viewport, 3=Inspector, 4=Console)\n"
                  << "  W/A/S/D: Move selected widget\n"
                  << "  Q/E: Resize selected widget\n"
                  << "  F: Toggle floating window\n"
                  << "  R: Reset layout\n"
                  << "  ESC: Exit\n\n";

        initializeLayout();
        running_ = true;
        while (running_) {
            processInput();
            update();
            render();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            frameCount_++;
        }
    }

private:
    void initializeLayout() {
        auto mk = [](const DFColor& c, const std::string& n) {
            auto w = std::make_unique<df::BasicDockWidget>(n);
            w->setContent(std::make_unique<InteractiveWidget>(c, n));
            return w;
        };

        widgetHolders_[0] = mk({0.2f,0.3f,0.4f,1.0f}, "Hierarchy");
        widgetHolders_[1] = mk({0.1f,0.1f,0.15f,1.0f}, "Viewport");
        widgetHolders_[2] = mk({0.3f,0.2f,0.3f,1.0f}, "Inspector");
        widgetHolders_[3] = mk({0.25f,0.25f,0.2f,1.0f}, "Console");

        for (int i = 0; i < 4; ++i) widgets_[i] = widgetHolders_[i].get();
        auto& mgr = df::DockManager::instance();
        for (auto* w : widgets_) mgr.registerWidget(w);

        auto floating = mk({0.4f,0.2f,0.2f,1.0f}, "Floating");
        floatingWidget_ = floating.get();
        mgr.registerWidget(floatingWidget_);
        floatingHolder_ = std::move(floating);

        createDefaultLayout();
        auto& wm = df::WindowManager::instance();
        floatingWindow_ = wm.createFloatingWindow(floatingWidget_, {200,150,400,300});
        selectedWidget_ = widgets_[0];
        std::cout << "Selected: " << selectedWidget_->title() << "\n";
    }

    void createDefaultLayout() {
        auto root = std::make_unique<df::DockLayout::Node>();
        root->type = df::DockLayout::Node::Type::Split;
        root->vertical = true;
        root->ratio = 0.25f;
        root->first = std::make_unique<df::DockLayout::Node>();
        root->first->type = df::DockLayout::Node::Type::Widget;
        root->first->widget = widgets_[0];
        root->second = std::make_unique<df::DockLayout::Node>();
        root->second->type = df::DockLayout::Node::Type::Widget;
        root->second->widget = widgets_[1];
        layout_.setRoot(std::move(root));
        layout_.update({0,0,800,600});
        splitters_.updateSplitters(layout_.root(), {0,0,800,600});
    }

    void processInput() {
        if (!_kbhit()) return;
        int key = _getch();
        switch (key) {
        case '1': case '2': case '3': case '4':
            selectWidget(key - '1');
            break;
        case 'w': case 'W': moveSelectedWidget(0,-10); break;
        case 's': case 'S': moveSelectedWidget(0,10); break;
        case 'a': case 'A': moveSelectedWidget(-10,0); break;
        case 'd': case 'D': moveSelectedWidget(10,0); break;
        case 'q': case 'Q': resizeSelectedWidget(-10,-10); break;
        case 'e': case 'E': resizeSelectedWidget(10,10); break;
        case 'f': case 'F': toggleFloating(); break;
        case 'r': case 'R': resetLayout(); break;
        case 27: running_ = false; break; // ESC
        default: break;
        }
    }

    void selectWidget(int idx) {
        if (idx >=0 && idx <4 && widgets_[idx]) {
            selectedWidget_ = widgets_[idx];
            std::cout << "Selected widget: " << selectedWidget_->title() << "\n";
        }
    }

    void moveSelectedWidget(float dx, float dy) {
        if (!selectedWidget_) return;
        auto b = selectedWidget_->bounds();
        b.x += dx; b.y += dy;
        selectedWidget_->setBounds(b);
        std::cout << "Moved " << selectedWidget_->title()
                  << " to (" << b.x << "," << b.y << ")\n";
    }

    void resizeSelectedWidget(float dw, float dh) {
        if (!selectedWidget_) return;
        auto b = selectedWidget_->bounds();
        b.width = std::max(50.f, b.width + dw);
        b.height = std::max(50.f, b.height + dh);
        selectedWidget_->setBounds(b);
        std::cout << "Resized " << selectedWidget_->title()
                  << " to " << b.width << "x" << b.height << "\n";
    }

    void toggleFloating() {
        auto& wm = df::WindowManager::instance();
        if (floatingWindow_) {
            wm.destroyWindow(floatingWindow_);
            floatingWindow_ = nullptr;
            std::cout << "Floating window hidden\n";
        } else {
            floatingWindow_ = wm.createFloatingWindow(floatingWidget_, {200,150,400,300});
            std::cout << "Floating window shown\n";
        }
    }

    void resetLayout() {
        createDefaultLayout();
        std::cout << "Layout reset\n";
    }

    void update() {
        df::WindowManager::instance().updateAllWindows();
    }

    void render() {
        std::cout << "\n--- Frame " << frameCount_ << " ---\n";
        Canvas canvas;
        for (auto* w : widgets_) if (w) w->paint(canvas);
        if (floatingWindow_) df::WindowManager::instance().renderAllWindows(canvas);
        splitters_.render(canvas);
        std::cout << "Selected: " << (selectedWidget_ ? selectedWidget_->title() : "None")
                  << " | Floating: " << (floatingWindow_ ? "Visible" : "Hidden") << "\n";
    }

    bool running_ = false;
    int frameCount_ = 0;
    df::DockWidget* widgets_[4] = {nullptr};
    df::DockWidget* selectedWidget_ = nullptr;
    df::DockWidget* floatingWidget_ = nullptr;
    df::WindowFrame* floatingWindow_ = nullptr;
    std::unique_ptr<df::DockWidget> widgetHolders_[4];
    std::unique_ptr<df::DockWidget> floatingHolder_;
    df::DockLayout layout_;
    df::DockSplitter splitters_;
};

int main() {
    InteractiveDemo demo;
    demo.run();
    return 0;
}

