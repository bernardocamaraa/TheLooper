#include "Icons.h"

#include <cmath>

namespace ui {

namespace {

void rays(juce::Path& p, float cx, float cy, float r1, float r2, int count) {
    for (int i = 0; i < count; ++i) {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(count);
        p.startNewSubPath(cx + r1 * std::cos(a), cy + r1 * std::sin(a));
        p.lineTo(cx + r2 * std::cos(a), cy + r2 * std::sin(a));
    }
}

void line(juce::Path& p, float x1, float y1, float x2, float y2) {
    p.startNewSubPath(x1, y1);
    p.lineTo(x2, y2);
}

void polyline(juce::Path& p, std::initializer_list<float> xy) {
    auto it = xy.begin();
    const float x = *it++;
    const float y = *it++;
    p.startNewSubPath(x, y);
    while (it != xy.end()) {
        const float nx = *it++;
        const float ny = *it++;
        p.lineTo(nx, ny);
    }
}

// Cada icone: o que e traco (stroke) e o que e cheio (fill), no quadro 24x24.
void build(Icon icon, juce::Path& stroke, juce::Path& fill) {
    switch (icon) {
        case Icon::Play:
            fill.addTriangle(8.0f, 5.5f, 8.0f, 18.5f, 18.5f, 12.0f);
            break;
        case Icon::Rec:
            fill.addEllipse(5.5f, 5.5f, 13.0f, 13.0f);
            break;
        case Icon::Stop:
            fill.addRoundedRectangle(6.5f, 6.5f, 11.0f, 11.0f, 2.0f);
            break;
        case Icon::Record:
            stroke.addEllipse(4.5f, 4.5f, 15.0f, 15.0f);
            fill.addEllipse(8.5f, 8.5f, 7.0f, 7.0f);
            break;
        case Icon::Undo:
            polyline(stroke, {9.0f, 5.0f, 4.5f, 9.5f, 9.0f, 14.0f});
            stroke.startNewSubPath(5.0f, 9.5f);
            stroke.lineTo(14.0f, 9.5f);
            stroke.cubicTo(17.3f, 9.5f, 20.0f, 12.0f, 20.0f, 15.0f);
            stroke.cubicTo(20.0f, 18.0f, 17.3f, 20.5f, 14.0f, 20.5f);
            stroke.lineTo(11.0f, 20.5f);
            break;
        case Icon::Mode:
            line(stroke, 4.0f, 8.0f, 17.0f, 8.0f);
            polyline(stroke, {13.5f, 4.5f, 17.0f, 8.0f, 13.5f, 11.5f});
            line(stroke, 20.0f, 16.0f, 7.0f, 16.0f);
            polyline(stroke, {10.5f, 12.5f, 7.0f, 16.0f, 10.5f, 19.5f});
            break;
        case Icon::Trash:
            line(stroke, 5.0f, 7.0f, 19.0f, 7.0f);
            polyline(stroke, {10.0f, 7.0f, 10.0f, 4.8f, 14.0f, 4.8f, 14.0f, 7.0f});
            polyline(stroke, {7.0f, 7.0f, 8.0f, 19.5f, 16.0f, 19.5f, 17.0f, 7.0f});
            break;
        case Icon::Plus:
            line(stroke, 12.0f, 5.0f, 12.0f, 19.0f);
            line(stroke, 5.0f, 12.0f, 19.0f, 12.0f);
            break;
        case Icon::Search:
            stroke.addEllipse(5.0f, 5.0f, 12.0f, 12.0f);
            line(stroke, 15.5f, 15.5f, 20.0f, 20.0f);
            break;
        case Icon::Mixer:
            line(stroke, 6.0f, 4.0f, 6.0f, 20.0f);
            line(stroke, 12.0f, 4.0f, 12.0f, 20.0f);
            line(stroke, 18.0f, 4.0f, 18.0f, 20.0f);
            fill.addRoundedRectangle(3.5f, 13.0f, 5.0f, 3.0f, 1.2f);
            fill.addRoundedRectangle(9.5f, 7.0f, 5.0f, 3.0f, 1.2f);
            fill.addRoundedRectangle(15.5f, 11.0f, 5.0f, 3.0f, 1.2f);
            break;
        case Icon::Sessions:
            stroke.addRoundedRectangle(4.0f, 4.0f, 7.0f, 7.0f, 2.0f);
            stroke.addRoundedRectangle(13.0f, 4.0f, 7.0f, 7.0f, 2.0f);
            stroke.addRoundedRectangle(4.0f, 13.0f, 7.0f, 7.0f, 2.0f);
            stroke.addRoundedRectangle(13.0f, 13.0f, 7.0f, 7.0f, 2.0f);
            break;
        case Icon::Setlist:
            line(stroke, 9.0f, 6.0f, 20.0f, 6.0f);
            line(stroke, 9.0f, 12.0f, 20.0f, 12.0f);
            line(stroke, 9.0f, 18.0f, 20.0f, 18.0f);
            fill.addEllipse(3.1f, 4.6f, 2.8f, 2.8f);
            fill.addEllipse(3.1f, 10.6f, 2.8f, 2.8f);
            fill.addEllipse(3.1f, 16.6f, 2.8f, 2.8f);
            break;
        case Icon::Screens:
            stroke.addRoundedRectangle(2.5f, 5.0f, 12.0f, 9.0f, 1.6f);
            stroke.addRoundedRectangle(16.0f, 8.0f, 5.5f, 7.0f, 1.3f);
            line(stroke, 6.0f, 18.0f, 11.0f, 18.0f);
            break;
        case Icon::Settings:
            stroke.addEllipse(9.0f, 9.0f, 6.0f, 6.0f);
            rays(stroke, 12.0f, 12.0f, 6.2f, 8.8f, 8);
            break;
        case Icon::Pedal:
            stroke.addRoundedRectangle(3.0f, 5.5f, 18.0f, 13.0f, 2.5f);
            for (int i = 0; i < 4; ++i) {
                fill.addEllipse(5.3f + 3.8f * static_cast<float>(i), 12.5f, 2.6f, 2.6f);
            }
            line(stroke, 5.5f, 9.0f, 18.5f, 9.0f);
            break;
        case Icon::Cards:
            stroke.addRoundedRectangle(4.0f, 5.0f, 7.0f, 14.0f, 2.0f);
            stroke.addRoundedRectangle(13.0f, 5.0f, 7.0f, 14.0f, 2.0f);
            break;
        case Icon::Export:
            line(stroke, 12.0f, 15.0f, 12.0f, 4.0f);
            polyline(stroke, {8.0f, 8.0f, 12.0f, 4.0f, 16.0f, 8.0f});
            polyline(stroke, {5.0f, 13.0f, 5.0f, 19.5f, 19.0f, 19.5f, 19.0f, 13.0f});
            break;
        case Icon::Folder:
            polyline(stroke, {3.5f, 6.5f, 9.5f, 6.5f, 11.5f, 8.5f, 20.5f, 8.5f, 20.5f, 18.5f, 3.5f, 18.5f});
            stroke.closeSubPath();
            break;
        case Icon::Next:
            fill.addTriangle(6.0f, 5.0f, 6.0f, 19.0f, 16.0f, 12.0f);
            fill.addRoundedRectangle(16.5f, 5.0f, 2.2f, 14.0f, 1.0f);
            break;
        case Icon::Prev:
            fill.addTriangle(18.0f, 5.0f, 18.0f, 19.0f, 8.0f, 12.0f);
            fill.addRoundedRectangle(5.3f, 5.0f, 2.2f, 14.0f, 1.0f);
            break;
        case Icon::Sun:
            stroke.addEllipse(8.0f, 8.0f, 8.0f, 8.0f);
            rays(stroke, 12.0f, 12.0f, 6.5f, 9.0f, 8);
            break;
        case Icon::Moon:
            fill.startNewSubPath(15.0f, 4.0f);
            fill.cubicTo(9.5f, 4.2f, 6.0f, 8.3f, 6.0f, 12.5f);
            fill.cubicTo(6.0f, 16.9f, 9.6f, 20.5f, 14.0f, 20.5f);
            fill.cubicTo(16.6f, 20.5f, 18.9f, 19.2f, 20.3f, 17.2f);
            fill.cubicTo(15.5f, 17.5f, 11.7f, 14.0f, 11.7f, 9.4f);
            fill.cubicTo(11.7f, 7.3f, 12.9f, 5.3f, 15.0f, 4.0f);
            fill.closeSubPath();
            break;
        case Icon::Check:
            polyline(stroke, {5.0f, 12.5f, 10.0f, 17.5f, 19.5f, 7.0f});
            break;
        case Icon::Close:
            line(stroke, 6.0f, 6.0f, 18.0f, 18.0f);
            line(stroke, 18.0f, 6.0f, 6.0f, 18.0f);
            break;
        case Icon::Edit:
            polyline(stroke, {4.5f, 19.5f, 8.5f, 19.5f, 19.0f, 9.0f, 15.0f, 5.0f, 4.5f, 15.5f});
            stroke.closeSubPath();
            break;
        case Icon::Up:
            polyline(stroke, {6.0f, 15.0f, 12.0f, 9.0f, 18.0f, 15.0f});
            break;
        case Icon::Down:
            polyline(stroke, {6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 9.0f});
            break;
        case Icon::Copy:
            stroke.addRoundedRectangle(8.5f, 8.5f, 11.0f, 11.0f, 2.0f);
            polyline(stroke, {15.5f, 8.5f, 15.5f, 4.5f, 4.5f, 4.5f, 4.5f, 15.5f, 8.5f, 15.5f});
            break;
        case Icon::Save:
            stroke.addRoundedRectangle(4.0f, 4.0f, 16.0f, 16.0f, 2.0f);
            polyline(stroke, {8.0f, 4.0f, 8.0f, 9.0f, 16.0f, 9.0f, 16.0f, 4.0f});
            stroke.addRoundedRectangle(8.0f, 13.0f, 8.0f, 7.0f, 1.0f);
            break;
    }
}

} // namespace

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour) {
    juce::Path stroke;
    juce::Path fill;
    build(icon, stroke, fill);

    const float s = juce::jmin(area.getWidth(), area.getHeight()) / 24.0f;
    const auto transform = juce::AffineTransform::scale(s).translated(area.getCentreX() - 12.0f * s,
                                                                       area.getCentreY() - 12.0f * s);
    g.setColour(colour);
    if (!fill.isEmpty()) {
        fill.applyTransform(transform);
        g.fillPath(fill);
    }
    if (!stroke.isEmpty()) {
        stroke.applyTransform(transform);
        g.strokePath(stroke, juce::PathStrokeType(juce::jmax(1.0f, 1.8f * s), juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

bool iconFromName(const juce::String& name, Icon& out) {
    static const std::pair<const char*, Icon> table[] = {
        {"play", Icon::Play},       {"rec", Icon::Rec},         {"stop", Icon::Stop},
        {"undo", Icon::Undo},       {"mode", Icon::Mode},       {"trash", Icon::Trash},
        {"plus", Icon::Plus},       {"search", Icon::Search},   {"mixer", Icon::Mixer},
        {"sessions", Icon::Sessions}, {"setlist", Icon::Setlist}, {"screens", Icon::Screens},
        {"settings", Icon::Settings}, {"pedal", Icon::Pedal},   {"cards", Icon::Cards},
        {"export", Icon::Export},   {"folder", Icon::Folder},   {"next", Icon::Next},
        {"prev", Icon::Prev},       {"sun", Icon::Sun},         {"moon", Icon::Moon},
        {"check", Icon::Check},     {"close", Icon::Close},     {"edit", Icon::Edit},
        {"up", Icon::Up},           {"down", Icon::Down},       {"copy", Icon::Copy},
        {"save", Icon::Save},       {"record", Icon::Record},
    };
    if (name.isEmpty()) {
        return false;
    }
    for (const auto& [key, icon] : table) {
        if (name == key) {
            out = icon;
            return true;
        }
    }
    return false;
}

void setButtonIcon(juce::Button& button, const juce::String& name) {
    button.getProperties().set("icon", name);
    button.repaint();
}

} // namespace ui
