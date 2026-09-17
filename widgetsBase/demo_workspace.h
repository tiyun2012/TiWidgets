#pragma once

#include "core_types.h"
#include "dock_theme.h"
#include "ui_controls.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

// Sample content belongs to the showcase, while the docking engine stays reusable.
struct DemoWorkspaceState {
    int selected = 3;
    bool showGrid = true;
    bool highlightSelection = true;
    bool showSelectionLabel = true;
    float previewScale = 0.5f;
    double frameMs = 0;
    unsigned long long frames = 0;
    const std::array<const char*, 5> objects{{"Scene", "Camera", "Lighting", "Cube", "Ground"}};
};

class DemoPanel final : public Widget {
public:
    DemoPanel(std::string kind, DemoWorkspaceState& state) : kind_(std::move(kind)), state_(state) {}

    void paint(Canvas& c) override
    {
        const auto& t = df::CurrentTheme();
        const DFRect b = bounds();
        if (b.width < 40 || b.height < 30) return;
        auto text = [&](float x, float y, const std::string& value, const DFColor& color, float scale = 0.85f) {
            if (y < 0 || y + 18 > b.height) return;
            DFDrawText(c, b.x + x, b.y + y,
                DFClipTextToWidth(value, b.width - x - 12, true, scale), color, scale, false);
        };
        auto fill = [&](DFRect r, const DFColor& color, float radius = -1.0f) {
            if (radius < 0) radius = t.controlCornerRadius;
            r.x += b.x; r.y += b.y;
            r.width = std::min(r.width, b.x + b.width - r.x);
            r.height = std::min(r.height, b.y + b.height - r.y);
            if (r.width > 0 && r.height > 0) c.drawRoundedRectangle(r, radius, color);
        };
        if (kind_ == "Hierarchy") {
            text(16, 16, "SCENE EXPLORER", t.mutedText, 0.75f);
            for (size_t i = 0; i < state_.objects.size(); ++i) {
                const float y = 50 + static_cast<float>(i) * t.rowHeight;
                const float rowH = std::max(0.0f, t.rowHeight - 4);
                if (y + rowH > b.height) break;
                if (state_.highlightSelection && state_.selected == static_cast<int>(i)) fill({8, y, b.width - 16, rowH}, t.selection);
                const float iconSize = std::max(0.0f, std::min(9.0f, rowH - 4));
                fill({i == 0 ? 18.0f : 32.0f, y + (rowH - iconSize) * .5f, iconSize, iconSize}, i == 3 ? t.tabAccent : t.mutedText, 2);
                const float textScale = std::min(.85f, std::max(0.2f, (rowH - 2) / (10 * DFTextPixelScale())));
                text(i == 0 ? 40.0f : 54.0f, y + (rowH - 10 * DFTextPixelScale() * textScale) * .5f,
                    state_.objects[i], state_.selected == static_cast<int>(i) ? t.text : t.mutedText, textScale);
            }
            text(16, 80 + static_cast<float>(state_.objects.size()) * t.rowHeight, "5 objects", t.mutedText, 0.75f);
            text(16, b.height - 50, "Click an object to inspect", t.mutedText, 0.7f);
        } else if (kind_ == "Viewport") {
            text(18, 14, "Perspective", t.text);
            if (b.width > 360) text(b.width - 190, 14, "Scene preview", t.mutedText, 0.75f);
            const float cx = b.x + b.width * 0.5f;
            const float cy = b.y + b.height * 0.54f;
            const float radius = std::max(0.0f, std::min(b.width * 0.2f, (b.height - 100) * 0.35f)) * (.75f + .5f * state_.previewScale);
            if (state_.showGrid && b.height > 120) {
                for (int i = -6; i <= 6; ++i) {
                    const float x = cx + static_cast<float>(i) * (b.width - 36) / 12;
                    c.drawLine({x, b.y + 52}, {x, b.y + b.height - 42}, t.dockBorder);
                }
                for (int i = 0; i < 7; ++i) {
                    const float y = b.y + 52 + static_cast<float>(i) * (b.height - 94) / 6;
                    c.drawLine({b.x + 18, y}, {b.x + b.width - 18, y}, t.dockBorder);
                }
            }
            if (radius > 10) {
                const DFPoint top{cx, cy - radius}, left{cx - radius, cy - radius * 0.45f};
                const DFPoint right{cx + radius, cy - radius * 0.45f}, center{cx, cy + radius * 0.1f};
                const DFPoint bottom{cx, cy + radius}, bottomLeft{cx - radius, cy + radius * 0.45f};
                const DFPoint bottomRight{cx + radius, cy + radius * 0.45f};
                const DFColor color = state_.highlightSelection && state_.selected == 3 ? t.tabAccent : t.mutedText;
                for (const auto& edge : std::array<std::pair<DFPoint, DFPoint>, 9>{{
                    {top, left}, {top, right}, {left, center}, {right, center},
                    {left, bottomLeft}, {right, bottomRight}, {center, bottom},
                    {bottomLeft, bottom}, {bottomRight, bottom}}}) c.drawLine(edge.first, edge.second, color, 2);
            }
            if (state_.showSelectionLabel)
                text(18, b.height - 28, std::string("Selected: ") + state_.objects[state_.selected], t.mutedText, 0.75f);
        } else if (kind_ == "Inspector") {
            text(18, 16, std::string("OBJECT / ") + state_.objects[state_.selected], t.text);
            text(18, 52, "Transform", t.mutedText, 0.8f);
            const float fieldW = std::max(40.0f, std::min(150.0f, (b.width - 70) / 3));
            const float switchY = gridSwitchY(b.height);
            const float fieldH = std::min(t.controlHeight, std::max(0.0f,
                b.height >= 140 ? switchY - 94 : b.height - 90));
            const float fieldTextH = 10 * DFTextPixelScale() * .75f;
            for (int i = 0; i < 3; ++i) {
                const float x = 18 + i * (fieldW + 10);
                fill({x, 78, fieldW, fieldH}, t.controlFill);
                if (fieldH >= fieldTextH + 4)
                    text(x + 10, 78 + (fieldH - fieldTextH) * .5f, std::string(1, "XYZ"[i]) + "  0.00", t.text, 0.75f);
            }
            if (b.height >= 140) {
                fill({18, switchY, 36, 20}, state_.showGrid ? t.tabAccent : t.controlFill, 10);
                fill({state_.showGrid ? 36.0f : 21.0f, switchY + 3, 14, 14}, t.clientAreaFill, 7);
                text(66, switchY + 4, "Show viewport grid", t.text, 0.8f);
            }
            text(18, switchY + 45, "Sample values / select objects in Hierarchy", t.mutedText, 0.7f);
        } else if (kind_ == "Console") {
            text(18, 18, "APPLICATION OUTPUT", t.mutedText, 0.75f);
            text(18, 54, "[info] Workspace initialized", t.success, 0.85f);
            text(18, 86, "[info] Drag tabs to rearrange or float panels", t.text, 0.8f);
            text(18, 118, "[info] F1 opens live input diagnostics", t.mutedText, 0.8f);
            text(18, 150, "[info] Ctrl+R restores the default workspace", t.mutedText, 0.8f);
        } else if (kind_ == "Profiler") {
            text(16, 18, "LIVE RENDERING", t.mutedText, 0.75f);
            std::ostringstream timing;
            timing << std::fixed << std::setprecision(1) << state_.frameMs << " ms / frame";
            text(16, 54, timing.str(), t.tabAccent);
            text(16, 86, "Frames: " + std::to_string(state_.frames), t.text, 0.8f);
            text(16, 128, "Includes present and GPU wait", t.mutedText, 0.7f);
        } else if (kind_ == "Assets") {
            text(18, 18, "SAMPLE ASSETS", t.mutedText, 0.75f);
            text(18, 56, "[mesh]      Cube", t.text);
            text(18, 90, "[material]  Default surface", t.text);
            text(18, 124, "[scene]     Workspace demo", t.text);
        } else if (kind_ == "Timeline") {
            text(18, 18, "TIMELINE / SAMPLE TRACK", t.mutedText, 0.75f);
            if (b.height > 110) {
                fill({18, 60, b.width - 36, 36}, t.controlFill);
                text(28, 72, "00:00          00:01          00:02", t.mutedText, 0.8f);
            }
        }
    }

