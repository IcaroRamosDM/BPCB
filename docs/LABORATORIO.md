Atualização 0.16.0 (17/10/2026): consulte [WORKBENCH.md](WORKBENCH.md) para mapas, quadros UART e o novo fluxo. O contexto anterior abaixo permanece como histórico.

# Laboratório BPCB 0.15.0

Atualizado em 15/10/2026. BPCB executa ensaios; BPCB Studio 0.5 cria as sequências. São projetos independentes. O laboratório fica em **Ferramentas > Comunicação da PCB > Terminal e exploradores > Laboratório**. As operações manuais usam a seleção aplicada na janela Comunicação; as sequências instaladas usam os adaptadores associados a seus canais.

## Componentes IoT

A aba Componentes oferece os dispositivos abaixo. Escolha endereço de 7 bits, ação e grandeza. Não identifique um componente somente por ACK: confirme placa, modelo, alimentação e pinagem. Inicializar/medir escreve configuração no componente; ler utiliza a configuração existente quando aplicável.

| Dispositivo | Grandezas | Parâmetros principais |
|---|---|---|
| BME280 | Temperatura °C, pressão Pa, umidade %RH | 0x76/0x77, calibração interna, conversão forçada |
| BMP280 | Temperatura °C, pressão Pa | 0x76/0x77, identificação e compensação próprias |
| INA219 | Tensão V, corrente A, potência W | Endereço, shunt real em ohms, LSB de corrente; calibração efetiva conferida |
| MPU6050 | Aceleração m/s², giro °/s, temperatura °C | Endereço, faixas do acelerômetro/giroscópio; escalas lidas do dispositivo |
| OLED SSD1306 | Comunicação ACK e padrão visual | 128x32/128x64, quadriculado/barras/preto/branco; conferência visual obrigatória |

As operações têm limite de 5 s, cancelamento e até 256 transferências de evidência. O resultado preserva a leitura escolhida, as demais grandezas e o trace I²C. OLED com ACK não significa pixels aprovados. No Studio, adicione confirmação visual e critérios numéricos adequados à PCB. Os valores de exemplo não são limites universais de aceitação.

## UART e protocolos

A aba UART oferece AT, resposta delimitada, comprimento fixo e Modbus RTU. Configuração serial e níveis elétricos pertencem ao adaptador; protocolo pertence à etapa. Bluetooth com comandos AT pode usar esse caminho se sua documentação confirmar os comandos. Wi-Fi ESP-AT é atendido pelo parser; outros módulos podem usar blocos UART/SPI/I²C e exigem protocolo específico.

- AT: comando textual único, ASCII/hex, terminação configurável, eco, resposta de sucesso exata ou por prefixo e eventos ESP-AT separados. ERROR/FAIL/busy interrompem a aprovação. Bytes antigos são drenados antes de transmitir; fluxo continuamente ocupado é recusado. +IPD simples com comprimento, com ou sem link, é separado da resposta. Variantes CIPDINFO ambíguas não são aceitas como OK.
- Delimitada: recebe até a primeira terminação, mesmo fragmentada; bytes posteriores ficam na fila para a próxima operação. Comprimento fixo recebe a quantidade declarada.
- Modbus RTU: funções 1, 2, 3, 4, 5, 6, 15 e 16; valida endereço, função, tamanho, eco de escrita, exceção e CRC16. Não aceita broadcast. Use transceptor RS-485 com direção automática quando necessário; UART TTL não é RS-485. A guarda conservadora de silêncio não comprova temporização física do barramento. O primeiro registrador/bit fornece o valor e os bytes completos permanecem na evidência; use fórmulas para escala específica.

Limites por bloco: 4096 bytes, 128 linhas e 50 a 10000 ms, também sujeitos aos limites do adaptador/pacote. Uma resposta recebida após o prazo não aprova o bloco. Resposta inválida força falha, mesmo que seu valor numérico caia em uma faixa aceitável.

## Vários canais na mesma sequência

O Studio permite até oito canais nomeados. Cada canal declara barramento, adaptadores permitidos, clock, timeout e tamanho máximo. No BPCB, abra o teste instalado, configure/aplique cada adaptador e associe-o na aba Canais. O caminho ou série deve identificar exatamente o equipamento. Associações ficam somente em memória e são descartadas ao mudar de teste ou fechar.

Interfaces diferentes podem permanecer abertas durante a sequência. Interfaces USB compartilhadas são alternadas sequencialmente, com bloqueio contra outro processo. Ao reabrir JTAG, é obrigatório definir IR novamente antes de DR. Falha não provoca busca automática de outro adaptador. Não há garantia de operações simultâneas em barramentos de um único CH347.

## Firmware STM32, ESP32 e FPGA

A aba Firmware gerencia manifesto JSON schema 1 com nome, versão, família, alvo exato, backend e uma ou mais imagens. O manifesto referencia arquivos locais; ao transferi-lo para outro computador, leve também as imagens correspondentes. Não há download automático de firmware/ferramentas.

