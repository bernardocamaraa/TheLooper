#include "ui/Pages.h"

#include <algorithm>
#include <cmath>

#include "UiText.h"

namespace {
juce::String dateText(const juce::Time& time) {
    return time.formatted("%d/%m/%Y  %H:%M");
}
} // namespace

// Grade de sessoes: desenhada inteira aqui (sem um componente por cartao), e
// rolada pelo Viewport.
class SessionsPage::Grid : public juce::Component {
public:
    explicit Grid(SessionsPage& owner) : owner_(owner) {}

    void layoutFor(int width) {
        gap_ = theme::px(12);
        columns_ = juce::jmax(1, (width + gap_) / (theme::px(210) + gap_));
        tileW_ = (width - gap_ * (columns_ - 1)) / columns_;
        tileH_ = theme::px(112);
        const int count = static_cast<int>(owner_.visible_.size());
        const int rows = (count + columns_ - 1) / columns_;
        setSize(width, juce::jmax(theme::px(120), rows * (tileH_ + gap_)));
        repaint();
    }

    juce::Rectangle<int> tile(int index) const {
        return {(index % columns_) * (tileW_ + gap_), (index / columns_) * (tileH_ + gap_), tileW_, tileH_};
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        if (owner_.visible_.empty()) {
            g.setColour(p.t3);
            g.setFont(theme::text(theme::pxf(15.0f)));
            g.drawFittedText(owner_.entries_.empty()
                                 ? ui::utf8("Nenhuma sessão ainda. Grave um loop e toque em “Salvar sessão atual”.")
                                 : ui::utf8("Nenhuma sessão com esse nome."),
                             getLocalBounds().reduced(theme::px(10)), juce::Justification::centredTop, 2, 0.9f);
            return;
        }
        for (size_t i = 0; i < owner_.visible_.size(); ++i) {
            const auto& entry = owner_.entries_[static_cast<size_t>(owner_.visible_[i])];
            const auto r = tile(static_cast<int>(i)).toFloat().reduced(2.0f);
            const float radius = theme::pxf(14.0f);
            theme::paintCard(g, r, radius);
            if (static_cast<int>(i) == owner_.selected_) {
                theme::paintSelection(g, r, radius, p.t1);
            }
            auto inner = r.reduced(theme::pxf(14.0f));
            g.setColour(p.t1);
            g.setFont(theme::text(theme::pxf(16.0f), theme::Weight::Semibold));
            g.drawFittedText(entry.file.getFileNameWithoutExtension(), inner.removeFromTop(theme::pxf(24.0f)).toNearestInt(),
                             juce::Justification::centredLeft, 1, 0.85f);
            inner.removeFromTop(theme::pxf(4.0f));
            g.setColour(entry.valid ? p.t3 : p.amber);
            g.setFont(theme::numbers(theme::pxf(12.5f)));
            const juce::String meta = entry.valid ? juce::String(entry.info.seconds(), 1) + " s    " + dateText(entry.modified)
                                                  : ui::utf8("arquivo ilegível");
            g.drawText(meta, inner.removeFromTop(theme::pxf(20.0f)), juce::Justification::centredLeft, false);

            auto dots = inner.removeFromBottom(theme::pxf(12.0f));
            const float d = theme::pxf(10.0f);
            for (int t = 0; t < config::kNumTracks; ++t) {
                g.setColour(entry.info.hasAudio[static_cast<size_t>(t)] ? theme::track(t) : p.s3);
                g.fillEllipse(dots.getX() + static_cast<float>(t) * d * 1.7f, dots.getCentreY() - d * 0.5f, d, d);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < owner_.visible_.size(); ++i) {
            if (tile(static_cast<int>(i)).contains(e.getPosition())) {
                owner_.select(static_cast<int>(i));
                return;
            }
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < owner_.visible_.size(); ++i) {
            if (tile(static_cast<int>(i)).contains(e.getPosition())) {
                owner_.select(static_cast<int>(i));
                owner_.openSelected();
                return;
            }
        }
    }

private:
    SessionsPage& owner_;
    int columns_ = 3;
    int tileW_ = 200;
    int tileH_ = 110;
    int gap_ = 12;
};

SessionsPage::SessionsPage(AppContext& context) : Page(context) {
    grid_ = std::make_unique<Grid>(*this);
    viewport_.setViewedComponent(grid_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    search_.setTextToShowWhenEmpty(ui::utf8("Buscar sessão…"), theme::palette().t3);
    search_.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(search_);

    auto setup = [this](juce::TextButton& b, const juce::String& text, const juce::String& icon, const juce::String& style) {
        b.setButtonText(text);
        ui::setButtonIcon(b, icon);
        ui::setButtonStyle(b, style);
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };
    setup(save_, ui::utf8("Salvar sessão atual"), "plus", "primary");
    save_.onClick = [this] { saveCurrent(); };
    setup(open_, "Abrir", "play", "primary");
    open_.onClick = [this] { openSelected(); };
    setup(export_, "Exportar stems (WAV)", "export", {});
    export_.onClick = [this] { exportSelected(); };
    setup(rename_, "Renomear", "edit", {});
    rename_.onClick = [this] { renameSelected(); };
    setup(duplicate_, "Duplicar", "copy", {});
    duplicate_.onClick = [this] { duplicateSelected(); };
    setup(trash_, "Mover para a lixeira", "trash", "danger");
    trash_.onClick = [this] { trashSelected(); };
    setup(reveal_, "Mostrar na pasta", "folder", {});
    reveal_.onClick = [this] {
        if (const Entry* entry = selectedEntry()) {
            entry->file.revealToUser();
        } else {
            ctx_.loopsFolder().revealToUser();
        }
    };
    updateButtons();
}

SessionsPage::~SessionsPage() {
    viewport_.setViewedComponent(nullptr, false);
}

void SessionsPage::pageShown() {
    rescan();
}

void SessionsPage::refreshColours() {
    search_.setTextToShowWhenEmpty(ui::utf8("Buscar sessão…"), theme::palette().t3);
    grid_->repaint();
    repaint();
}

void SessionsPage::rescan() {
    const juce::File keep = selectedEntry() != nullptr ? selectedEntry()->file : juce::File();
    entries_.clear();
    for (const auto& file : ctx_.loopsFolder().findChildFiles(juce::File::findFiles, false, loopfile::kWildcard)) {
        Entry entry;
        entry.file = file;
        entry.modified = file.getLastModificationTime();
        entry.valid = loopfile::readInfo(file, entry.info).isEmpty();
        entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });

    selected_ = -1;
    applyFilter();
    for (size_t i = 0; i < visible_.size(); ++i) {
        if (entries_[static_cast<size_t>(visible_[i])].file == keep) {
            select(static_cast<int>(i));
        }
    }
    repaint();
}

void SessionsPage::applyFilter() {
    const juce::String query = search_.getText().trim();
    const juce::File keep = selectedEntry() != nullptr ? selectedEntry()->file : juce::File();
    visible_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (query.isEmpty() || entries_[i].file.getFileNameWithoutExtension().containsIgnoreCase(query)) {
            visible_.push_back(static_cast<int>(i));
        }
    }
    selected_ = visible_.empty() ? -1 : 0;
    for (size_t i = 0; i < visible_.size(); ++i) {
        if (entries_[static_cast<size_t>(visible_[i])].file == keep) {
            selected_ = static_cast<int>(i);
        }
    }
    grid_->layoutFor(juce::jmax(100, viewport_.getMaximumVisibleWidth()));
    updateButtons();
    repaint(detail_);
}

