// Ponto de entrada do app desktop: a JANELA PRINCIPAL (abas + HUD). A tela de
// performance (segundo monitor) e criada pela MainComponent.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "ui/Brand.h"

class PedalLooperApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "The Looper"; }
    const juce::String getApplicationVersion() override { return "2.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        // A abertura ja sai no tema escolhido (le so a preferencia, antes do
        // resto subir).
        {
            Settings settings;
            const int mode = settings.themeMode();
            theme::setMode(mode == 1 ? theme::Mode::Light : mode == 2 ? theme::Mode::System : theme::Mode::Dark);
        }
        // Fica na tela enquanto o device de audio abre (o que pode levar alguns
        // segundos com ASIO) e some sozinha depois.
        auto* splash = new juce::SplashScreen("The Looper",
                                              brand::splashImage(560, 380, juce::String(juce::CharPointer_UTF8(
                                                                               "Abrindo o áudio e procurando o pedal…"))),
                                              true);
        mainWindow_.reset(new MainWindow(getApplicationName()));
        splash->deleteAfterDelay(juce::RelativeTime::seconds(1.2), false);
    }

    void shutdown() override { mainWindow_ = nullptr; }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, theme::enclosure, DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setResizable(true, false);
            // Estreita o bastante para o layout de celular (barra de abas embaixo).
            setResizeLimits(400, 640, 6000, 4000);

            auto content = std::make_unique<MainComponent>();
            content_ = content.get();
            setContentOwned(content.release(), true);

            const juce::Rectangle<int> saved = Settings::ensureOnScreen(content_->settings().windowBounds("controls"));
            if (!saved.isEmpty()) {
                setBounds(saved);
            } else {
                centreWithSize(1180, 800);
            }
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        // Atalhos do pedal e das abas. Tratados na janela: a tecla so chega
        // aqui quando o componente com foco nao a consumiu (renomear uma track
        // continua funcionando normalmente).
        bool keyPressed(const juce::KeyPress& key) override {
            if (content_ != nullptr && content_->handleKey(key)) {
                return true;
            }
            return DocumentWindow::keyPressed(key);
        }

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
