#pragma once

#include "core_types.h"
#include "dock_theme.h"
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace df::ui {

inline bool isPointerPress(Event::Type type)
{ return type == Event::Type::MouseDown || type == Event::Type::MouseDoubleClick; }

inline DFRect intersect(const DFRect& a, const DFRect& b)
{
    const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0.0f, std::min(a.x + a.width, b.x + b.width) - x),
        std::max(0.0f, std::min(a.y + a.height, b.y + b.height) - y)};
}

// A portable clip adapter, also nestable around another ClippedCanvas. Fully
// visible primitives keep the renderer's normal rounded geometry and text atlas.
class ClippedCanvas final : public Canvas {
public:
    ClippedCanvas(Canvas& target, DFRect clip) : target_(target), clip_(clip) {}
    void drawRectangle(const DFRect& rect, const DFColor& color) override
    {
        const DFRect r = intersect(rect, clip_);
        if (r.width > 0 && r.height > 0) target_.drawRectangle(r, color);
    }
    void drawRoundedRectangle(const DFRect& rect, float radius, const DFColor& color) override
    {
        if (contains(rect)) { target_.drawRoundedRectangle(rect, radius, color); return; }
        const DFRect r = intersect(rect, clip_);
        if (r.width <= 0 || r.height <= 0) return;
        const float rad = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
        if (rad < 0.5f) { drawRectangle(rect, color); return; }
        for (float y = r.y; y < r.y + r.height; y += 1.0f) {
            const float h = std::min(1.0f, r.y + r.height - y);
            const float middle = y + h * 0.5f;
            const float dy = std::max({rect.y + rad - middle, middle - (rect.y + rect.height - rad), 0.0f});
            const float inset = rad - std::sqrt(std::max(0.0f, rad * rad - dy * dy));
            drawRectangle({rect.x + inset, y, std::max(0.0f, rect.width - 2 * inset), h}, color);
        }
    }
    void drawRoundedRectangleOutline(const DFRect& rect, float radius, const DFColor& color, float thickness = 1) override
    {
        if (contains(rect)) target_.drawRoundedRectangleOutline(rect, radius, color, thickness);
        else Canvas::drawRoundedRectangleOutline(rect, radius, color, thickness);
    }
    void drawLine(const DFPoint& a, const DFPoint& b, const DFColor& color, float thickness = 1) override
    {
        const float half = std::max(0.0f, thickness * 0.5f);
        if (clip_.width <= 2 * half || clip_.height <= 2 * half) return;
        const float dx = b.x - a.x, dy = b.y - a.y;
        float low = 0, high = 1;
        auto edge = [&](float p, float q) {
            if (std::abs(p) < 0.00001f) return q >= 0;
            const float t = q / p;
            if (p < 0) low = std::max(low, t); else high = std::min(high, t);
            return low <= high;
        };
        if (edge(-dx, a.x - clip_.x - half) && edge(dx, clip_.x + clip_.width - half - a.x) &&
            edge(-dy, a.y - clip_.y - half) && edge(dy, clip_.y + clip_.height - half - a.y))
            target_.drawLine({a.x + low * dx, a.y + low * dy}, {a.x + high * dx, a.y + high * dy}, color, thickness);
    }
    void drawText(float x, float y, const std::string& text, const DFColor& color) override
    { drawTextScaled(x, y, text, color, 1, DFTextSmooth()); }
    void drawTextScaled(float x, float y, const std::string& text, const DFColor& color, float scale, bool smooth) override
    {
        // Skip vertically cut text instead of switching between native and bitmap
        // fonts at a scroll boundary. Horizontal clipping retains complete glyphs.
        // Native font descenders extend below the portable 7-pixel cap height.
        const float inkHeight = 10.0f * DFTextPixelScale() * std::clamp(scale, .2f, 4.0f);
        if (y - 1.0f < clip_.y || y + inkHeight > clip_.y + clip_.height) return;
        const float advance = DFGlyphAdvancePx(scale);
        const size_t first = static_cast<size_t>(std::max(0.0f, std::ceil((clip_.x - x) / advance)));
        if (first >= text.size()) return;
        const float start = x + first * advance;
        const int count = DFMaxCharsForWidth(clip_.x + clip_.width - start, scale);
        if (count > 0) target_.drawTextScaled(start, y, text.substr(first, static_cast<size_t>(count)), color, scale, smooth);
    }
private:
    bool contains(const DFRect& r) const
    { return r.x >= clip_.x && r.y >= clip_.y && r.x + r.width <= clip_.x + clip_.width && r.y + r.height <= clip_.y + clip_.height; }
    Canvas& target_;
    DFRect clip_;
};

