#include "demo_workspace.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < .01f; }
Event pointer(Event::Type type, float x, float y)
{ Event event(type); event.x = x; event.y = y; return event; }
void click(Widget& widget, float x, float y)
{
    auto down = pointer(Event::Type::MouseDown, x, y); widget.handleEvent(down);
    auto up = pointer(Event::Type::MouseUp, x, y); widget.handleEvent(up);
}
void key(Widget& widget, Event::Type type, int code, bool shift = false)
{ Event event(type); event.key = code; event.shift = shift; widget.handleEvent(event); }

class RecordingCanvas : public Canvas {
public:
    std::vector<DFRect> rectangles;
    std::vector<DFRect> roundedRectangles;
    std::vector<float> roundedRadii;
    std::vector<DFRect> textBounds;
    std::vector<std::string> labels;
    void drawRectangle(const DFRect& r, const DFColor&) override { rectangles.push_back(r); }
    void drawRoundedRectangle(const DFRect& r, float radius, const DFColor&) override { rectangles.push_back(r); roundedRectangles.push_back(r); roundedRadii.push_back(radius); }
    void drawTextScaled(float x, float y, const std::string& value, const DFColor&, float scale, bool) override
    { const DFRect r{x, y, value.size() * DFGlyphAdvancePx(scale), DFGlyphHeightPx(scale)}; rectangles.push_back(r); textBounds.push_back(r); labels.push_back(value); }
};

void interactionAndInheritance()
{
    using namespace df::ui;
    df::MutableTheme() = df::MakeDarkTheme();
    StackPanel parent;
    int clicks = 0;
    auto& button = parent.emplace<Button>("Apply", [&] { ++clicks; });
    parent.setBounds({20, 30, 240, 200});
    require(near(button.preferredHeight(), df::CurrentTheme().buttonHeight), "theme height inherited");
    parent.styleOverrides().buttonHeight = 42.0f;
    parent.styleOverrides().buttonRadius = 11.0f;
    require(near(button.preferredHeight(), 42) && near(button.style().buttonRadius, 11), "parent overrides inherited");
    button.styleOverrides().buttonRadius = 0.0f;
    require(near(button.style().buttonRadius, 0), "explicit zero overrides parent radius");
    df::MutableTheme().controlHeight = 46;
    require(near(button.style().height, 46), "theme changes reach existing controls");
    parent.setBounds({20, 30, 240, 200});
    click(parent, button.bounds().x + 10, button.bounds().y + 10);
    require(clicks == 1 && button.isFocused(), "mouse activation and focus");
    key(parent, Event::Type::KeyDown, 32);
    key(parent, Event::Type::KeyDown, 32);
    require(clicks == 1, "keyboard repeat does not activate before release");
    key(parent, Event::Type::KeyUp, 32);
    require(clicks == 2, "keyboard activation once on release");
    auto down = pointer(Event::Type::MouseDown, button.bounds().x + 10, button.bounds().y + 10); parent.handleEvent(down);
    auto up = pointer(Event::Type::MouseUp, 500, 500); parent.handleEvent(up);
    require(clicks == 2 && !button.pressed(), "release outside cancels click");
    parent.setEnabled(false);
    click(parent, button.bounds().x + 10, button.bounds().y + 10);
    require(clicks == 2 && !button.enabled() && !button.isFocused(), "disabled parent suppresses interaction and focus");
    parent.setEnabled(true); button.setVisible(false);
    click(parent, button.bounds().x + 10, button.bounds().y + 10);
    require(clicks == 2, "hidden controls ignore input");
    parent.inheritFrom(&button);
    require(parent.parent() == nullptr, "style parent cycle rejected");
    StackPanel clipped; clipped.setPadding(0);
    clipped.emplace<Button>("Visible");
    clipped.emplace<Button>("Clipped", [&] { ++clicks; });
    clipped.setBounds({0, 0, 200, 30});
    click(clipped, 10, 54);
    require(clicks == 2, "stack rejects clicks in overflowing clipped children");
}

