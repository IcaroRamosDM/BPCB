# BPCB

Executor de testes para PCBs, com pacotes `.bancada`, interface gráfica e ferramentas de comunicação. Os pacotes podem ser produzidos pelo [BPCB Studio](https://github.com/IcaroRamosDM/BPCB-Studio).

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

## Uso e validação

Comece pelos exemplos em simulação. Selecione o adaptador e confira pinagem, tensão,
alimentação e configuração antes de autorizar qualquer transmissão. Recursos disponíveis
dependem do modelo, modo, driver e dispositivo; listar um adaptador não comprova que
todos os protocolos foram qualificados em hardware. Consulte os documentos em `docs/`.

Histórico de resultados exige escolha explícita do usuário e começa desativado.
A demonstração gerada pelo build não acessa hardware. Pacotes de terceiros contêm código executável e devem ser revisados antes da instalação.

Este repositório publica os fontes; não inclui os antigos instaladores locais.