    void handleEvent(Event& event) override
    {
        if (event.type != Event::Type::MouseDown) return;
        const auto& t = df::CurrentTheme();
        if (kind_ == "Hierarchy" && event.x >= 8 && event.x < bounds().width - 8 && event.y >= 50) {
            const int row = static_cast<int>((event.y - 50) / t.rowHeight);
            if (row < static_cast<int>(state_.objects.size()) && event.y < 50 + row * t.rowHeight + t.rowHeight - 4 &&
                50 + row * t.rowHeight + t.rowHeight - 4 <= bounds().height) {
                state_.selected = row;
                event.handled = true;
            }
        } else if (kind_ == "Inspector" && bounds().height >= 140 &&
            DFRect{18, gridSwitchY(bounds().height), std::min(270.0f, bounds().width - 30), 24}.contains({event.x, event.y})) {
            state_.showGrid = !state_.showGrid;
            event.handled = true;
        }
    }

private:
    static float gridSwitchY(float height)
    { return std::min(std::max(132.0f, 94 + df::CurrentTheme().controlHeight), height - 28); }
    std::string kind_;
    DemoWorkspaceState& state_;
};

// A live component showcase; its state is shared with the original scene panels.
class DemoGallery final : public Widget {
public:
    explicit DemoGallery(DemoWorkspaceState& state) : state_(state)
    {
        using namespace df::ui;
        setAcceptsFocus(true);
        auto controls = std::make_unique<StackPanel>();
        controls->emplace<Label>("INTERACTIVE CONTROLS").setMuted(true);
        action_ = &controls->emplace<Button>("Run action", [this] { ++clickCount_; refresh(); });
        action_->setPrimary(true);
        result_ = &controls->emplace<Label>("Actions completed: 0");
        grid_ = &controls->emplace<Toggle>("Show viewport grid", state_.showGrid);
        grid_->setOnChange([this](bool checked) { state_.showGrid = checked; });
        controls->emplace<Label>("Preview scale / drag or use arrow keys").setMuted(true);
        slider_ = &controls->emplace<Slider>(state_.previewScale);
        slider_->setOnChange([this](float value) { state_.previewScale = value; progress_->setValue(value); });
        progress_ = &controls->emplace<ProgressBar>(state_.previewScale);
        controls->emplace<Button>("Disabled button").setEnabled(false);
        auto& local = controls->emplace<Button>("Rounded style override", [this] { ++clickCount_; refresh(); });
        local.styleOverrides().buttonRadius = 14.0f;
        local.styleOverrides().buttonHeight = 38.0f;
        controls->emplace<Label>("Tab to focus / Enter or Space to activate").setMuted(true);
        controlCount_ += controls->children().size();
        tabs_.addTab("Controls", std::make_unique<ScrollView>(std::move(controls)));

        auto settings = std::make_unique<StackPanel>();
        settings->emplace<Label>("WORKSPACE SETTINGS").setMuted(true);
        auto& highlight = settings->emplace<Checkbox>("Highlight selected object", state_.highlightSelection);
        highlight.setOnChange([this](bool checked) { state_.highlightSelection = checked; refresh(); });
        auto& selectionLabel = settings->emplace<Toggle>("Show viewport selection label", state_.showSelectionLabel);
        selectionLabel.setOnChange([this](bool checked) { state_.showSelectionLabel = checked; refresh(); });
        settings->emplace<Label>("Inherited section style").setMuted(true);
        auto group = std::make_unique<StackPanel>();
        group->setPadding(0);
        group->styleOverrides().buttonRadius = 10.0f;
        group->styleOverrides().buttonHeight = 38.0f;
        group->emplace<Button>("Select camera", [this] { state_.selected = 1; refresh(); });
        group->emplace<Button>("Select cube", [this] { state_.selected = 3; refresh(); });
        auto& reset = group->emplace<Button>("Reset preview", [this] {
            state_.previewScale = .5f; state_.showGrid = true; slider_->setValue(.5f); progress_->setValue(.5f); refresh();
        });
        reset.styleOverrides().buttonRadius = 0.0f;
        controlCount_ += group->children().size();
        settings->add(std::move(group));
        settingsSummary_ = &settings->emplace<Label>("");
        settings->emplace<Label>("Shared with Hierarchy, Inspector and Viewport").setMuted(true);
        controlCount_ += settings->children().size();
        tabs_.addTab("Settings", std::make_unique<ScrollView>(std::move(settings)));

        auto list = std::make_unique<StackPanel>();
        list->emplace<Label>("SCROLLABLE ACTION LIST").setMuted(true);
        listStatus_ = &list->emplace<Label>("Choose any item below");
        for (int i = 1; i <= 40; ++i) {
            list->emplace<Button>("Item " + std::to_string(i) + " / select object", [this, i] {
                selectedListItem_ = i; state_.selected = (i - 1) % static_cast<int>(state_.objects.size()); refresh();
            });
        }
        controlCount_ += list->children().size();
        auto scroll = std::make_unique<ScrollView>(std::move(list));
        listScroll_ = scroll.get();
        tabs_.addTab("Scroll list", std::move(scroll));
        refresh();
    }
    df::ui::TabView& tabs() { return tabs_; }
    df::ui::Button& actionButton() { return *action_; }
    df::ui::Slider& slider() { return *slider_; }
    df::ui::ScrollView& scrollView() { return *listScroll_; }
    float scrollOffset() const { return listScroll_->offset(); }
    int clickCount() const { return clickCount_; }
    int selectedListItem() const { return selectedListItem_; }
    size_t controlCount() const { return controlCount_; }
    void setBounds(const DFRect& bounds) override
    {
        Widget::setBounds(bounds);
        tabs_.setBounds({bounds.x + 10, bounds.y + 10, std::max(0.0f, bounds.width - 20), std::max(0.0f, bounds.height - 20)});
    }
    void paint(Canvas& c) override { refresh(); tabs_.paint(c); }
    void handleEvent(Event& event) override
    {
        Event absolute = event; absolute.x += bounds_.x; absolute.y += bounds_.y;
        tabs_.handleEvent(absolute); event.handled = absolute.handled;
    }
protected:
    void onFocusChanged(bool focused) override { if (!focused) tabs_.cancelInteraction(); }
private:
    void refresh()
    {
        grid_->setChecked(state_.showGrid);
        result_->setText("Actions completed: " + std::to_string(clickCount_));
        settingsSummary_->setText(std::string("Highlight ") + (state_.highlightSelection ? "on" : "off") + " / Selection label " + (state_.showSelectionLabel ? "on" : "off"));
        listStatus_->setText(selectedListItem_ ? "Selected item " + std::to_string(selectedListItem_) : "Choose any item below");
    }
    DemoWorkspaceState& state_;
    df::ui::TabView tabs_;
    df::ui::Button* action_ = nullptr;
    df::ui::Label *result_ = nullptr, *settingsSummary_ = nullptr, *listStatus_ = nullptr;
    df::ui::Toggle* grid_ = nullptr;
    df::ui::Slider* slider_ = nullptr;
    df::ui::ProgressBar* progress_ = nullptr;
    df::ui::ScrollView* listScroll_ = nullptr;
    int clickCount_ = 0, selectedListItem_ = 0;
    size_t controlCount_ = 0;
};
