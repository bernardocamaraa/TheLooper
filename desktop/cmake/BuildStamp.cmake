# Gera o .cpp com a data e hora do build (ver Source/BuildInfo.h).
#
# Roda a CADA build, chamado pelo target pedal_build_stamp do CMakeLists. Nao
# da para usar __DATE__/__TIME__ num .cpp comum: ele so e recompilado quando
# muda, entao o carimbo ficaria parado na data da ultima vez que alguem mexeu
# nele - justamente o que nao pode acontecer num carimbo para descobrir se o
# FL abriu o plugin novo ou um velho.
#
# Uso: cmake -DOUT=<arquivo.cpp> -P BuildStamp.cmake
string(TIMESTAMP stamp "%d/%m/%Y %H:%M")
set(content "// GERADO a cada build por cmake/BuildStamp.cmake - nao edite.\n#include \"BuildInfo.h\"\n\nconst char* buildinfo::stamp() { return \"${stamp}\"; }\n")

# So reescreve se mudou: o arquivo identico nao forca recompilar nem relinkar.
if(EXISTS "${OUT}")
    file(READ "${OUT}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUT}" "${content}")