void sliderAndToggle()
{
    using namespace df::ui;
    Slider slider(.5f); slider.setBounds({10, 20, 200, 32});
    int changes = 0; slider.setOnChange([&](float) { ++changes; });
    auto down = pointer(Event::Type::MouseDown, 18, 30); slider.handleEvent(down);
    require(near(slider.value(), 0), "slider pointer sets value");
    auto move = pointer(Event::Type::MouseMove, 800, 30); slider.handleEvent(move);
    require(near(slider.value(), 1), "slider capture clamps outside track");
    auto up = pointer(Event::Type::MouseUp, 800, 30); slider.handleEvent(up);
    key(slider, Event::Type::KeyDown, 37);
    require(near(slider.value(), .95f), "slider left arrow");
    key(slider, Event::Type::KeyDown, 36);
    require(near(slider.value(), 0) && changes == 4, "slider home and callbacks");
    key(slider, Event::Type::KeyDown, 32);
    move = pointer(Event::Type::MouseMove, 200, 30); slider.handleEvent(move);
    key(slider, Event::Type::KeyUp, 32);
    require(near(slider.value(), 0) && changes == 4, "keyboard press does not start a slider pointer drag");
    Toggle toggle("Grid"); toggle.setBounds({10, 20, 200, 32});
    bool grid = false; toggle.setOnChange([&](bool value) { grid = value; });
    click(toggle, 30, 30);
    require(toggle.checked() && grid, "toggle inherits checkbox activation");
}

void booleanShapes()
{
    using namespace df::ui;
    for (float height : {18.0f, 24.0f, 32.0f, 40.0f, 96.0f}) {
        Checkbox checkbox("Highlight", true);
        checkbox.styleOverrides().radius = 48.0f;
        checkbox.setBounds({10, 20, 240, height});
        RecordingCanvas box; checkbox.paint(box);
        require(!box.roundedRectangles.empty(), "checked box paints an indicator");
        require(near(box.roundedRectangles.front().width, box.roundedRectangles.front().height), "checkbox stays square at all control heights");
        require(box.roundedRadii.front() < box.roundedRectangles.front().width * .5f,
            "large inherited corners cannot turn a checkbox into a radio button");
        Toggle toggle("Selection label"); toggle.setBounds({10, 20, 240, height});
        RecordingCanvas off; toggle.paint(off);
        toggle.setChecked(true);
        RecordingCanvas on; toggle.paint(on);
        require(off.roundedRectangles.size() == 2 && on.roundedRectangles.size() == 2, "toggle paints track and thumb in both states");
        const auto track = on.roundedRectangles[0], left = off.roundedRectangles[1], right = on.roundedRectangles[1];
        require(near(left.y, right.y) && near(left.width, left.height) && near(right.width, right.height), "toggle thumb stays circular and vertically aligned");
        require(near(off.roundedRadii[1], left.width * .5f) && near(on.roundedRadii[1], right.width * .5f) && near(on.roundedRadii[0], track.height * .5f), "toggle preserves circular thumbs and capsule track");
        require(near(left.x - track.x, track.x + track.width - right.x - right.width), "on and off thumb endpoints use equal insets");
        require(near(track.width, std::round(track.width)), "toggle width avoids fractional edge seams");
    }
}

