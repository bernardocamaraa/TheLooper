# Build

## Firmware (Arduino Mega 2560)

Pré-requisitos: [PlatformIO](https://platformio.org/) (extensão do VS Code ou
`pip install platformio`).

1. Abra a pasta `software/firmware` no VS Code com a extensão PlatformIO, ou
   rode pela CLI:
   ```
   pio run -e megaatmega2560 -d software/firmware
   ```
2. Grave no Mega:
   ```
   pio run -e megaatmega2560 -d software/firmware -t upload
   ```
3. **Antes de gravar**, confira `software/firmware/include/Config.h` e ajuste
   os números dos pinos (`kPin*`) para bater com a fiação real do pedal.
4. Para depurar, o Monitor Serial do PlatformIO mostra os bytes brutos do
   protocolo (não é texto legível — é o protocolo binário de
   `docs/PROTOCOL.md`).

## App desktop (Windows)

Pré-requisitos:

1. **JUCE** — clone como submódulo dentro de `software/desktop`:
   ```
   git submodule add https://github.com/juce-framework/JUCE.git software/desktop/JUCE
   ```
   (ou baixe/extraia manualmente em `software/desktop/JUCE`, contanto que
   `software/desktop/JUCE/CMakeLists.txt` exista).

2. **ASIO SDK** da Steinberg — necessário para o app enxergar o driver
   ASIO4ALL (ou qualquer outro driver ASIO da sua interface de áudio). A
   licença da Steinberg não permite redistribuir os headers neste
   repositório:
   - Baixe em https://www.steinberg.net/developers/ (seção "3rd Party
     Developers", ASIO SDK).
   - Extraia em qualquer pasta local (ex: `C:\asiosdk`).
   - Aponte o CMake para ela na configuração (próximo passo).

3. **ASIO4ALL** (ou o driver ASIO nativo da sua interface, se ela já tiver
   um) instalado no Windows — é o que faz a interface de áudio aparecer como
   dispositivo ASIO para o app escolher na tela de "Configurar Áudio...".

4. **VB-CABLE** (opcional, só necessário para a função de microfone virtual)
   — baixe e instale em https://vb-audio.com/Cable/ (gratuito). Depois de
   instalado, ele aparece como um dispositivo de áudio chamado algo como
   "CABLE Input (VB-Audio Virtual Cable)" — é isso que
   `desktop/Source/Config.h::kVirtualCableDeviceNameContains` procura. Sem
   isso instalado, o app funciona normalmente, só sem o microfone virtual.

### Configurar e compilar

```
cmake -S software/desktop -B software/desktop/build -DASIO_SDK_PATH="C:/asiosdk"
cmake --build software/desktop/build --config Release
```

Se `ASIO_SDK_PATH` não for informado, o build funciona mas só com
WASAPI/DirectSound (sem suporte a ASIO4ALL) — não recomendado para uso ao
vivo por causa da latência maior.

### Uso

1. Conecte o Arduino Mega por USB. O app auto-detecta a porta COM pelo
   VID:PID; se falhar, preencha a porta no campo "Porta COM" da seção ÁUDIO e
   clique em "Reconectar pedal" — não é mais preciso recompilar.
   O app também funciona **sem o pedal**, pelo teclado ou pelos botões da
   seção PEDAL (ver docs/CONTROL_MODEL.md).
2. Conecte a interface de áudio e tenha o driver ASIO instalado.
3. Abra o executável gerado. Use o botão "Configurar Áudio..." para escolher
   o dispositivo ASIO da sua interface, se não vier selecionado
   automaticamente.
4. Se a tela do pedal estiver conectada como segundo monitor do Windows, a
   janela abre nela automaticamente.

## Versão plugin (VST3, para o FL Studio)

O mesmo looper roda como VST3 dentro do FL. É o **mesmo motor** do
standalone (`LooperEngine`): a única diferença é quem entrega o áudio — no app
é um device ASIO, no plugin é o host. Por isso o plugin não tem diálogo de
áudio, nem microfone virtual, nem as janelas de apresentação.

O SDK do VST3 já vem dentro do JUCE — não há nada a baixar além do que o
standalone já exige.

### Compilar

O target sai junto no build normal. Para compilar só ele:

```
cmake --build software/desktop/build --config Release --target PedalLooperPlugin_VST3
```

O resultado é `PEDAL Looper.vst3` em
`software/desktop/build/PedalLooperPlugin_artefacts/Release/VST3/`.

A cópia automática para `C:\Program Files\Common Files\VST3` está **desligada**
(`COPY_PLUGIN_AFTER_BUILD FALSE`): ela exige um shell de administrador e fazia
o build inteiro falhar. Em vez disso, adicione a pasta acima aos caminhos de
busca do FL (`Options > Manage plugins > Plugin search paths`) e rode o "Find
more plugins". Assim cada rebuild já vale, sem copiar nada.

Para não compilar o plugin: `-DPEDAL_BUILD_PLUGIN=OFF`.

### Ligar no FL Studio

O looper trabalha com **duas entradas separadas** (violão e voz) e entrega
**cinco saídas** (o mix e as quatro tracks). O wrapper do FL faz as duas
ligações, ambas na aba **PROCESSING** do plugin.

**Entradas.** Ponha o plugin na track do mixer do violão. Na track do
microfone, clique com o botão direito no *Track Send* apontando para a track do
plugin e escolha **"Sidechain to this track"**. Depois, no wrapper do plugin,
use **"Auto map inputs"**. A track sidechainada de menor número vira a segunda
entrada do plugin.

Cada entrada é somada em mono `(L+R)/2` — é por isso que mandar o mesmo mono
duplicado nos dois lados (o caso normal de um violão ou um microfone) devolve
exatamente o sinal, sem os +6 dB que uma soma daria.

Se o violão e a voz estiverem trocados, não refaça o roteamento: use o botão
**"Entrada principal: Violão / Voz"** na página PEDAL do plugin.

**Saídas.** No wrapper, use **"Auto map outputs"**. As quatro saídas de track
caem nas quatro tracks do mixer seguintes à do plugin, em ordem. Com elas
mapeadas, **desligue "Mix na saída principal"** no plugin — senão o mix e as
quatro tracks somam duas vezes.

O que sai em cada track já passou pelo fader e pelo mute daquela track do
looper, mas **não** pelo limiter, que continua sendo só do mix.

Com as quatro tracks do mixer armadas para gravar em disco, o FL entrega
quatro arquivos separados.

### Latência

Dentro do host o plugin **não consegue ler a latência do driver** — o device é
do FL. Sobra o ajuste manual em ms (página PEDAL), que é salvo no projeto.
Se o overdub cair atrasado, aumente.

### Pedal físico

O plugin abre a porta serial **só quando você clica em "Conectar pedal"**, e
não sozinho: o FL instancia o plugin durante a varredura de plugins, e a porta
COM é de um processo por vez. Pelo mesmo motivo, **o app standalone e o plugin
não podem usar o pedal ao mesmo tempo** — feche um antes de conectar o outro.

A escolha (ligado/desligado e a porta) fica salva no projeto do FL, junto com
a mesa, os nomes das tracks e o trim de latência.