struct ControlStyle {
    DFColor fill, text, mutedText, accent, border, selection;
    float height = 32, buttonHeight = 30, radius = 4, buttonRadius = 4;
    float spacing = 10, rowHeight = 34, scrollbarWidth = 10, scrollStep = 34;
    float tabHeight = 32, tabRadius = 4;
};

struct StyleOverrides {
    std::optional<DFColor> fill, text, mutedText, accent, border, selection;
    std::optional<float> height, buttonHeight, radius, buttonRadius, spacing, rowHeight;
    std::optional<float> scrollbarWidth, scrollStep, tabHeight, tabRadius;
};

// Shared interaction, focus, visibility, and style behavior. Containers own their
// children and wire the parent chain; overrides stay live when themes change.
class Control : public Widget {
public:
    Control() = default;
    void setEnabled(bool enabled) { enabled_ = enabled; if (!enabled) cancelInteraction(); }
    bool enabled() const { return enabled_ && (!parent_ || parent_->enabled()); }
    void setVisible(bool visible) { visible_ = visible; if (!visible) cancelInteraction(); }
    bool visible() const { return visible_ && (!parent_ || parent_->visible()); }
    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    StyleOverrides& styleOverrides() { return overrides_; }
    const StyleOverrides& styleOverrides() const { return overrides_; }
    Control* parent() const { return parent_; }
    void inheritFrom(Control* parent)
    {
        for (auto* p = parent; p; p = p->parent_) if (p == this) return;
        parent_ = parent;
    }
    ControlStyle style() const
    {
        const auto& t = CurrentTheme();
        ControlStyle s = parent_ ? parent_->style() : ControlStyle{t.controlFill, t.text, t.mutedText,
            t.tabAccent, t.dockBorder, t.selection, t.controlHeight, t.buttonHeight,
            t.controlCornerRadius, t.buttonCornerRadius, t.spacing, t.rowHeight,
            t.scrollbarWidth, t.scrollStep, t.tabBarHeight, t.tabCornerRadius};
#define DF_STYLE_OVERRIDE(field) if (overrides_.field) s.field = *overrides_.field
        DF_STYLE_OVERRIDE(fill); DF_STYLE_OVERRIDE(text); DF_STYLE_OVERRIDE(mutedText);
        DF_STYLE_OVERRIDE(accent); DF_STYLE_OVERRIDE(border); DF_STYLE_OVERRIDE(selection);
        DF_STYLE_OVERRIDE(height); DF_STYLE_OVERRIDE(buttonHeight); DF_STYLE_OVERRIDE(radius);
        DF_STYLE_OVERRIDE(buttonRadius); DF_STYLE_OVERRIDE(spacing); DF_STYLE_OVERRIDE(rowHeight);
        DF_STYLE_OVERRIDE(scrollbarWidth); DF_STYLE_OVERRIDE(scrollStep); DF_STYLE_OVERRIDE(tabHeight); DF_STYLE_OVERRIDE(tabRadius);
#undef DF_STYLE_OVERRIDE
        return s;
    }
    virtual float preferredHeight() const { return style().height; }
    virtual void cancelInteraction()
    { pressed_ = hovered_ = keyboardPressed_ = false; setFocused(false); }
    virtual void collectFocusTargets(std::vector<Control*>& targets)
    { if (visible() && enabled() && acceptsFocus()) targets.push_back(this); }
    void paint(Canvas& canvas) override
    {
        if (!visible() || bounds_.width <= 0 || bounds_.height <= 0) return;
        ClippedCanvas clip(canvas, bounds_);
        paintControl(clip);
    }
    void handleEvent(Event& event) override
    {
        if (!visible() || !enabled()) { cancelInteraction(); return; }
        const bool inside = bounds_.width > 0 && bounds_.height > 0 && bounds_.contains({event.x, event.y});
        if (event.type == Event::Type::MouseMove || event.type == Event::Type::MouseDrag) hovered_ = inside;
        if (!acceptsFocus() || event.handled) return;
        if (isPointerPress(event.type) && inside) {
            pressed_ = true; keyboardPressed_ = false; setFocused(true); event.handled = true;
        } else if (event.type == Event::Type::MouseUp && pressed_ && !keyboardPressed_) {
            pressed_ = false; event.handled = true; if (inside) activate();
        } else if (isFocused() && (event.key == 13 || event.key == 32)) {
            if (event.type == Event::Type::KeyDown) {
                pressed_ = keyboardPressed_ = true; event.handled = true;
            } else if (event.type == Event::Type::KeyUp && keyboardPressed_) {
                pressed_ = keyboardPressed_ = false; event.handled = true; activate();
            }
        }
    }
protected:
    bool pointerPressed() const { return pressed_ && !keyboardPressed_; }
    virtual void paintControl(Canvas&) {}
    virtual void activate() {}
    void onFocusChanged(bool focused) override { if (!focused) pressed_ = keyboardPressed_ = false; }
    DFColor foreground() const { return enabled() ? style().text : style().mutedText; }
    void text(Canvas& c, const std::string& value, DFRect rect, const DFColor& color, float scale = 0.85f) const
    {
        const auto label = DFClipTextToWidth(value, std::max(0.0f, rect.width), true, scale);
        DFDrawText(c, rect.x, DFTextBaselineYForRect(rect, scale), label, color, scale, CurrentTheme().smoothFont);
    }
    void focusOutline(Canvas& c, float radius) const
    { if (isFocused() && enabled()) c.drawRoundedRectangleOutline(bounds_, radius, style().accent, 1.5f); }
private:
    Control* parent_ = nullptr;
    StyleOverrides overrides_{};
    bool enabled_ = true, visible_ = true, hovered_ = false, pressed_ = false, keyboardPressed_ = false;
};

