// Carimbo do build, mostrado no rodape do app e do plugin.
//
// Serve para uma pergunta so: "o FL abriu o plugin que eu acabei de compilar,
// ou uma copia velha?". Com duas copias do .vst3 no computador, ou com o FL
// aberto travando o arquivo durante o build, as duas coisas parecem iguais
// ate se comparar a hora.
//
// O .cpp e gerado a cada build por cmake/BuildStamp.cmake.
#pragma once

namespace buildinfo {

// "dd/mm/aaaa hh:mm", hora local da maquina que compilou.
const char* stamp();

} // namespace buildinfo
