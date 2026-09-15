// Base das abas da janela principal.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppContext.h"
#include "PedalLookAndFeel.h"
#include "Widgets.h"

class Page : public juce::Component {
public:
    explicit Page(AppContext& context) : ctx_(context) {}

    // 30 Hz, so na aba aberta.
    virtual void refresh() {}
    // Quando a aba e aberta (hora de reler valores que mudaram por fora).
    virtual void pageShown() {}
    // Tema ou cores das tracks mudaram.
    virtual void refreshColours() { repaint(); }

protected:
    // Faixa do titulo no topo da pagina; o resto e da pagina.
    juce::Rectangle<int> takeHeader(juce::Rectangle<int>& area) const {
        auto header = area.removeFromTop(theme::px(64));
        area.removeFromTop(theme::px(10));
        return header;
    }

    AppContext& ctx_;
};
