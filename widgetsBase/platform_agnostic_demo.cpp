#include "dock_framework.h"
#include "dock_layout.h"
#include "dock_splitter.h"
#include "window_manager.h"
#include "dock_widget_impl.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Cross-platform keyboard helper
class Keyboard {
public:
    static void init() {
#ifndef _WIN32
        tcgetattr(STDIN_FILENO, &orig_);
        termios raw = orig_;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
    }
    static void cleanup() {
#ifndef _WIN32
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
#endif
    }
    static bool keyPressed() {
#ifdef _WIN32
        return _kbhit() != 0;
#else
        int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
        int c = getchar();
        fcntl(STDIN_FILENO, F_SETFL, oldf);
        if (c != EOF) { ungetc(c, stdin); return true; }
        return false;
#endif
    }
    static int getKey() {
#ifdef _WIN32
        return _getch();
#else
        return getchar();
#endif
    }
private:
#ifndef _WIN32
    static termios orig_;
#endif
};
#ifndef _WIN32
termios Keyboard::orig_{};
#endif

// Console renderer using the base Canvas
class ConsoleCanvas : public Canvas {
public:
    void drawRectangle(const DFRect& rect, const DFColor& DFColor) override {
        // Very lightweight: just log DFRect and dominant DFColor
        char c = ' ';
        if (DFColor.r > DFColor.g && DFColor.r > DFColor.b) c = 'R';
        else if (DFColor.g > DFColor.r && DFColor.g > DFColor.b) c = 'G';
        else if (DFColor.b > DFColor.r && DFColor.b > DFColor.g) c = 'B';
        std::cout << "[DRAW] " << c << " rect at (" << rect.x << "," << rect.y
                  << ") " << rect.width << "x" << rect.height << "\n";
    }
};

class ConsoleWidget : public Widget {
public:
    explicit ConsoleWidget(std::string name) : name_(std::move(name)) {}
    void paint(Canvas& canvas) override {
        canvas.drawRectangle(bounds(), {0.3f, 0.3f, 0.3f, 1.0f});
        std::cout << "[Widget] " << name_ << "\n";
    }
private:
    std::string name_;
};

class GameLoopDemo {
public:
    GameLoopDemo() { Keyboard::init(); setupUI(); }
    ~GameLoopDemo() { Keyboard::cleanup(); }

    void run() {
        std::cout << "=== Docking Framework Game Loop Demo ===\n";
        std::cout << "Controls: 1-5 select, +/- resize, F float, ESC exit\n";
        while (running_) {
            processInput();
            update();
            render();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++frame_;
        }
    }

private:
    void setupUI() {
        const char* names[] = {"Hierarchy","Viewport","Inspector","Console","Tools"};
        for (int i=0;i<5;++i) {
            auto w = std::make_unique<df::BasicDockWidget>(names[i]);
            w->setContent(std::make_unique<ConsoleWidget>(names[i]));
            widgets_[i] = w.get();
            holders_[i] = std::move(w);
            df::DockManager::instance().registerWidget(widgets_[i]);
        }
        createLayout();
        selected_ = widgets_[0];
    }

    void createLayout() {
        auto root = std::make_unique<df::DockLayout::Node>();
        root->type = df::DockLayout::Node::Type::Split;
        root->vertical = true;
        root->ratio = 0.5f;

        root->first = std::make_unique<df::DockLayout::Node>();
        root->first->type = df::DockLayout::Node::Type::Widget;
        root->first->widget = widgets_[0];

        root->second = std::make_unique<df::DockLayout::Node>();
        root->second->type = df::DockLayout::Node::Type::Widget;
        root->second->widget = widgets_[1];

        layout_.setRoot(std::move(root));
        layout_.update({0,0,800,600});
    }

    void processInput() {
        if (!Keyboard::keyPressed()) return;
        int k = Keyboard::getKey();
        switch (k) {
        case '1': case '2': case '3': case '4': case '5':
            selectWidget(k - '1');
            break;
        case '+': case '=': resizeSelected(10,10); break;
        case '-': case '_': resizeSelected(-10,-10); break;
        case 'f': case 'F': toggleFloating(); break;
        case 27: running_ = false; break;
        default: break;
        }
    }

    void selectWidget(int idx) {
        if (idx>=0 && idx<5 && widgets_[idx]) {
            selected_ = widgets_[idx];
            std::cout << "Selected: " << selected_->title() << "\n";
        }
    }

    void resizeSelected(float dw, float dh) {
        if (!selected_) return;
        auto b = selected_->bounds();
        b.width = std::max(50.f, b.width + dw);
        b.height = std::max(50.f, b.height + dh);
        selected_->setBounds(b);
    }

    void toggleFloating() {
        auto& wm = df::WindowManager::instance();
        if (!floating_) {
            auto w = std::make_unique<df::BasicDockWidget>("Floating");
            w->setContent(std::make_unique<ConsoleWidget>("Floating"));
            floatingWidget_ = w.get();
            df::DockManager::instance().registerWidget(floatingWidget_);
            floatingHolder_ = std::move(w);
            floating_ = wm.createFloatingWindow(floatingWidget_, {100,100,300,200});
            std::cout << "Floating window created\n";
        } else {
            wm.destroyWindow(floating_);
            floating_ = nullptr;
            std::cout << "Floating window destroyed\n";
        }
    }

    void update() { df::WindowManager::instance().updateAllWindows(); }

    void render() {
        std::cout << "--- Frame " << frame_ << " ---\n";
        ConsoleCanvas canvas;
        for (auto* w : widgets_) if (w) w->paint(canvas);
        if (floating_) df::WindowManager::instance().renderAllWindows(canvas);
    }

    bool running_ = true;
    int frame_ = 0;
    df::DockWidget* widgets_[5] = {nullptr};
    df::DockWidget* selected_ = nullptr;
    std::unique_ptr<df::DockWidget> holders_[5];
    df::DockLayout layout_;
    df::DockWidget* floatingWidget_ = nullptr;
    std::unique_ptr<df::DockWidget> floatingHolder_;
    df::WindowFrame* floating_ = nullptr;
};

int main() {
    try {
        GameLoopDemo demo;
        demo.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