void SessionsPage::select(int visibleIndex) {
    selected_ = visibleIndex;
    grid_->repaint();
    updateButtons();
    repaint(detail_);
}

const SessionsPage::Entry* SessionsPage::selectedEntry() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<size_t>(visible_[static_cast<size_t>(selected_)])];
}

void SessionsPage::updateButtons() {
    const Entry* entry = selectedEntry();
    const bool has = entry != nullptr;
    for (auto* b : {&open_, &export_, &rename_, &duplicate_, &trash_}) {
        b->setEnabled(has && (b == &trash_ || b == &rename_ || entry->valid));
    }
}

void SessionsPage::saveCurrent() {
    if (!ctx_.engine().loopDefined()) {
        ui::notify(ui::utf8("Nada para salvar"), ui::utf8("Grave um loop antes de salvar a sessão."));
        return;
    }
    const juce::String suggested = "Loop " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M");
    ui::askText(ui::utf8("Salvar sessão"), ui::utf8("Nome da sessão:"), suggested, "Salvar", [this](juce::String name) {
        if (name.isEmpty()) {
            return;
        }
        const juce::File file = ctx_.loopsFolder().getChildFile(juce::File::createLegalFileName(name) + loopfile::kExtension);
        auto doSave = [this, file] {
            const juce::String error = ctx_.saveSession(file);
            if (error.isNotEmpty()) {
                ui::notify(ui::utf8("Não deu para salvar"), error);
                return;
            }
            search_.clear();
            rescan();
            for (size_t i = 0; i < visible_.size(); ++i) {
                if (entries_[static_cast<size_t>(visible_[i])].file == file) {
                    select(static_cast<int>(i));
                }
            }
        };
        if (file.existsAsFile()) {
            ui::confirm(ui::utf8("Substituir a sessão?"), ui::utf8("Já existe uma sessão chamada “") + name + ui::utf8("”."),
                        "Substituir", doSave);
        } else {
            doSave();
        }
    });
}

