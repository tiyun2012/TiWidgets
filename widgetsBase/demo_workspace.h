#pragma once

#include "core_types.h"
#include "dock_theme.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

// Sample content belongs to the showcase, while the docking engine stays reusable.
struct DemoWorkspaceState {
    int selected = 3;
    bool showGrid = true;
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
        auto fill = [&](DFRect r, const DFColor& color, float radius = 4.0f) {
            r.x += b.x; r.y += b.y;
            r.width = std::min(r.width, b.x + b.width - r.x);
            r.height = std::min(r.height, b.y + b.height - r.y);
            if (r.width > 0 && r.height > 0) c.drawRoundedRectangle(r, radius, color);
        };
        if (kind_ == "Hierarchy") {
            text(16, 16, "SCENE EXPLORER", t.mutedText, 0.75f);
            for (size_t i = 0; i < state_.objects.size(); ++i) {
                const float y = 50 + static_cast<float>(i) * 34;
                if (y + 30 > b.height) break;
                if (state_.selected == static_cast<int>(i)) fill({8, y, b.width - 16, 30}, t.selection);
                fill({i == 0 ? 18.0f : 32.0f, y + 10, 9, 9}, i == 3 ? t.tabAccent : t.mutedText, 2);
                text(i == 0 ? 40.0f : 54.0f, y + 8, state_.objects[i], state_.selected == static_cast<int>(i) ? t.text : t.mutedText);
            }
            text(16, 250, "5 objects", t.mutedText, 0.75f);
            text(16, b.height - 50, "Click an object to inspect", t.mutedText, 0.7f);
        } else if (kind_ == "Viewport") {
            text(18, 14, "Perspective", t.text);
            if (b.width > 360) text(b.width - 190, 14, "Scene preview", t.mutedText, 0.75f);
            const float cx = b.x + b.width * 0.5f;
            const float cy = b.y + b.height * 0.54f;
            const float radius = std::max(0.0f, std::min(b.width * 0.2f, (b.height - 100) * 0.35f));
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
                const DFColor color = state_.selected == 3 ? t.tabAccent : t.mutedText;
                for (const auto& edge : std::array<std::pair<DFPoint, DFPoint>, 9>{{
                    {top, left}, {top, right}, {left, center}, {right, center},
                    {left, bottomLeft}, {right, bottomRight}, {center, bottom},
                    {bottomLeft, bottom}, {bottomRight, bottom}}}) c.drawLine(edge.first, edge.second, color, 2);
            }
            text(18, b.height - 28, std::string("Selected: ") + state_.objects[state_.selected], t.mutedText, 0.75f);
        } else if (kind_ == "Inspector") {
            text(18, 16, std::string("OBJECT / ") + state_.objects[state_.selected], t.text);
            text(18, 52, "Transform", t.mutedText, 0.8f);
            const float fieldW = std::max(40.0f, std::min(150.0f, (b.width - 70) / 3));
            for (int i = 0; i < 3; ++i) {
                const float x = 18 + i * (fieldW + 10);
                fill({x, 78, fieldW, 32}, t.controlFill);
                text(x + 10, 88, std::string(1, "XYZ"[i]) + "  0.00", t.text, 0.75f);
            }
            if (b.height >= 140) {
                const float switchY = gridSwitchY(b.height);
                fill({18, switchY, 36, 20}, state_.showGrid ? t.tabAccent : t.controlFill, 10);
                fill({state_.showGrid ? 36.0f : 21.0f, switchY + 3, 14, 14}, t.clientAreaFill, 7);
                text(66, switchY + 4, "Show viewport grid", t.text, 0.8f);
            }
            text(18, 177, "Sample values / select objects in Hierarchy", t.mutedText, 0.7f);
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
        if (kind_ == "Hierarchy" && event.x >= 8 && event.x < bounds().width - 8 && event.y >= 50) {
            const int row = static_cast<int>((event.y - 50) / 34);
            if (row < static_cast<int>(state_.objects.size()) && event.y < 50 + row * 34 + 30) {
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
    static float gridSwitchY(float height) { return std::min(132.0f, height - 28); }
    std::string kind_;
    DemoWorkspaceState& state_;
};
