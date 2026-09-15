#include "ui/Pages.h"

#include "Settings.h"
#include "UiText.h"

namespace {
juce::File setlistsFolder(AppContext& ctx) {
    auto folder = ctx.loopsFolder().getParentDirectory().getChildFile("Setlists");
    folder.createDirectory();
    return folder;
}
} // namespace

// Lista de musicas, desenhada inteira aqui e rolada pelo Viewport.
class SetlistPage::SongList : public juce::Component {
public:
    explicit SongList(SetlistPage& owner) : owner_(owner) {}

    int rowHeight() const { return theme::px(56); }

    void update(int width) {
        const int rows = owner_.ctx_.setlist().size();
        setSize(width, juce::jmax(theme::px(80), rows * rowHeight()));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        const SetlistModel& model = owner_.ctx_.setlist();
        if (model.empty()) {
            g.setColour(p.t3);
            g.setFont(theme::text(theme::pxf(15.0f)));
            g.drawFittedText(ui::utf8("Nenhuma música ainda. Toque em “+ Música”."), getLocalBounds().reduced(theme::px(14)),
                             juce::Justification::centredTop, 2, 0.9f);
            return;
        }
        for (int i = 0; i < model.size(); ++i) {
            const auto& song = *model.song(i);
            auto row = juce::Rectangle<int>(0, i * rowHeight(), getWidth(), rowHeight()).toFloat();
            const bool current = i == model.current;
            const bool selected = i == owner_.selectedRow_;
            if (current || selected) {
                g.setColour(current ? p.play.withAlpha(0.14f) : p.s3);
                g.fillRoundedRectangle(row.reduced(2.0f), theme::pxf(10.0f));
            } else if (i > 0) {
                g.setColour(p.line);
                g.fillRect(row.getX() + theme::pxf(10.0f), row.getY(), row.getWidth() - theme::pxf(20.0f), 1.0f);
            }
            auto inner = row.reduced(theme::pxf(14.0f), theme::pxf(6.0f));
            g.setColour(current ? p.play : p.t3);
            g.setFont(theme::numbers(theme::pxf(14.0f), current));
            g.drawText(juce::String(i + 1), inner.removeFromLeft(theme::pxf(30.0f)), juce::Justification::centredLeft,
                       false);
            auto top = inner.removeFromTop(inner.getHeight() * 0.56f);
            g.setColour(i < model.current ? p.t3 : p.t1);
            g.setFont(theme::text(theme::pxf(15.5f), theme::Weight::Semibold));
            g.drawFittedText(song.name, top.toNearestInt(), juce::Justification::bottomLeft, 1, 0.85f);
            juce::StringArray names;
            for (const auto& n : song.tracks) {
                if (n.isNotEmpty()) {
                    names.add(n);
                }
            }
            g.setColour(p.t3);
            g.setFont(theme::text(theme::pxf(12.0f)));
            g.drawFittedText(names.joinIntoString(ui::utf8("  ·  ")), inner.toNearestInt(), juce::Justification::topLeft,
                             1, 0.85f);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const int row = e.getPosition().getY() / juce::jmax(1, rowHeight());
        if (row >= 0 && row < owner_.ctx_.setlist().size()) {
            owner_.selectedRow_ = row;
            repaint();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = e.getPosition().getY() / juce::jmax(1, rowHeight());
        if (row >= 0 && row < owner_.ctx_.setlist().size()) {
            owner_.editSong(row, false);
        }
    }

private:
    SetlistPage& owner_;
};

SetlistPage::SetlistPage(AppContext& context) : Page(context) {
    list_ = std::make_unique<SongList>(*this);
    viewport_.setViewedComponent(list_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    auto setup = [this](juce::TextButton& b, const juce::String& text, const juce::String& icon, const juce::String& style) {
        b.setButtonText(text);
        ui::setButtonIcon(b, icon);
        ui::setButtonStyle(b, style);
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };
    setup(new_, "Novo", "plus", {});
    setup(open_, ui::utf8("Abrir…"), "folder", {});
    setup(save_, "Renomear", "edit", {});
    setup(add_, ui::utf8("Música"), "plus", {});
    setup(edit_, "Editar", "edit", {});
    setup(remove_, "Remover", "trash", "danger");
    setup(up_, {}, "up", {});
    setup(down_, {}, "down", {});
    setup(next_, ui::utf8("Próxima"), "next", "primary");
    setup(prev_, "Anterior", "prev", {});
    setup(apply_, "Usar a selecionada", "check", {});

    new_.onClick = [this] {
        auto start = [this] {
            SetlistModel& model = ctx_.setlist();
            model.data = Setlist{};
            model.data.title = "Setlist novo";
            model.file = juce::File();
            model.current = 0;
            selectedRow_ = 0;
            persist();
            pageShown();
        };
        if (ctx_.setlist().empty()) {
            start();
        } else {
            ui::confirm(ui::utf8("Começar um setlist novo?"), ui::utf8("O setlist atual continua salvo em disco."),
                        "Novo setlist", start, false);
        }
    };
    open_.onClick = [this] { showOpenMenu(); };
    save_.onClick = [this] {
        ui::askText("Renomear setlist", "Nome do setlist:", ctx_.setlist().data.title, "Renomear",
                    [this](juce::String name) {
                        if (name.isEmpty()) {
                            return;
                        }
                        SetlistModel& model = ctx_.setlist();
                        const juce::File old = model.file;
                        model.data.title = name;
                        model.file = setlistsFolder(ctx_).getNonexistentChildFile(juce::File::createLegalFileName(name),
                                                                                  setlistfile::kExtension, true);
                        persist();
                        if (old.existsAsFile() && old != model.file) {
                            old.deleteFile();
                        }
                        repaint();
                    });
    };
    add_.onClick = [this] { editSong(ctx_.setlist().size(), true); };
    edit_.onClick = [this] { editSong(selectedRow_, false); };
    remove_.onClick = [this] {
        SetlistModel& model = ctx_.setlist();
        const SetlistSong* song = model.song(selectedRow_);
        if (song == nullptr) {
            return;
        }
        ui::confirm(ui::utf8("Remover a música?"), ui::utf8("“") + song->name + ui::utf8("” sai do setlist."), "Remover",
                    [this] {
                        SetlistModel& m = ctx_.setlist();
                        if (selectedRow_ < 0 || selectedRow_ >= m.size()) {
                            return;
                        }
                        m.data.songs.erase(m.data.songs.begin() + selectedRow_);
                        if (m.current > selectedRow_ || m.current >= m.size()) {
                            m.current = juce::jmax(0, m.current - 1);
                        }
                        selectedRow_ = juce::jlimit(0, juce::jmax(0, m.size() - 1), selectedRow_);
                        persist();
                        pageShown();
                    });
    };
    auto move = [this](int delta) {
        SetlistModel& model = ctx_.setlist();
        const int from = selectedRow_;
        const int to = from + delta;
        if (from < 0 || to < 0 || from >= model.size() || to >= model.size()) {
            return;
        }
        std::swap(model.data.songs[static_cast<size_t>(from)], model.data.songs[static_cast<size_t>(to)]);
        if (model.current == from) {
            model.current = to;
        } else if (model.current == to) {
            model.current = from;
        }
        selectedRow_ = to;
        persist();
        pageShown();
    };
    up_.onClick = [move] { move(-1); };
    down_.onClick = [move] { move(1); };
    next_.onClick = [this] { ctx_.setlistStep(1); };
    prev_.onClick = [this] { ctx_.setlistStep(-1); };
    apply_.onClick = [this] { ctx_.setlistGoTo(selectedRow_); };
}

SetlistPage::~SetlistPage() {
    viewport_.setViewedComponent(nullptr, false);
}

void SetlistPage::persist() {
    SetlistModel& model = ctx_.setlist();
    // Sempre salvo: um setlist montado na passagem de som nao pode sumir se o
    // app fechar.
    if (model.file == juce::File()) {
        model.file = setlistsFolder(ctx_).getNonexistentChildFile(
            juce::File::createLegalFileName(model.data.title.isNotEmpty() ? model.data.title : "Setlist"),
            setlistfile::kExtension, true);
    }
    const juce::String error = setlistfile::save(model.file, model.data);
    if (error.isNotEmpty()) {
        ui::notify(ui::utf8("Não deu para salvar o setlist"), error);
        return;
    }
    ctx_.settings().setSetlistFile(model.file);
    ctx_.settings().setSetlistIndex(model.current);
}

void SetlistPage::editSong(int index, bool isNew) {
    SetlistModel& model = ctx_.setlist();
    SetlistSong song;
    if (isNew) {
        song.name = ui::utf8("Música ") + juce::String(model.size() + 1);
        for (int t = 0; t < config::kNumTracks; ++t) {
            song.tracks[static_cast<size_t>(t)] = ctx_.trackName(t);
        }
    } else if (const SetlistSong* existing = model.song(index)) {
        song = *existing;
    } else {
        return;
    }

    auto window = std::make_shared<juce::AlertWindow>(isNew ? ui::utf8("Nova música") : ui::utf8("Editar música"),
                                                      ui::utf8("O loop dela é gravado ao vivo. Aqui ficam só os nomes."),
                                                      juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", song.name, ui::utf8("Nome da música"));
    for (int t = 0; t < config::kNumTracks; ++t) {
        window->addTextEditor("t" + juce::String(t), song.tracks[static_cast<size_t>(t)], "Track " + juce::String(t + 1));
    }
    window->addButton(ui::utf8("Cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->addButton("Salvar", 1, juce::KeyPress(juce::KeyPress::returnKey));
    if (auto* button = window->getButton("Salvar")) {
        ui::setButtonStyle(*button, "primary");
    }
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window, index, isNew](int result) {
                                window->setVisible(false);
                                if (result != 1) {
                                    return;
                                }
                                SetlistSong edited;
                                edited.name = window->getTextEditorContents("name").trim();
                                if (edited.name.isEmpty()) {
                                    edited.name = ui::utf8("Sem nome");
                                }
                                for (int t = 0; t < config::kNumTracks; ++t) {
                                    edited.tracks[static_cast<size_t>(t)] =
                                        window->getTextEditorContents("t" + juce::String(t)).trim()
                                            .substring(0, config::kMaxTrackNameLength);
                                }
                                SetlistModel& m = ctx_.setlist();
                                if (isNew) {
                                    m.data.songs.push_back(edited);
                                    selectedRow_ = m.size() - 1;
                                } else if (index >= 0 && index < m.size()) {
                                    m.data.songs[static_cast<size_t>(index)] = edited;
                                }
                                persist();
                                pageShown();
                            }),
                            false);
}

void SetlistPage::showOpenMenu() {
    juce::PopupMenu menu;
    const auto files = setlistsFolder(ctx_).findChildFiles(juce::File::findFiles, false, setlistfile::kWildcard);
    int id = 1;
    for (const auto& file : files) {
        menu.addItem(id++, file.getFileNameWithoutExtension(), true, file == ctx_.setlist().file);
    }
    if (files.isEmpty()) {
        menu.addItem(-1, "Nenhum setlist salvo", false);
    }
    menu.addSeparator();
    menu.addItem(1000, ui::utf8("Outro arquivo…"));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&open_), [this, files](int result) {
        auto load = [this](const juce::File& file) {
            Setlist loaded;
            const juce::String error = setlistfile::load(file, loaded);
            if (error.isNotEmpty()) {
                ui::notify(ui::utf8("Não deu para abrir"), error);
                return;
            }
            SetlistModel& model = ctx_.setlist();
            model.data = loaded;
            model.file = file;
            model.current = 0;
            selectedRow_ = 0;
            ctx_.settings().setSetlistFile(file);
            ctx_.settings().setSetlistIndex(0);
            pageShown();
        };
        if (result >= 1 && result <= files.size()) {
            load(files[result - 1]);
        } else if (result == 1000) {
            auto chooser = std::make_shared<juce::FileChooser>("Abrir setlist", setlistsFolder(ctx_),
                                                               setlistfile::kWildcard);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [load, chooser](const juce::FileChooser& fc) {
                                     if (fc.getResult().existsAsFile()) {
                                         load(fc.getResult());
                                     }
                                 });
        }
    });
}