class Label : public Control {
public:
    explicit Label(std::string value = {}) : value_(std::move(value)) {}
    void setText(std::string value) { value_ = std::move(value); }
    const std::string& label() const { return value_; }
    void setMuted(bool muted) { muted_ = muted; }
protected:
    void paintControl(Canvas& c) override { text(c, value_, bounds_, muted_ ? style().mutedText : foreground()); }
private:
    std::string value_;
    bool muted_ = false;
};

class Button : public Control {
public:
    explicit Button(std::string label = {}, std::function<void()> callback = {}) : label_(std::move(label)), callback_(std::move(callback))
    { setAcceptsFocus(true); }
    void setText(std::string label) { label_ = std::move(label); }
    void setOnClick(std::function<void()> callback) { callback_ = std::move(callback); }
    void setPrimary(bool primary) { primary_ = primary; }
    float preferredHeight() const override { return style().buttonHeight; }
protected:
    void activate() override { if (callback_) callback_(); }
    void paintControl(Canvas& c) override
    {
        const auto s = style();
        const auto fill = pressed() ? s.selection : hovered() && enabled() ? s.border : primary_ && enabled() ? s.selection : s.fill;
        c.drawRoundedRectangle(bounds_, s.buttonRadius, fill);
        if (primary_) c.drawRoundedRectangleOutline(bounds_, s.buttonRadius, s.accent);
        text(c, label_, {bounds_.x + 12, bounds_.y, std::max(0.0f, bounds_.width - 24), bounds_.height}, foreground());
        focusOutline(c, s.buttonRadius);
    }
private:
    std::string label_;
    std::function<void()> callback_;
    bool primary_ = false;
};

