# Build do app desktop (Windows, JUCE + ASIO). Ver docs/BUILD.md para os
# pre-requisitos (JUCE, ASIO SDK, VB-CABLE) antes de configurar este projeto.
#
# SAO DOIS TARGETS a partir do mesmo codigo:
#   PedalLooper       - o app standalone (o de sempre, com ASIO e o pedal serial)
#   PedalLooperPlugin - o mesmo looper como VST3, para rodar dentro do FL Studio
#
# As fontes compartilhadas sao listadas UMA vez (kPedalCoreSources) e adicionadas
# aos dois targets. Nao ha biblioteca intermediaria de proposito: cada target
# JUCE compila os modulos do JUCE com os seus proprios defines
# (JUCE_STANDALONE_APPLICATION vs JucePlugin_*), e uma lib compartilhada
# compilaria o JUCE uma terceira vez, com defines de ninguem. Compilar as
# fontes duas vezes custa alguns minutos e nao custa nenhuma surpresa.
cmake_minimum_required(VERSION 3.22)
project(PedalLooper VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Este app depende de ASIO/WASAPI e SetupAPI - so builda no Windows.")
endif()

# JUCE: espera um checkout em desktop/JUCE (submodulo git recomendado:
# `git submodule add https://github.com/juce-framework/JUCE.git desktop/JUCE`).
# O SDK do VST3 ja vem dentro do JUCE - nao precisa baixar nada para o plugin.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE nao encontrado em desktop/JUCE - ver docs/BUILD.md.")
endif()
add_subdirectory(JUCE)

# ASIO SDK da Steinberg: licenca nao permite redistribuir os headers neste
# repositorio - baixe separadamente e aponte esta variavel para a pasta
# extraida (deve conter common/, host/, host/pc/). Sem isso o app ainda
# compila, mas so com WASAPI/DirectSound (sem ASIO4ALL).
#
# So vale para o STANDALONE: dentro do FL quem abre o driver e o host.
set(ASIO_SDK_PATH "" CACHE PATH "Caminho para o ASIO SDK da Steinberg")

option(PEDAL_BUILD_PLUGIN "Compilar tambem a versao VST3 (para o FL Studio)" ON)

# --------------------------------------------------------------------------
# Fontes compartilhadas: o motor, os arquivos .loop, o pedal serial e todos os
# componentes de interface reaproveitaveis. Nada aqui sabe quem e o dono do
# device de audio - por isso serve para os dois targets.
# --------------------------------------------------------------------------
set(kPedalCoreSources
    Source/LooperEngine.cpp
    Source/AudioTrack.cpp
    Source/LayerStore.cpp
    Source/LoopFile.cpp
    Source/SerialLink.cpp
    Source/PedalLookAndFeel.cpp
    Source/ui/Icons.cpp
    Source/ui/Widgets.cpp
    Source/PedalMap.cpp
    Source/ModeFrame.cpp
    Source/LoopProgressBar.cpp
    Source/VuMeter.cpp
    Source/TrackControlStrip.cpp
    Source/TrackMeterPanel.cpp
)

# Ajustes que valem para qualquer target deste projeto.
function(pedal_configure_target target)
    target_include_directories(${target} PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    )

    if(MSVC)
        # Fontes em UTF-8: a interface e em portugues e precisa de acentuacao
        # correta ("Violao" -> "Violao" com til, "Latencia" com acento). Sem isto o
        # MSVC le os literais na codepage do sistema e os acentos saem quebrados.
        target_compile_options(${target} PRIVATE /utf-8)
        # Simbolos de depuracao TAMBEM no build Release (sem mexer na otimizacao):
        # o app ja teve crashes intermitentes, e um minidump sem PDB nao diz nada.
        # /OPT:REF e /OPT:ICF sao repostos porque /DEBUG os desliga por padrao.
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Zi>)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    endif()

    target_compile_definitions(${target} PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
    )
endfunction()

# --------------------------------------------------------------------------
# Target 1: app standalone
# --------------------------------------------------------------------------
# Logo e icone (Assets/, extraidos de thelooper.jpg) embutidos no executavel.
juce_add_binary_data(TheLooperAssets SOURCES
    Assets/logo-mark.png
    Assets/logo-wordmark.png
    Assets/logo-full.png
    Assets/app-icon.png
)

juce_add_gui_app(PedalLooper
    PRODUCT_NAME "The Looper"
    COMPANY_NAME "PEDAL"
    VERSION "2.0.0"
    ICON_BIG Assets/app-icon.png
    ICON_SMALL Assets/app-icon.png
)