void SessionsPage::openSelected() {
    const Entry* entry = selectedEntry();
    if (entry == nullptr || !entry->valid) {
        return;
    }
    const juce::File file = entry->file;
    auto doOpen = [this, file] {
        const juce::String error = ctx_.openSession(file);
        if (error.isNotEmpty()) {
            ui::notify(ui::utf8("Não deu para abrir"), error);
        }
    };
    if (ctx_.engine().loopDefined()) {
        ui::confirm(ui::utf8("Abrir a sessão?"), ui::utf8("O loop que está gravado agora será substituído por “") +
                                                    file.getFileNameWithoutExtension() + ui::utf8("”."),
                    "Abrir", doOpen, false);
    } else {
        doOpen();
    }
}

void SessionsPage::exportSelected() {
    const Entry* entry = selectedEntry();
    if (entry == nullptr) {
        return;
    }
    LoopSession session;
    juce::String error = loopfile::load(entry->file, session);
    if (error.isEmpty()) {
        const juce::String name = entry->file.getFileNameWithoutExtension();
        const juce::File folder = ctx_.stemsFolder().getChildFile(juce::File::createLegalFileName(name));
        juce::Array<juce::File> written;
        error = loopfile::exportStems(session, folder, name, &written);
        if (error.isEmpty()) {
            folder.revealToUser();
            ui::notify(ui::utf8("Stems exportados"),
                       juce::String(written.size()) + ui::utf8(" arquivos WAV (um por track e o mix) em:\n") +
                           folder.getFullPathName());
            return;
        }
    }
    ui::notify(ui::utf8("Não deu para exportar"), error);
}

void SessionsPage::renameSelected() {
    const Entry* entry = selectedEntry();
    if (entry == nullptr) {
        return;
    }
    const juce::File file = entry->file;
    ui::askText(ui::utf8("Renomear sessão"), ui::utf8("Novo nome:"), file.getFileNameWithoutExtension(), "Renomear",
                [this, file](juce::String name) {
                    if (name.isEmpty()) {
                        return;
                    }
                    const juce::File target =
                        file.getSiblingFile(juce::File::createLegalFileName(name) + loopfile::kExtension);
                    if (target.exists() && target != file) {
                        ui::notify(ui::utf8("Nome em uso"), ui::utf8("Já existe uma sessão com esse nome."));
                        return;
                    }
                    if (!file.moveFileTo(target)) {
                        ui::notify(ui::utf8("Não deu para renomear"), file.getFullPathName());
                    }
                    rescan();
                });
}