void SetlistPage::pageShown() {
    list_->update(juce::jmax(100, viewport_.getMaximumVisibleWidth()));
    shownCurrent_ = -1;
    updateNowCard();
    repaint();
}

void SetlistPage::refresh() {
    const bool armed = ctx_.setlistArmed();
    if (armed != armed_ || ctx_.setlist().current != shownCurrent_) {
        armed_ = armed;
        updateNowCard();
    }
}

void SetlistPage::updateNowCard() {
    const SetlistModel& model = ctx_.setlist();
    shownCurrent_ = model.current;
    const SetlistSong* nextSong = model.song(model.current + 1);
    next_.getProperties().set("subtitle", nextSong != nullptr ? nextSong->name : juce::String("Fim do setlist"));
    next_.setEnabled(nextSong != nullptr);
    prev_.setEnabled(model.current > 0);
    const bool has = !model.empty();
    for (auto* b : {&edit_, &remove_, &up_, &down_, &apply_}) {
        b->setEnabled(has);
    }
    next_.repaint();
    list_->repaint();
    repaint(nowCard_);
}

void SetlistPage::refreshColours() {
    list_->repaint();
    repaint();
}

void SetlistPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    {
        auto buttons = header_.removeFromRight(theme::px(370)).withSizeKeepingCentre(theme::px(370), theme::px(40));
        const int w = (buttons.getWidth() - theme::px(16)) / 3;
        for (auto* b : {&new_, &open_, &save_}) {
            b->setBounds(buttons.removeFromLeft(w));
            buttons.removeFromLeft(theme::px(8));
        }
    }

    const bool compact = getWidth() < theme::px(760);
    const int gap = theme::px(18);
    juce::Rectangle<int> listCard;
    if (compact) {
        nowCard_ = area.removeFromBottom(theme::px(330));
        area.removeFromBottom(gap);
        listCard = area;
    } else {
        nowCard_ = area.removeFromRight(area.getWidth() * 45 / 100);
        area.removeFromRight(gap);
        listCard = area;
    }

    // Lista + botoes de edicao embaixo.
    auto tools = listCard.removeFromBottom(theme::px(40));
    listCard.removeFromBottom(theme::px(10));
    const int small = theme::px(46);
    down_.setBounds(tools.removeFromRight(small));
    tools.removeFromRight(theme::px(6));
    up_.setBounds(tools.removeFromRight(small));
    tools.removeFromRight(theme::px(12));
    const int w = (tools.getWidth() - theme::px(16)) / 3;
    for (auto* b : {&add_, &edit_, &remove_}) {
        b->setBounds(tools.removeFromLeft(w));
        tools.removeFromLeft(theme::px(8));
    }
    viewport_.setBounds(listCard.reduced(theme::px(8)));
    list_->update(juce::jmax(100, viewport_.getMaximumVisibleWidth()));

    // Cartao da musica atual.
    auto card = nowCard_.reduced(theme::px(22));
    auto row = card.removeFromBottom(theme::px(42));
    prev_.setBounds(row.removeFromLeft((row.getWidth() - theme::px(10)) / 2));
    row.removeFromLeft(theme::px(10));
    apply_.setBounds(row);
    card.removeFromBottom(theme::px(12));
    next_.setBounds(card.removeFromBottom(theme::px(92)));
    card.removeFromBottom(theme::px(14));
    nowText_ = card;
}