target_sources(PedalLooper PRIVATE
    ${kPedalCoreSources}
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/PerformanceComponent.cpp
    Source/AudioEngine.cpp
    Source/VirtualMicOutput.cpp
    Source/Recorder.cpp
    Source/Settings.cpp
    Source/Setlist.cpp
    Source/ui/Brand.cpp
    Source/ui/AppShell.cpp
    Source/ui/pages/PlayPage.cpp
    Source/ui/pages/MixerPage.cpp
    Source/ui/pages/SessionsPage.cpp
    Source/ui/pages/SetlistPage.cpp
    Source/ui/pages/ScreensPage.cpp
    Source/ui/pages/SettingsPage.cpp
)

pedal_configure_target(PedalLooper)
target_link_libraries(PedalLooper PRIVATE TheLooperAssets)

if(ASIO_SDK_PATH)
    target_include_directories(PedalLooper PRIVATE
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host
        ${ASIO_SDK_PATH}/host/pc
    )
    target_compile_definitions(PedalLooper PRIVATE JUCE_ASIO=1)
else()
    message(WARNING "ASIO_SDK_PATH nao definido - build sem suporte a ASIO (so WASAPI/DirectSound). Ver docs/BUILD.md.")
    target_compile_definitions(PedalLooper PRIVATE JUCE_ASIO=0)
endif()

target_compile_definitions(PedalLooper PRIVATE
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
)

target_link_libraries(PedalLooper PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_core
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# --------------------------------------------------------------------------
# Testes do motor (sem JUCE, sem device de audio): gravam e desfazem centenas
# de camadas com audio sintetico e conferem a soma amostra por amostra.
# Rodar: build/Release/LooperEngineTests.exe
# --------------------------------------------------------------------------
option(PEDAL_BUILD_TESTS "Compilar os testes do motor" ON)
if(PEDAL_BUILD_TESTS)
    add_executable(LooperEngineTests
        Tests/LooperEngineTests.cpp
        Source/LooperEngine.cpp
        Source/AudioTrack.cpp
        Source/LayerStore.cpp
    )
    target_include_directories(LooperEngineTests PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    )
    if(MSVC)
        target_compile_options(LooperEngineTests PRIVATE /utf-8)
    endif()
endif()

# SetupAPI (auto-deteccao da porta COM do Mega por VID:PID, ver SerialLink.cpp)
# ja e vinculada via #pragma comment(lib, "setupapi.lib") no proprio arquivo
# (padrao MSVC) - nao precisa ser listada aqui.

# --------------------------------------------------------------------------
# Target 2: VST3 (FL Studio)
#
# Barramentos (ver PluginProcessor.h para o porque de cada um):
#   entrada  - "Violao" (principal) + "Voz" (sidechain)
#   saida    - "Mix" (principal) + uma por track, para o Auto map outputs do
#              wrapper do FL jogar cada track numa track do mixer.
# --------------------------------------------------------------------------
if(PEDAL_BUILD_PLUGIN)
    juce_add_plugin(PedalLooperPlugin
        PRODUCT_NAME "PEDAL Looper"
        COMPANY_NAME "PEDAL"
        PLUGIN_MANUFACTURER_CODE Pdal
        PLUGIN_CODE Pdlp
        FORMATS VST3
        VST3_CATEGORIES "Fx" "Tools"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        # O looper tem atalhos de teclado (ver PedalKeys.h) - sem isto o host
        # fica com as teclas e nada chega ao editor.
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # Copiar para C:\Program Files\Common Files\VST3 exige admin e faz o
        # build inteiro falhar num shell normal. O .vst3 fica em
        # build/PedalLooperPlugin_artefacts/Release/VST3 e essa pasta se
        # adiciona nos caminhos de busca do FL (Manage plugins) - ver
        # docs/BUILD.md.
        COPY_PLUGIN_AFTER_BUILD FALSE
        VERSION "1.0.0"
    )

    target_sources(PedalLooperPlugin PRIVATE
        ${kPedalCoreSources}
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp
    )

    pedal_configure_target(PedalLooperPlugin)

    # O host e o dono do device: nenhum backend de audio e compilado aqui.
    target_compile_definitions(PedalLooperPlugin PRIVATE
        JUCE_ASIO=0
        JUCE_WASAPI=0
        JUCE_DIRECTSOUND=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(PedalLooperPlugin PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_core
        juce::juce_data_structures
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    )
endif()
