# BPCB

[![Checks](https://github.com/IcaroRamosDM/BPCB/actions/workflows/checks.yml/badge.svg)](https://github.com/IcaroRamosDM/BPCB/actions/workflows/checks.yml)

Executor de testes para PCBs, com pacotes `.bancada`, interface gráfica e ferramentas de comunicação. Os pacotes podem ser produzidos pelo [BPCB Studio](https://github.com/IcaroRamosDM/BPCB-Studio).

A versão 0.16.0 abre pacotes de teste, registra medições e exporta relatórios. Na bancada, o BPCB pode comunicar por I²C, SPI, UART ou JTAG. Para experimentar a interface sem conectar uma placa, abra a demonstração em simulação.

## Compilar no Linux

Requer Python 3.12+, CMake 3.20+, compiladores C17/C++17 e as bibliotecas de
desenvolvimento listadas no instalador. Em Debian/Ubuntu, revise e execute:

```sh
bash scripts/instalar-dependencias.sh
bash scripts/compilar.sh
./trabalho/build/bancada_pcb
```

O primeiro build baixa FLTK 1.4.5 de sua publicação oficial e confere SHA-256.
Dependências e builds ficam em pastas locais ignoradas pelo Git. Para uso offline,
coloque `fltk-1.4.5-source.tar.gz` oficial em `dependencias/downloads/` antes do build.

Para usar uma instalação FLTK 1.4 existente:

```sh
cmake -S . -B trabalho/build -DFLTK_DIR=/caminho/share/fltk -DCMAKE_BUILD_TYPE=Debug
cmake --build trabalho/build -j4
ctest --test-dir trabalho/build --output-on-failure -j1
```

## Usar na bancada

Selecione o adaptador, confira a pinagem e a tensão lógica da sua placa e revise a configuração antes de transmitir. O histórico de medições começa desativado. Instale apenas pacotes `.bancada` de procedência conhecida, pois contêm código executável.

[Adaptadores e conexões](docs/COMUNICACAO.md) · [Laboratório](docs/LABORATORIO.md) · [Preparar um teste](docs/WORKBENCH.md)

## Autor

**Icaro Ramos Rodrigues dos Santos**<br>
Engenheiro Eletrônico

Embedded Systems | Firmware | C | C++ | Python | Dart | Assembly | Hardware Design | PCB Design | STM32 | ARM Cortex-M | Linux | Windows | RISC-V | Verilog | SystemVerilog | UVM | Design Verification | Instrumentation | Signal Processing | Qt | FLTK

GitHub: [IcaroRamosDM](https://github.com/IcaroRamosDM)<br>
LinkedIn: [icaro-ramos-r](https://www.linkedin.com/in/icaro-ramos-r/)<br>
[icaroelt@gmail.com](mailto:icaroelt@gmail.com)
