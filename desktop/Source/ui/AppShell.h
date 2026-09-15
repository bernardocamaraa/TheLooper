// Moldura da janela principal: barra de navegacao (lateral no computador,
// barra de abas embaixo no celular/janela estreita), o HUD sempre visivel no
// topo (modo, tempo do loop, progresso, pedal, gravacao) e a pagina aberta.
#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppContext.h"
#include "Icons.h"
#include "ModeFrame.h"
#include "Page.h"

class AppShell : public juce::Component {
public:
    explicit AppShell(AppContext& context);
    ~AppShell() override;

    void addPage(const juce::String& id, const juce::String& label, ui::Icon icon, std::unique_ptr<Page> page);
    void showPage(const juce::String& id);
    void showPageIndex(int index);
    const juce::String& currentPage() const { return current_; }

    void refresh();        // 30 Hz
    void refreshColours(); // tema, tamanho ou cores das tracks mudaram
    bool compact() const { return compact_; }

    std::function<void(const juce::String&)> onPageChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    class NavRail;
    class StatusHud;

    struct Entry {
        juce::String id;
        juce::String label;
        ui::Icon icon;
        std::unique_ptr<Page> page;
    };

    AppContext& ctx_;
    std::vector<Entry> pages_;
    juce::String current_;
    std::unique_ptr<NavRail> rail_;
    std::unique_ptr<StatusHud> hud_;
    ui::FrameMood mood_ = ui::FrameMood::Stopped;
    bool compact_ = false;
    juce::Rectangle<int> pageArea_;
};