void scrollAndClip()
{
    using namespace df::ui;
    df::MutableTheme() = df::MakeDarkTheme();
    auto rows = std::make_unique<StackPanel>();
    int clicks = 0;
    for (int i = 0; i < 20; ++i) rows->emplace<Button>("Row " + std::to_string(i), [&] { ++clicks; });
    ScrollView scroll(std::move(rows)); scroll.setBounds({100, 50, 250, 130});
    auto wheel = pointer(Event::Type::MouseWheel, 120, 80); wheel.wheelDelta = -1;
    scroll.handleEvent(wheel);
    require(wheel.handled && near(scroll.offset(), df::CurrentTheme().scrollStep), "wheel detent scrolls down");
    auto outside = pointer(Event::Type::MouseWheel, 20, 20); outside.wheelDelta = -2;
    scroll.handleEvent(outside);
    require(!outside.handled && near(scroll.offset(), df::CurrentTheme().scrollStep), "wheel outside viewport ignored");
    scroll.setOffset(100000);
    require(near(scroll.offset(), scroll.maximumOffset()), "scroll clamps bottom");
    wheel.handled = false; scroll.handleEvent(wheel);
    require(!wheel.handled, "wheel bubbles at bottom");
    scroll.setOffset(-40); require(near(scroll.offset(), 0), "scroll clamps top");
    const auto thumb = scroll.scrollbarThumb();
    auto down = pointer(Event::Type::MouseDown, thumb.x + thumb.width * .5f, thumb.y + 5); scroll.handleEvent(down);
    auto drag = pointer(Event::Type::MouseMove, thumb.x + 5, 400); scroll.handleEvent(drag);
    auto up = pointer(Event::Type::MouseUp, thumb.x + 5, 400); scroll.handleEvent(up);
    require(near(scroll.offset(), scroll.maximumOffset()), "thumb drag clamps bottom outside viewport");
    scroll.setOffset(19);
    RecordingCanvas canvas; scroll.paint(canvas);
    require(!canvas.rectangles.empty(), "scroll paints visible rows");
    for (const auto& rect : canvas.rectangles) {
        require(rect.x >= 99.99f && rect.y >= 49.99f && rect.x + rect.width <= 350.01f && rect.y + rect.height <= 180.01f,
            "all scroll drawing stays inside clip bounds");
    }
    // A partly visible first row still extends above the viewport. It must not
    // accept a click in the clipped portion or a captured release outside it.
    click(scroll, 130, 44); require(clicks == 0, "clipped row does not accept pointer input");
    down = pointer(Event::Type::MouseDown, 130, 58); scroll.handleEvent(down);
    up = pointer(Event::Type::MouseUp, 130, 44); scroll.handleEvent(up);
    require(clicks == 0, "captured release outside viewport cancels activation");
    scroll.setBounds({100, 50, 250, 3000});
    require(near(scroll.offset(), 0) && near(scroll.maximumOffset(), 0), "resize removes obsolete scroll offset");
}

void demoMetricRanges()
{
    df::MutableTheme() = df::MakeDarkTheme();
    DemoWorkspaceState state;
    DemoPanel inspector("Inspector", state);
    for (float controlHeight : {18.0f, 32.0f, 40.0f, 96.0f}) {
        df::MutableTheme().controlHeight = controlHeight;
        for (float panelHeight : {140.0f, 190.0f, 320.0f}) {
            inspector.setBounds({0, 0, 400, panelHeight});
            RecordingCanvas canvas; inspector.paint(canvas);
            require(canvas.roundedRectangles.size() == 5, "Inspector retains three fields and a grid toggle");
            const auto toggle = canvas.roundedRectangles[3];
            for (size_t i = 0; i < 3; ++i)
                require(canvas.roundedRectangles[i].y + canvas.roundedRectangles[i].height + 15.99f <= toggle.y,
                    "configured Inspector fields do not overlap the toggle");
            const bool before = state.showGrid;
            click(inspector, toggle.x + 10, toggle.y + 10);
            require(state.showGrid != before, "resized Inspector toggle hit test follows drawing");
        }
    }
    df::MutableTheme().rowHeight = 18;
    DFSetTextPixelScale(2.6f);
    DemoPanel hierarchy("Hierarchy", state); hierarchy.setBounds({0, 0, 400, 300});
    RecordingCanvas canvas; hierarchy.paint(canvas);
    for (const auto& rect : canvas.roundedRectangles) {
        const float rowY = 50 + std::floor((rect.y - 50) / 18) * 18;
        require(rect.y >= rowY && rect.y + rect.height <= rowY + 14.01f, "compact hierarchy icons and highlights fit rows");
    }
    for (size_t i = 0; i < canvas.labels.size(); ++i) {
        for (size_t row = 0; row < state.objects.size(); ++row) if (canvas.labels[i] == state.objects[row]) {
            const auto r = canvas.textBounds[i]; const float rowY = 50 + row * 18.0f;
            require(r.y >= rowY && r.y + r.height <= rowY + 14.01f, "compact hierarchy text fits rows at large font scale");
        }
    }
    df::MutableTheme() = df::MakeDarkTheme(); DFSetTextPixelScale(df::CurrentTheme().fontPixelScale);
}