class Checkbox : public Control {
public:
    explicit Checkbox(std::string label = {}, bool checked = false) : label_(std::move(label)), checked_(checked) { setAcceptsFocus(true); }
    bool checked() const { return checked_; }
    void setChecked(bool checked) { checked_ = checked; }
    void setOnChange(std::function<void(bool)> callback) { callback_ = std::move(callback); }
protected:
    void activate() override { checked_ = !checked_; if (callback_) callback_(checked_); }
    void paintControl(Canvas& c) override
    {
        const auto s = style();
        const float side = std::max(0.0f, std::min(20.0f, bounds_.height - 6));
        const DFRect box{bounds_.x + 3, std::round(bounds_.y + (bounds_.height - side) * 0.5f), side, side};
        // A checkbox remains a rounded square even when the theme uses pill corners.
        const float radius = std::clamp(s.radius, 0.0f, side * .2f);
        c.drawRoundedRectangle(box, radius, checked_ ? s.selection : s.fill);
        c.drawRoundedRectangleOutline(box, radius, checked_ ? s.accent : s.border);
        if (checked_) {
            const DFPoint start{box.x + side * .23f, box.y + side * .5f};
            const DFPoint joint{box.x + side * .43f, box.y + side * .7f};
            const DFPoint end{box.x + side * .77f, box.y + side * .28f};
            const float stroke = std::min(2.0f, side * .12f);
            c.drawLine(start, joint, foreground(), stroke);
            c.drawLine(joint, end, foreground(), stroke);
            // Cover the join between the two strokes without a sharp inner seam.
            c.drawRoundedRectangle({joint.x - stroke * .5f, joint.y - stroke * .5f, stroke, stroke}, stroke * .5f, foreground());
        }
        text(c, label_, {bounds_.x + 32, bounds_.y, std::max(0.0f, bounds_.width - 36), bounds_.height}, foreground());
        focusOutline(c, s.radius);
    }
    const std::string& label() const { return label_; }
private:
    std::string label_;
    bool checked_ = false;
    std::function<void(bool)> callback_;
};

class Toggle : public Checkbox {
public:
    using Checkbox::Checkbox;
protected:
    void paintControl(Canvas& c) override
    {
        const auto s = style();
        const float h = std::max(0.0f, std::min(22.0f, bounds_.height - 6));
        const DFRect track{bounds_.x + 3, std::round(bounds_.y + (bounds_.height - h) * .5f), std::round(h * 1.8f), h};
        c.drawRoundedRectangle(track, h * .5f, checked() ? s.accent : s.fill);
        const float knob = std::max(0.0f, h - 6);
        c.drawRoundedRectangle({track.x + (checked() ? track.width - knob - 3 : 3), track.y + 3, knob, knob}, knob * .5f, checked() ? CurrentTheme().clientAreaFill : s.mutedText);
        text(c, label(), {bounds_.x + 52, bounds_.y, std::max(0.0f, bounds_.width - 56), bounds_.height}, foreground());
        focusOutline(c, s.radius);
    }
};