void SetlistPage::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    const SetlistModel& model = ctx_.setlist();
    const juce::String subtitle =
        model.empty() && model.data.title.isEmpty()
            ? ui::utf8("Monte a ordem das músicas do show. O loop de cada uma é gravado ao vivo.")
            : model.data.title + ui::utf8("  ·  ") + juce::String(model.size()) +
                  (model.size() == 1 ? ui::utf8(" música") : ui::utf8(" músicas"));
    ui::paintPageTitle(g, header_, "Setlist", subtitle);

    const auto listCard = viewport_.getBounds().expanded(theme::px(8));
    theme::paintCard(g, listCard.toFloat().reduced(1.0f), theme::pxf(16.0f));
    theme::paintCard(g, nowCard_.toFloat().reduced(1.0f), theme::pxf(16.0f));

    auto r = nowText_.toFloat();
    g.setColour(p.t3);
    g.setFont(theme::caps(theme::pxf(11.0f)));
    g.drawText(ui::utf8("MÚSICA ATUAL"), r.removeFromTop(theme::pxf(20.0f)), juce::Justification::centredLeft, false);

    const SetlistSong* song = model.currentSong();
    g.setColour(p.t1);
    g.setFont(theme::text(theme::pxf(30.0f), theme::Weight::Bold));
    g.drawFittedText(song != nullptr ? song->name : ui::utf8("Nenhuma música"),
                     r.removeFromTop(theme::pxf(44.0f)).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    r.removeFromTop(theme::pxf(8.0f));

    if (song != nullptr) {
        for (int t = 0; t < config::kNumTracks && r.getHeight() > theme::pxf(60.0f); ++t) {
            auto line = r.removeFromTop(theme::pxf(24.0f));
            const float d = theme::pxf(10.0f);
            g.setColour(theme::track(t));
            g.fillEllipse(line.getX(), line.getCentreY() - d * 0.5f, d, d);
            g.setColour(p.t1);
            g.setFont(theme::text(theme::pxf(14.0f)));
            const juce::String name = song->tracks[static_cast<size_t>(t)];
            g.drawText(juce::String(t + 1) + ui::utf8("  ·  ") + (name.isNotEmpty() ? name : ui::utf8("—")),
                       line.withTrimmedLeft(d * 1.8f), juce::Justification::centredLeft, false);
        }
        r.removeFromTop(theme::pxf(10.0f));
    }

    g.setFont(theme::text(theme::pxf(13.0f)));
    if (armed_) {
        g.setColour(p.amber);
        g.drawFittedText(ui::utf8("Há um loop gravado. Toque de novo para limpar e trocar de música, ou continue "
                                  "tocando."),
                         r.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    } else {
        g.setColour(p.t3);
        g.drawFittedText(ui::utf8("O loop começa vazio e é gravado ao vivo, do tamanho que você tocar. Trocar de "
                                  "música limpa o loop e troca o nome das tracks. Atalhos: N próxima, P anterior."),
                         r.toNearestInt(), juce::Justification::topLeft, 4, 0.9f);
    }
}