void SessionsPage::duplicateSelected() {
    const Entry* entry = selectedEntry();
    if (entry == nullptr) {
        return;
    }
    const juce::File target = entry->file.getParentDirectory().getNonexistentChildFile(
        entry->file.getFileNameWithoutExtension() + ui::utf8(" (cópia)"), loopfile::kExtension, false);
    if (!entry->file.copyFileTo(target)) {
        ui::notify(ui::utf8("Não deu para duplicar"), target.getFullPathName());
    }
    rescan();
}

void SessionsPage::trashSelected() {
    const Entry* entry = selectedEntry();
    if (entry == nullptr) {
        return;
    }
    const juce::File file = entry->file;
    ui::confirm(ui::utf8("Mover para a lixeira?"),
                ui::utf8("“") + file.getFileNameWithoutExtension() +
                    ui::utf8("” vai para a Lixeira do Windows. Dá para recuperar de lá."),
                "Mover para a lixeira", [this, file] {
                    if (!file.moveToTrash()) {
                        ui::notify(ui::utf8("Não deu para mover"), file.getFullPathName());
                    }
                    rescan();
                });
}

void SessionsPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    const int saveW = theme::px(230);
    save_.setBounds(header_.withX(header_.getRight() - saveW).withWidth(saveW).withSizeKeepingCentre(saveW, theme::px(42)));

    compact_ = getWidth() < theme::px(760);
    const int gap = theme::px(10);
    if (compact_) {
        // Acoes em duas linhas embaixo; o detalhe em texto fica so no cartao.
        auto actions = area.removeFromBottom(theme::px(40) * 2 + gap);
        area.removeFromBottom(theme::px(12));
        auto row1 = actions.removeFromTop(theme::px(40));
        actions.removeFromTop(gap);
        auto row2 = actions;
        const int w = (row1.getWidth() - gap * 2) / 3;
        for (auto* b : {&open_, &export_, &rename_}) {
            b->setBounds(row1.removeFromLeft(w));
            row1.removeFromLeft(gap);
        }
        for (auto* b : {&duplicate_, &trash_, &reveal_}) {
            b->setBounds(row2.removeFromLeft(w));
            row2.removeFromLeft(gap);
        }
        detail_ = {};
        detailText_ = {};
    } else {
        detail_ = area.removeFromRight(juce::jmax(theme::px(290), area.getWidth() * 28 / 100));
        area.removeFromRight(theme::px(18));
        auto inner = detail_.reduced(theme::px(18));
        // Renomear e Duplicar dividem uma linha: sobra altura para os nomes das
        // quatro tracks no detalhe.
        const int rowH = theme::px(40);
        for (auto* b : {&reveal_, &trash_}) {
            b->setBounds(inner.removeFromBottom(rowH));
            inner.removeFromBottom(gap);
        }
        auto pair = inner.removeFromBottom(rowH);
        inner.removeFromBottom(gap);
        rename_.setBounds(pair.removeFromLeft((pair.getWidth() - gap) / 2));
        pair.removeFromLeft(gap);
        duplicate_.setBounds(pair);
        for (auto* b : {&export_, &open_}) {
            b->setBounds(inner.removeFromBottom(rowH));
            inner.removeFromBottom(gap);
        }
        detailText_ = inner;
    }

    search_.setFont(theme::text(theme::pxf(15.0f)));
    search_.setIndents(theme::px(12), (theme::px(40) - theme::px(18)) / 2);
    search_.setBounds(area.removeFromTop(theme::px(40)));
    area.removeFromTop(theme::px(14));
    viewport_.setBounds(area);
    grid_->layoutFor(juce::jmax(100, viewport_.getMaximumVisibleWidth()));
}

