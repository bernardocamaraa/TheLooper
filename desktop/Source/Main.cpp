// Ponto de entrada do app desktop. Abre a JANELA DE CONTROLES; a janela de
// VUs e criada pela MainComponent (ver PerformanceComponent) para poder ir
// para o segundo monitor.
#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"

class PedalLooperApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Pedal Looper"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { mainWindow_.reset(new MainWindow(getApplicationName())); }
    void shutdown() override { mainWindow_ = nullptr; }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name + juce::String(juce::CharPointer_UTF8(" — Controles")), theme::enclosure, DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setResizable(true, false);
            // Abaixo disto a coluna de ajustes e a mesa se atropelam.
            setResizeLimits(900, 620, 6000, 4000);

            auto content = std::make_unique<MainComponent>();
            content_ = content.get();
            setContentOwned(content.release(), true);

            // Posicao salva da ultima sessao; senao, monitor principal (a
            // janela de VUs e que vai para o segundo, ver MainComponent).
            const juce::Rectangle<int> saved =
                Settings::ensureOnScreen(content_->settings().windowBounds("controls"));
            if (!saved.isEmpty()) {
                setBounds(saved);
            } else {
                centreWithSize(1040, 760);
            }

            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        // Atalhos do pedal. Tratados aqui, na janela, e nao no conteudo: a
        // tecla so sobe ate este ponto quando o componente com foco nao a
        // consumiu, entao editar o nome de uma track continua funcionando
        // normalmente (o TextEditor fica com as teclas enquanto edita).
        bool keyPressed(const juce::KeyPress& key) override {
            if (content_ != nullptr && content_->handlePedalKey(key)) {
                return true;
            }
            return DocumentWindow::keyPressed(key);
        }

        // A posicao e salva a cada movimento/redimensionamento: com dois
        // monitores, arrumar as janelas uma vez tem de valer para sempre.
        void moved() override {
            DocumentWindow::moved();
            saveBounds();
        }

        void resized() override {
            DocumentWindow::resized();
            saveBounds();
        }

    private:
        void saveBounds() {
            // resized() e chamado durante a construcao, antes de content_
            // existir.
            if (content_ != nullptr) {
                content_->saveControlsWindowBounds(getBounds());
            }
        }

        MainComponent* content_ = nullptr;
    };

private:
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(PedalLooperApplication)
