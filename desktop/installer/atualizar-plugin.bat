@echo off
rem Duplo clique aqui: compila o plugin VST3 e troca a copia que o FL Studio
rem carrega. O trabalho todo esta em atualizar-plugin.ps1 - ver o comentario
rem no topo dele. O -ExecutionPolicy Bypass vale so para esta execucao.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0atualizar-plugin.ps1"