void SessionsPage::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    const int count = static_cast<int>(entries_.size());
    ui::paintPageTitle(g, header_.withTrimmedRight(theme::px(240)), ui::utf8("Sessões"),
                       juce::String(count) + (count == 1 ? ui::utf8(" sessão em ") : ui::utf8(" sessões em ")) +
                           ctx_.loopsFolder().getFullPathName());
    if (detail_.isEmpty()) {
        return;
    }
    theme::paintCard(g, detail_.toFloat().reduced(2.0f), theme::pxf(16.0f));
    auto r = detailText_.toFloat();
    g.setColour(p.t3);
    g.setFont(theme::caps(theme::pxf(11.0f)));
    g.drawText(ui::utf8("SESSÃO SELECIONADA"), r.removeFromTop(theme::pxf(20.0f)), juce::Justification::centredLeft, false);

    const Entry* entry = selectedEntry();
    if (entry == nullptr) {
        g.setColour(p.t2);
        g.setFont(theme::text(theme::pxf(14.0f)));
        g.drawFittedText(ui::utf8("Escolha uma sessão na lista."), r.removeFromTop(theme::pxf(40.0f)).toNearestInt(),
                         juce::Justification::topLeft, 2, 0.9f);
        return;
    }
    g.setColour(p.t1);
    g.setFont(theme::text(theme::pxf(21.0f), theme::Weight::Semibold));
    g.drawFittedText(entry->file.getFileNameWithoutExtension(), r.removeFromTop(theme::pxf(34.0f)).toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.8f);
    r.removeFromTop(theme::pxf(6.0f));

    auto line = [&](const juce::String& key, const juce::String& value) {
        auto row = r.removeFromTop(theme::pxf(22.0f));
        g.setColour(p.t3);
        g.setFont(theme::text(theme::pxf(13.0f)));
        g.drawText(key, row, juce::Justification::centredLeft, false);
        g.setColour(p.t1);
        g.setFont(theme::numbers(theme::pxf(13.0f)));
        g.drawText(value, row, juce::Justification::centredRight, false);
    };
    if (entry->valid) {
        line(ui::utf8("Duração"), juce::String(entry->info.seconds(), 1) + " s");
        line("Salva", dateText(entry->modified));
        line("Taxa", juce::String(entry->info.sampleRate / 1000.0, 1) + " kHz");
        line("Tracks", juce::String(entry->info.tracksWithAudio()) + " de " + juce::String(config::kNumTracks));
        r.removeFromTop(theme::pxf(10.0f));
        // Duas colunas (1 e 2 em cima, 3 e 4 embaixo).
        const float colW = r.getWidth() * 0.5f;
        juce::Rectangle<float> pairRow;
        for (int t = 0; t < config::kNumTracks; ++t) {
            if (t % 2 == 0) {
                if (r.getHeight() < theme::pxf(20.0f)) {
                    break;
                }
                pairRow = r.removeFromTop(theme::pxf(22.0f));
            }
            const auto row = t % 2 == 0 ? pairRow.withWidth(colW) : pairRow.withTrimmedLeft(colW);
            const bool has = entry->info.hasAudio[static_cast<size_t>(t)];
            const float d = theme::pxf(9.0f);
            g.setColour(has ? theme::track(t) : p.s3);
            g.fillEllipse(row.getX(), row.getCentreY() - d * 0.5f, d, d);
            g.setColour(has ? p.t1 : p.t3);
            g.setFont(theme::text(theme::pxf(13.0f)));
            const juce::String name = entry->info.names[static_cast<size_t>(t)];
            g.drawText(has ? (name.isNotEmpty() ? name : "Track " + juce::String(t + 1)) : juce::String("vazia"),
                       row.withTrimmedLeft(d * 1.8f), juce::Justification::centredLeft, false);
        }
    } else {
        g.setColour(p.amber);
        g.setFont(theme::text(theme::pxf(13.0f)));
        g.drawFittedText(ui::utf8("Não consegui ler este arquivo. Ele pode ser de outra versão ou estar corrompido."),
                         r.removeFromTop(theme::pxf(60.0f)).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    }
}