class Slider : public Control {
public:
    explicit Slider(float value = 0.5f) { setAcceptsFocus(true); setValue(value); }
    float value() const { return value_; }
    void setValue(float value) { value_ = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
    void setOnChange(std::function<void(float)> callback) { callback_ = std::move(callback); }
    void handleEvent(Event& e) override
    {
        if (!visible() || !enabled() || e.handled) { Control::handleEvent(e); return; }
        if (isFocused() && e.type == Event::Type::KeyDown) {
            if (e.key == 37 || e.key == 40) { change(value_ - .05f); e.handled = true; }
            else if (e.key == 38 || e.key == 39) { change(value_ + .05f); e.handled = true; }
            else if (e.key == 36 || e.key == 35) { change(e.key == 36 ? 0.0f : 1.0f); e.handled = true; }
        }
        Control::handleEvent(e);
        if (pointerPressed() && (isPointerPress(e.type) || e.type == Event::Type::MouseMove || e.type == Event::Type::MouseDrag)) {
            change((e.x - bounds_.x - 8) / std::max(1.0f, bounds_.width - 16)); e.handled = true;
        }
    }
protected:
    void paintControl(Canvas& c) override
    {
        const auto s = style();
        const DFRect track{bounds_.x + 8, bounds_.y + bounds_.height * .5f - 3, std::max(0.0f, bounds_.width - 16), 6};
        c.drawRoundedRectangle(track, 3, s.fill);
        c.drawRoundedRectangle({track.x, track.y, track.width * value_, track.height}, 3, enabled() ? s.accent : s.mutedText);
        c.drawRoundedRectangle({track.x + track.width * value_ - 7, track.y - 4, 14, 14}, s.radius, foreground());
        focusOutline(c, s.radius);
    }
private:
    void change(float value) { const float old = value_; setValue(value); if (old != value_ && callback_) callback_(value_); }
    float value_ = .5f;
    std::function<void(float)> callback_;
};

class ProgressBar : public Control {
public:
    explicit ProgressBar(float value = .5f) { setValue(value); }
    void setValue(float value) { value_ = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0; }
    float value() const { return value_; }
protected:
    void paintControl(Canvas& c) override
    {
        const auto s = style();
        c.drawRoundedRectangle(bounds_, s.radius, s.fill);
        c.drawRoundedRectangle({bounds_.x, bounds_.y, bounds_.width * value_, bounds_.height}, s.radius, s.selection);
        text(c, std::to_string(static_cast<int>(std::round(value_ * 100))) + "%", {bounds_.x + 10, bounds_.y, std::max(0.0f, bounds_.width - 20), bounds_.height}, foreground());
    }
private:
    float value_ = .5f;
};

class StackPanel : public Control {
public:
    Control& add(std::unique_ptr<Control> child)
    {
        child->inheritFrom(this); children_.push_back(std::move(child)); arrange(); return *children_.back();
    }
    template<class T, class... Args> T& emplace(Args&&... args)
    { auto child = std::make_unique<T>(std::forward<Args>(args)...); T& ref = *child; add(std::move(child)); return ref; }
    const std::vector<std::unique_ptr<Control>>& children() const { return children_; }
    void setPadding(float padding) { padding_ = std::max(0.0f, padding); arrange(); }
    float preferredHeight() const override
    {
        float result = padding_ * 2; size_t count = 0;
        for (const auto& child : children_) if (child->visible()) { result += child->preferredHeight(); ++count; }
        return result + (count > 0 ? (count - 1) * style().spacing : 0);
    }
    void setBounds(const DFRect& bounds) override { Control::setBounds(bounds); arrange(); }
    void cancelInteraction() override { Control::cancelInteraction(); capture_ = nullptr; for (auto& child : children_) child->cancelInteraction(); }
    void collectFocusTargets(std::vector<Control*>& targets) override
    { if (visible() && enabled()) for (auto& child : children_) child->collectFocusTargets(targets); }
    void handleEvent(Event& e) override
    {
        if (!visible() || !enabled()) { cancelInteraction(); return; }
        arrange();
        if (e.handled) return;
        if (e.type == Event::Type::KeyDown || e.type == Event::Type::KeyUp || e.type == Event::Type::TextInput) {
            for (auto& child : children_) { child->handleEvent(e); if (e.handled) break; }
            return;
        }
        if (isPointerPress(e.type)) {
            cancelInteraction();
            if (!bounds_.contains({e.x, e.y})) return;
            for (auto& child : children_) if (child->visible() && child->bounds().contains({e.x, e.y})) {
                child->handleEvent(e); if (e.handled) { capture_ = child.get(); break; }
            }
        } else if (capture_ && (e.type == Event::Type::MouseMove || e.type == Event::Type::MouseDrag || e.type == Event::Type::MouseUp)) {
            capture_->handleEvent(e); if (e.type == Event::Type::MouseUp) capture_ = nullptr;
        } else {
            for (auto& child : children_) {
                child->handleEvent(e); if (e.handled) break;
            }
        }
    }
protected:
    void paintControl(Canvas& c) override { arrange(); for (auto& child : children_) child->paint(c); }
private:
    void arrange()
    {
        float y = bounds_.y + padding_;
        for (auto& child : children_) if (child->visible()) {
            const float h = std::max(0.0f, child->preferredHeight());
            child->setBounds({bounds_.x + padding_, y, std::max(0.0f, bounds_.width - padding_ * 2), h});
            y += h + style().spacing;
        }
    }
    std::vector<std::unique_ptr<Control>> children_;
    Control* capture_ = nullptr;
    float padding_ = 12;
};

class ScrollView : public Control {
public:
    explicit ScrollView(std::unique_ptr<Control> content) : content_(std::move(content))
    { if (content_) content_->inheritFrom(this); setAcceptsFocus(true); }
    Control* content() const { return content_.get(); }
    float offset() const { return offset_; }
    float maximumOffset() const { return std::max(0.0f, contentHeight() - std::max(0.0f, bounds_.height)); }
    void setOffset(float offset) { offset_ = std::isfinite(offset) ? std::clamp(offset, 0.0f, maximumOffset()) : 0; arrange(); }
    DFRect viewport() const
    { return {bounds_.x, bounds_.y, std::max(0.0f, bounds_.width - (maximumOffset() > 0 ? style().scrollbarWidth + 4 : 0)), std::max(0.0f, bounds_.height)}; }
    DFRect scrollbarThumb() const
    {
        const float height = std::max(0.0f, bounds_.height);
        const float thumbHeight = std::min(height, std::max(24.0f, height * height / std::max(1.0f, contentHeight())));
        return {bounds_.x + std::max(0.0f, bounds_.width - style().scrollbarWidth),
            bounds_.y + (maximumOffset() > 0 ? offset_ / maximumOffset() * (height - thumbHeight) : 0),
            std::min(bounds_.width, style().scrollbarWidth), thumbHeight};
    }
    void ensureVisible(const DFRect& rect)
    {
        if (rect.y < bounds_.y) setOffset(offset_ + rect.y - bounds_.y);
        else if (rect.y + rect.height > bounds_.y + bounds_.height) setOffset(offset_ + rect.y + rect.height - bounds_.y - bounds_.height);
    }
    void setBounds(const DFRect& bounds) override { Control::setBounds(bounds); arrange(); }
    void cancelInteraction() override { Control::cancelInteraction(); dragging_ = contentCapture_ = false; if (content_) content_->cancelInteraction(); }
    void collectFocusTargets(std::vector<Control*>& targets) override
    {
        if (!visible() || !enabled()) return;
        const size_t count = targets.size(); if (content_) content_->collectFocusTargets(targets);
        if (count == targets.size()) Control::collectFocusTargets(targets);
    }
    void handleEvent(Event& e) override
    {
        if (!visible() || !enabled()) { cancelInteraction(); return; }
        if (e.handled) return;
        arrange();
        if (e.type == Event::Type::MouseWheel && bounds_.contains({e.x, e.y})) {
            // Nested views get first chance, allowing their wheel to bubble at a bound.
            if (content_ && viewport().contains({e.x, e.y})) content_->handleEvent(e);
            if (!e.handled) { const float before = offset_; setOffset(offset_ - e.wheelDelta * style().scrollStep); e.handled = before != offset_; }
            return;
        }
        if (e.type == Event::Type::KeyDown || e.type == Event::Type::KeyUp || e.type == Event::Type::TextInput) {
            if (content_) content_->handleEvent(e);
            if (!e.handled && e.type == Event::Type::KeyDown && hasFocus()) {
                if (e.key == 33 || e.key == 34) { setOffset(offset_ + bounds_.height * .85f * (e.key == 33 ? -1 : 1)); e.handled = true; }
                else if (isFocused() && (e.key == 36 || e.key == 35)) { setOffset(e.key == 36 ? 0 : maximumOffset()); e.handled = true; }
            }
            return;
        }
        if (dragging_) {
            if (e.type == Event::Type::MouseMove || e.type == Event::Type::MouseDrag) {
                const float travel = bounds_.height - scrollbarThumb().height;
                setOffset(dragStartOffset_ + (e.y - dragStartY_) * maximumOffset() / std::max(1.0f, travel)); e.handled = true;
            } else if (e.type == Event::Type::MouseUp) { dragging_ = false; e.handled = true; }
            return;
        }
        if (isPointerPress(e.type) && bounds_.contains({e.x, e.y})) {
            cancelInteraction(); setFocused(true);
            if (maximumOffset() > 0 && !viewport().contains({e.x, e.y})) {
                if (scrollbarThumb().contains({e.x, e.y})) { dragging_ = true; dragStartY_ = e.y; dragStartOffset_ = offset_; }
                else setOffset(offset_ + bounds_.height * .85f * (e.y < scrollbarThumb().y ? -1 : 1));
                e.handled = true; return;
            }
        }
        if (content_) {
            const bool inside = viewport().contains({e.x, e.y});
            if (e.type == Event::Type::MouseUp && contentCapture_ && !inside) {
                content_->cancelInteraction(); contentCapture_ = false; e.handled = true; return;
            }
            if (inside || contentCapture_) {
                content_->handleEvent(e);
                if (isPointerPress(e.type) && e.handled) { contentCapture_ = true; setFocused(false); }
                if (e.type == Event::Type::MouseUp) contentCapture_ = false;
            } else if (e.type == Event::Type::MouseMove || e.type == Event::Type::MouseDrag) {
                Event leave = e; leave.x = leave.y = -1.0e9f; content_->handleEvent(leave);
            }
        }
    }
protected:
    void paintControl(Canvas& c) override
    {
        arrange();
        ClippedCanvas clip(c, viewport()); if (content_) content_->paint(clip);
        if (maximumOffset() > 0) {
            const auto thumb = scrollbarThumb();
            c.drawRoundedRectangle({thumb.x, bounds_.y, thumb.width, bounds_.height}, style().radius, style().fill);
            c.drawRoundedRectangle(thumb, style().radius, dragging_ ? style().accent : style().mutedText);
        }
        focusOutline(c, style().radius);
    }
private:
    float contentHeight() const { return content_ ? std::max(0.0f, content_->preferredHeight()) : 0; }
    void arrange()
    {
        offset_ = std::clamp(offset_, 0.0f, maximumOffset());
        const auto view = viewport(); if (content_) content_->setBounds({view.x, view.y - offset_, view.width, contentHeight()});
    }
    bool hasFocus() const
    {
        if (isFocused()) return true;
        std::vector<Control*> targets; if (content_) content_->collectFocusTargets(targets);
        for (const auto* target : targets) if (target->isFocused()) return true;
        return false;
    }
    std::unique_ptr<Control> content_;
    float offset_ = 0, dragStartY_ = 0, dragStartOffset_ = 0;
    bool dragging_ = false, contentCapture_ = false;
};

class TabView : public Control {
public:
    TabView() { setAcceptsFocus(true); }
    Control& addTab(std::string label, std::unique_ptr<Control> page)
    {
        page->inheritFrom(this); pages_.push_back({std::move(label), std::move(page)}); arrange(); return *pages_.back().content;
    }
    size_t tabCount() const { return pages_.size(); }
    size_t activeIndex() const { return active_; }
    Control* activePage() const { return active_ < pages_.size() ? pages_[active_].content.get() : nullptr; }
    void setActiveIndex(size_t index)
    {
        if (index >= pages_.size() || active_ == index) return;
        if (activePage()) activePage()->cancelInteraction();
        active_ = index; arrange();
    }
    DFRect tabBounds(size_t index) const
    {
        const float width = pages_.empty() ? 0 : bounds_.width / static_cast<float>(pages_.size());
        return {bounds_.x + index * width, bounds_.y, width, std::min(bounds_.height, style().tabHeight)};
    }
    void setBounds(const DFRect& bounds) override { Control::setBounds(bounds); arrange(); }
    void cancelInteraction() override { Control::cancelInteraction(); for (auto& page : pages_) page.content->cancelInteraction(); }
    void collectFocusTargets(std::vector<Control*>& targets) override
    { Control::collectFocusTargets(targets); if (visible() && enabled() && activePage()) activePage()->collectFocusTargets(targets); }
    void handleEvent(Event& e) override
    {
        if (!visible() || !enabled()) { cancelInteraction(); return; }
        if (e.handled) return;
        arrange();
        if (isPointerPress(e.type)) {
            if (!bounds_.contains({e.x, e.y})) { cancelInteraction(); return; }
            if (e.y < bounds_.y + std::min(bounds_.height, style().tabHeight)) {
                for (size_t i = 0; i < pages_.size(); ++i) if (tabBounds(i).contains({e.x, e.y})) {
                    if (activePage()) activePage()->cancelInteraction(); setActiveIndex(i); setFocused(true); e.handled = true; return;
                }
            }
            setFocused(false);
        }
        if (e.type == Event::Type::KeyDown && e.key == 9) { advanceFocus(e.shift); e.handled = true; return; }
        if (e.type == Event::Type::KeyDown && !pages_.empty() && ((isFocused() && (e.key == 37 || e.key == 39 || e.key == 36 || e.key == 35)) || (e.ctrl && (e.key == 33 || e.key == 34)))) {
            if (e.key == 36) setActiveIndex(0);
            else if (e.key == 35) setActiveIndex(pages_.size() - 1);
            else setActiveIndex((active_ + pages_.size() + ((e.key == 37 || e.key == 33) ? -1 : 1)) % pages_.size());
            setFocused(true); e.handled = true; return;
        }
        if (activePage()) activePage()->handleEvent(e);
    }
protected:
    void paintControl(Canvas& c) override
    {
        arrange(); const auto s = style();
        for (size_t i = 0; i < pages_.size(); ++i) {
            auto r = tabBounds(i); r.width = std::max(0.0f, r.width - 2);
            c.drawRoundedRectangle(r, s.tabRadius, i == active_ ? s.selection : s.fill);
            text(c, pages_[i].label, {r.x + 10, r.y, std::max(0.0f, r.width - 20), r.height}, i == active_ ? s.text : s.mutedText);
            if (i == active_ && isFocused()) c.drawRoundedRectangleOutline(r, s.tabRadius, s.accent);
        }
        if (activePage()) activePage()->paint(c);
    }
private:
    void arrange()
    {
        const float header = std::min(std::max(0.0f, bounds_.height), style().tabHeight);
        if (activePage()) activePage()->setBounds({bounds_.x, bounds_.y + header + 6, bounds_.width, std::max(0.0f, bounds_.height - header - 6)});
    }
    void advanceFocus(bool reverse)
    {
        std::vector<Control*> targets; collectFocusTargets(targets); if (targets.empty()) return;
        size_t index = reverse ? 0 : targets.size() - 1;
        for (size_t i = 0; i < targets.size(); ++i) if (targets[i]->isFocused()) { index = i; break; }
        auto* target = targets[(index + targets.size() + (reverse ? -1 : 1)) % targets.size()];
        cancelInteraction();
        target->setFocused(true);
        for (auto* ancestor = target->parent(); ancestor; ancestor = ancestor->parent())
            if (auto* scroll = dynamic_cast<ScrollView*>(ancestor)) scroll->ensureVisible(target->bounds());
    }
    struct Page { std::string label; std::unique_ptr<Control> content; };
    std::vector<Page> pages_;
    size_t active_ = 0;
};

} // namespace df::ui