void tabsAndGallery()
{
    using namespace df::ui;
    TabView tabs;
    auto first = std::make_unique<StackPanel>(); int firstClicks = 0, secondClicks = 0;
    auto& firstButton = first->emplace<Button>("First", [&] { ++firstClicks; });
    auto second = std::make_unique<StackPanel>();
    auto& secondButton = second->emplace<Button>("Second", [&] { ++secondClicks; });
    tabs.addTab("One", std::make_unique<ScrollView>(std::move(first)));
    tabs.addTab("Two", std::make_unique<ScrollView>(std::move(second)));
    tabs.setBounds({0, 0, 300, 200});
    click(tabs, firstButton.bounds().x + 4, firstButton.bounds().y + 4);
    require(firstClicks == 1 && firstButton.isFocused(), "active tab routes to its control");
    tabs.setActiveIndex(1);
    require(!firstButton.isFocused(), "switching tabs clears inactive focus");
    key(tabs, Event::Type::KeyDown, 32); key(tabs, Event::Type::KeyUp, 32);
    require(firstClicks == 1 && secondClicks == 0, "inactive page receives no keyboard activation");
    click(tabs, secondButton.bounds().x + 4, secondButton.bounds().y + 4);
    require(firstClicks == 1 && secondClicks == 1, "only active page receives click");
    tabs.setFocused(true); secondButton.setFocused(false);
    key(tabs, Event::Type::KeyDown, 37);
    require(tabs.activeIndex() == 0, "tab left arrow selects previous page");
    key(tabs, Event::Type::KeyDown, 9);
    require(firstButton.isFocused() && !secondButton.isFocused(), "Tab traverses active page only");

    DemoWorkspaceState state; DemoGallery gallery(state); gallery.setBounds({150, 90, 410, 230});
    const auto button = gallery.actionButton().bounds();
    click(gallery, button.x - gallery.bounds().x + 10, button.y - gallery.bounds().y + 10);
    require(gallery.clickCount() == 1 && gallery.controlCount() >= 50, "gallery adapts local dock coordinates and has full sample set");
    auto doubleClick = pointer(Event::Type::MouseDoubleClick, button.x - gallery.bounds().x + 10, button.y - gallery.bounds().y + 10);
    gallery.handleEvent(doubleClick);
    auto release = pointer(Event::Type::MouseUp, doubleClick.x, doubleClick.y); gallery.handleEvent(release);
    require(gallery.clickCount() == 2, "double-click second press activates on release");
    gallery.tabs().setActiveIndex(1);
    auto* settings = dynamic_cast<StackPanel*>(dynamic_cast<ScrollView*>(gallery.tabs().activePage())->content());
    for (size_t i : {size_t(1), size_t(2)}) {
        const auto r = settings->children()[i]->bounds();
        click(gallery, r.x - gallery.bounds().x + 10, r.y - gallery.bounds().y + 10);
    }
    require(!state.highlightSelection && !state.showSelectionLabel, "settings control actual shared preview options");
    DemoPanel viewport("Viewport", state); viewport.setBounds({0, 0, 400, 300});
    RecordingCanvas preview; viewport.paint(preview);
    for (const auto& label : preview.labels) require(label.find("Selected:") != 0, "selection label setting changes viewport output");
    gallery.tabs().setActiveIndex(2);
    auto wheel = pointer(Event::Type::MouseWheel, 40, 110); wheel.wheelDelta = -2; gallery.handleEvent(wheel);
    require(gallery.scrollOffset() > 0, "gallery list scrolls");
    // Keyboard traversal reveals controls beyond the visible list area.
    gallery.tabs().setFocused(true);
    for (int i = 0; i < 15; ++i) key(gallery, Event::Type::KeyDown, 9);
    require(gallery.scrollOffset() > 250, "keyboard focus reveals offscreen list controls");
}
}

int main()
{
    try {
        interactionAndInheritance(); sliderAndToggle(); booleanShapes(); scrollAndClip(); demoMetricRanges(); tabsAndGallery();
        std::cout << "ALL UI CONTROL CHECKS PASSED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "UI control check failed: " << error.what() << '\n';
        return 1;
    }
}
