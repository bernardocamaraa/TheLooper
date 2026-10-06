# Compila o plugin VST3 e troca a copia que o FL Studio carrega.
#
# Existe porque "compilei e o FL continua mostrando o plugin velho" tem tres
# causas, e as tres sao silenciosas:
#   1. so o app foi compilado, o target do plugin nao;
#   2. o FL estava aberto, travando o .vst3, e o build nao conseguiu
#      sobrescreve-lo;
#   3. ha duas copias (a pasta build no "Plugin search paths" do FL e outra em
#      Common Files\VST3) e o FL abre a velha.
# Este script trata as tres. Rode pelo atualizar-plugin.bat (duplo clique).
#
# So a copia final pede administrador (Common Files e protegida). O build roda
# como usuario normal: compilar como admin deixaria a pasta build com arquivos
# que depois o usuario normal nao consegue sobrescrever.

$ErrorActionPreference = 'Stop'

$desktopDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $desktopDir 'build'
$builtPlugin = Join-Path $buildDir 'PedalLooperPlugin_artefacts\Release\VST3\PEDAL Looper.vst3'
$vst3Dir = Join-Path $env:CommonProgramFiles 'VST3'
$installedPlugin = Join-Path $vst3Dir 'PEDAL Looper.vst3'

function Fail([string]$message) {
    Write-Host ''
    Write-Host "ERRO: $message" -ForegroundColor Red
    Write-Host ''
    Read-Host 'Aperte Enter para fechar'
    exit 1
}

function Step([string]$message) {
    Write-Host ''
    Write-Host "==> $message" -ForegroundColor Cyan
}

# --- 1. FL fechado -----------------------------------------------------------
# Com o FL aberto o .vst3 esta travado: o build falha ao sobrescreve-lo e a
# copia para Common Files tambem.
Step 'Conferindo se o FL Studio esta fechado'
while (Get-Process -Name 'FL64', 'FL' -ErrorAction SilentlyContinue) {
    Write-Host 'O FL Studio esta aberto. Feche o FL e aperte Enter (ou Ctrl+C para cancelar).' -ForegroundColor Yellow
    Read-Host | Out-Null
}
Write-Host 'OK'

# --- 2. CMake ----------------------------------------------------------------
# Usa o do PATH; se nao houver, o que vem junto com o Visual Studio.
Step 'Procurando o CMake'
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { $cmake = $candidate }
        }
    }
}
if (-not $cmake) {
    Fail 'CMake nao encontrado. Instale o Visual Studio com "Desenvolvimento para desktop com C++" (ver docs/BUILD.md).'
}
Write-Host $cmake

# --- 3. Configurar (so na primeira vez) --------------------------------------
# Se a pasta build ja existe, NAO reconfigura: ela guarda o caminho do ASIO
# SDK que o app standalone usa, e reconfigurar sem ele tiraria o ASIO do app.
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Step 'Configurando o projeto (primeira vez)'
    & $cmake -S $desktopDir -B $buildDir
    if ($LASTEXITCODE -ne 0) { Fail 'A configuracao do CMake falhou - veja as mensagens acima.' }
}

# --- 4. Compilar o plugin ----------------------------------------------------
Step 'Compilando o plugin (pode levar alguns minutos)'
$buildStart = Get-Date
& $cmake --build $buildDir --config Release --target PedalLooperPlugin_VST3
if ($LASTEXITCODE -ne 0) { Fail 'A compilacao do plugin falhou - veja as mensagens acima.' }

# Confere que o arquivo foi mesmo reescrito AGORA: e exatamente o caso em que
# o build "passa" e o plugin continua o velho.
$binary = Get-ChildItem -Path (Join-Path $builtPlugin 'Contents') -Recurse -Filter '*.vst3' -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $binary) { Fail "O build terminou mas o plugin nao foi encontrado em $builtPlugin" }
if ($binary.LastWriteTime -lt $buildStart.AddMinutes(-1)) {
    # Nada mudou desde o ultimo build: o arquivo antigo ja e o atual. Avisa,
    # mas segue - pode ser so que nao havia nada novo para compilar.
    Write-Host "Aviso: o plugin compilado e de $($binary.LastWriteTime) (nada mudou desde o ultimo build)." -ForegroundColor Yellow
}
Write-Host "Plugin compilado: $($binary.LastWriteTime)"

# --- 5. Trocar a copia em Common Files\VST3 (pede administrador) -------------
# Apaga a pasta inteira antes de copiar: copiar por cima deixaria para tras
# arquivos de um build antigo que nao existem mais no novo.
Step 'Instalando em Common Files\VST3 (o Windows vai pedir permissao)'
# Os caminhos vao entre aspas simples no comando; uma aspa simples no nome de
# usuario (C:\Users\D'Angelo...) quebraria o comando, entao e dobrada.
$q = { param($path) "'" + ($path -replace "'", "''") + "'" }
$copyCommand = @"
`$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $(& $q $vst3Dir) | Out-Null
if (Test-Path $(& $q $installedPlugin)) { Remove-Item -Recurse -Force $(& $q $installedPlugin) }
Copy-Item -Recurse -Force $(& $q $builtPlugin) $(& $q $installedPlugin)
"@
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($copyCommand))
try {
    $proc = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden `
        -ArgumentList '-NoProfile', '-EncodedCommand', $encoded
} catch {
    Fail 'Permissao de administrador negada - o plugin nao foi instalado.'
}
if ($proc.ExitCode -ne 0) { Fail "A copia para $installedPlugin falhou." }

$installedBinary = Get-ChildItem -Path (Join-Path $installedPlugin 'Contents') -Recurse -Filter '*.vst3' -File |
    Select-Object -First 1
if (-not $installedBinary -or $installedBinary.LastWriteTime -ne $binary.LastWriteTime) {
    Fail "A copia em $installedPlugin nao bate com o plugin compilado."
}
Write-Host "Instalado em $installedPlugin"

# --- 6. O que o script nao consegue fazer -----------------------------------
Write-Host ''
Write-Host 'PRONTO. Falta so, no FL Studio:' -ForegroundColor Green
Write-Host '  1. Options > Manage plugins: se a pasta "build" do projeto estiver em'
Write-Host '     "Plugin search paths", REMOVA - senao o FL acha duas copias do plugin.'
Write-Host '  2. Clique em "Find more plugins".'
Write-Host '  3. Coloque o PEDAL Looper num slot de efeito do Mixer.'
# O carimbo e o que o build gravou em generated\BuildInfo.cpp (ver
# cmake/BuildStamp.cmake) - e o MESMO texto que o plugin mostra no rodape.
$stampFile = Join-Path $buildDir 'generated\BuildInfo.cpp'
$stamp = $null
if (Test-Path $stampFile) {
    $match = Select-String -Path $stampFile -Pattern 'return "([^"]+)"'
    if ($match) { $stamp = $match.Matches[0].Groups[1].Value }
}
if ($stamp) {
    Write-Host "  4. Confira no rodape do plugin: tem de aparecer ""build $stamp""."
}
Write-Host ''
Read-Host 'Aperte Enter para fechar'