| Família | Formatos/backend | Limites |
|---|---|---|
| STM32 | BIN com endereço; ELF32 ARM executável; Intel HEX; OpenOCD | Múltiplos BIN são reunidos em HEX sem preencher lacunas |
| ESP32 | BIN com endereços explícitos; esptool v5 via UART, ou uma imagem via OpenOCD/JTAG | Até 16 imagens, setores de 4096 bytes sem sobreposição |
| FPGA | Um SVF ou XSVF; OpenOCD/JTAG | Vetor preparado para o FPGA/cadeia exatos; não converte bitstream arbitrário |

Máximo: 16 MiB de imagens no pacote. ELF/HEX têm validação de cabeçalhos, limites, checksum/regiões e sobreposição. As regiões genéricas de família **não substituem** conferir a capacidade/endereço reais do alvo. BIN não permite deduzir compatibilidade de hardware; SVF/XSVF também precisa ser confiável e corresponder à cadeia.

Procedimento: escolha seleção física e alvo; adicione/importa imagens; selecione executável/scripts locais confiáveis; gere a prévia; confira hashes, endereços, identidade e comandos; confirme expressamente a programação. A execução confere novamente o SHA da prévia, da ferramenta e das imagens e cria cópias temporárias privadas antes de transmitir. Caminhos e argumentos não passam por shell. Saída da ferramenta é limitada a 64 KiB e processos descendentes são encerrados ao finalizar/cancelar. Não há erase-all, force, alteração de eFuses ou reset automático pela integração esptool; coloque o alvo no bootloader conforme seu manual. Verificar também pode parar/alterar o estado do alvo conforme a ferramenta.

OpenOCD é externo e opcional. Para usar um CH347, escolha uma versão do OpenOCD que inclua esse driver. FPGA exige configuração de TAP/IR/ID e scripts apropriados; STM32/ESP32 exigem o modelo exato. UART esptool usa porta VCP; CH347 M2/HID não é porta VCP para esptool.

## Reprodução offline e falhas

Exporte captura UART somente quando desejar. **Limpar captura** inicia uma captura nova em memória, mediante confirmação. Para reprodução, importe JSON de captura completa de uma sessão ou trace de leitura IoT. Capturas com eventos perdidos/múltiplas sessões são recusadas. A importação agrupa RX entre comandos e conserva o atraso até o primeiro byte; não é um analisador temporal de cada fragmento.

Escolha a operação das abas Componentes/UART ou a transferência manual. Falhas: fragmentação UART, atraso, timeout, truncamento, corrupção (índice e máscara XOR), desconexão, NACK I²C. O índice de transação começa em zero. Falhas incompatíveis/índices inexistentes são recusados. Tempo é virtual, e não há fallback para hardware. O resultado mostra transações consumidas, falha aplicada, bytes e motivo. Capturas podem conter dados do módulo; exportação é explícita e não ativa histórico.

Formato mínimo de reprodução:

```json
{"schema":1,"name":"AT ilustrativo","bus":"uart","exchanges":[{"operation":{"write":"41 54 0D 0A","read":4},"received":"4F 4B 0D 0A","delay_ms":0}]}
```

Operação correspondente:

```json
{"kind":"uart_protocol","protocol":"at","write":"41 54","delimiter":"crlf","timeout_ms":500,"max_bytes":128}
```

## CLI, formatos e persistência

`bancada-core lab-catalog`; `comm-exchange CONFIG OPERACAO` aceita também os blocos IoT/UART; `replay CAPTURA OPERACAO [FALHA]`; `replay-import CAPTURA`; `firmware-plan PEDIDO`; `firmware-run PEDIDO`; `qualify-plan CONFIG PROCEDIMENTO`; `qualify-assess PLANO RESPOSTAS`. Os argumentos em maiúsculas são caminhos JSON, exceto o identificador do procedimento. Operações de comunicação exigem consentimento atual e bloqueio da bancada.

.bancada conserva schema 1/API de execução 2. Declarações antigas de comunicação usam API 1; multicanal usa API 2. Novos recursos exigem BPCB >=0.15. Studio novo usa .bpcb schema 2 e lê projetos antigos schema 1; estes conservam schema 1 enquanto não usam recursos do laboratório. A nova marca evita que um Studio antigo descarte silenciosamente campos novos.

Histórico continua opcional, inicia desativado e não é necessário para PDF/JSON. Parâmetros não são reaplicados por autosave. Firmware/capturas/qualificação são exportados somente por solicitação.

## Fontes primárias

- [Bosch BME280](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf) e [SensorAPI Bosch](https://github.com/boschsensortec/BME280_SensorAPI).
- [TI INA219](https://www.ti.com/lit/ds/symlink/ina219.pdf), [TDK MPU-6000/6050](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf), [Solomon SSD1306](https://www.solomon-systech.com/product/ssd1306).
- [ESP-AT](https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/) e [Modbus serial](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf).
- [Esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html) e [OpenOCD](https://openocd.org/doc/html/).
