# Comunicação avançada — BPCB 0.14.0

Abra **Ferramentas → Comunicação da PCB → Terminal e exploradores**.
Escolha primeiro o adaptador, barramento, interface/mode CH347 e parâmetros.
Confira as conexões novamente depois de carregar ou alterar uma seleção.
Nenhuma ferramenta transmite ao abrir uma janela ou importar um perfil.

## Terminal UART

Selecione UART e a porta exata (COM no Windows, /dev no Linux; HID no CH347 M2).
Escolha ASCII ou hexadecimal, delimitador nenhum/LF/CR/CRLF e duração 1–3600 s.
**Abrir terminal** mantém uma sessão, recebe continuamente e permite **Enviar uma vez**.
O delimitador vale para o envio e para separar a recepção, inclusive entre pacotes USB.
ASCII mostra caracteres de controle escapados (\r, \n, \x00); bytes não ASCII devem
ser enviados em hexadecimal. Não há reconexão nem repetição automática de escrita.

**Parar operação**, fechar o terminal ou encerrar o programa cancela a sessão.
Silêncio é permitido; desconexão é erro. Quadros de até 4096 bytes são preservados;
um quadro maior ou sem delimitador é mostrado em fragmentos. Ao parar, o fragmento
já recebido é mostrado como incompleto. Abertura/configuração de UART pode alterar
DTR/RTS, conforme o adaptador. CH347 M2 continua sem configuração de linhas de modem.

**Exportar captura** é opcional. Até 4096 eventos / 4 MiB ficam em memória, com TX/RX,
hex, ASCII, tempo e indicação de fragmento. Eventos antigos removidos são contados
em `dropped_events`; o texto visível tem um limite menor. Nenhum arquivo de captura
é criado automaticamente. O histórico dos ensaios continua desligado por padrão.

Bluetooth com firmware AT e ESP-AT são exemplos de protocolos sobre UART. `AT` e
CRLF só são apropriados se o manual do módulo indicar esses comandos, modo e baud.
Wi-Fi não determina o transporte: módulos podem usar UART, SPI ou interfaces próprias.

## Perfis nomeados

**Salvar seleção** grava explicitamente um nome em `communication-profiles.json`,
na pasta de dados escolhida para o BPCB. Até 64 nomes, com 1–80 bytes; substituição
e exclusão exigem ação explícita. **Carregar para edição** não conecta nem ativa o
perfil: confira as conexões e clique **Usar seleção** na janela de comunicação.
A confirmação de conexões e a permissão para liberar o driver são sempre renovadas.
Parâmetros dos testes não são salvos/reaplicados por este recurso.

Backup/restauração de cadastros preservam os perfis, inclusive em um cadastro sem
PCB instalada. Um backup com estes perfis requer BPCB 0.14 ou posterior para restaurar.
A escrita é atômica, com bloqueio de armazenamento; arquivos de perfil simbólicos
não são seguidos. Backups não ativam histórico nem autorizam operações no hardware.

## Explorador I²C

A varredura é **manual**, limita endereços a 0x08–0x77 e lê **um byte** por endereço.
Não é uma sonda sem efeitos: ler pode consumir dados, limpar flags ou alterar o
ponteiro de um dispositivo. Escolha um intervalo apropriado para sua PCB.
ACK indica resposta ao acesso; não identifica componente, tensão ou pinagem.
NACK é exibido separadamente de falha de USB, timeout e cancelamento.
Após NACK, MCP2221A/CP2112 podem precisar de uma sessão nova no **mesmo dispositivo**
para o endereço seguinte. O endereço que falhou não é repetido. Erro de transporte
interrompe a varredura; limite global 120 s. Resultados parciais aparecem no painel.

Mapas iniciais: TCA9534A, ADS7924, BME280/BMP280, MPU-6050, INA219, MCP4728 e SSD1306.
As leituras de sensores são dados brutos, sem inicialização/compensação automática.
SSD1306 I²C é uma interface de comandos/escrita: o mapa oferece o byte de controle
00 seguido de AE/AF, sem tentar ler registradores inexistentes. Inicialização e
geometria do display continuam dependentes do módulo.

Importe mapas JSON, em memória, com `schema:1`, `name`, `registers` (1–256 entradas).
Cada entrada contém `name`, `register`, `register_bytes` (0/1/2, prefixo big endian),
`width` (1–32) e `access` (`ro`, `rw`, `wo`). `command` pode fornecer um prefixo hex.
`default_address` é uma sugestão editável de 7 bits. Escritas respeitam o acesso e o
comprimento do mapa e apresentam prévia; não há leitura periódica nem escrita implícita.
Exemplos dos mapas podem ser obtidos com `bpcb-cli comm-register-maps`.

## JTAG e OpenOCD

**IDCODE/BYPASS (CH347)** reinicia TAP e interpreta até 32 TAPs, incluindo BYPASS.
Exibe ID, fabricante, parte e revisão quando há IDCODE. Não descobre comprimento IR
nem garante modelo da placa; TDO fixo alto/baixo e cadeias truncadas são inconclusivos.
CH347T precisa estar em M3. Este recurso nativo não depende de OpenOCD.

A integração OpenOCD é opcional e externa: escolha o executável e sua pasta `scripts`
de uma instalação confiável. **Conferir driver** mostra se essa instalação inclui o driver `ch347` sem abrir a conexão USB. Use uma compilação oficial
compatível com CH347; para ESP32, também são necessários os drivers/scripts de flash
compatíveis da Espressif. O BPCB não instala drivers nem troca firmware do adaptador.

Perfis iniciais: STM32 F1/F2/F3/F4/F7/H7/L1/L4/G4; ESP32/S2/S3/C3/C6/H2; FPGA
Xilinx Spartan-6/7-series/UltraScale. Selecione a família exata. A presença no menu
não prova que a instalação OpenOCD escolhida tenha aquele alvo ou consiga gravá-lo.
Outras famílias podem ser importadas como JSON de dados:
`{"id":"meu-alvo","family":"fpga","script":"cpld/meu-alvo.cfg"}`.
Só são aceitas famílias stm32/esp32/fpga e caminhos relativos em `target/` ou `cpld/`,
sem travessia de diretórios ou comandos Tcl fornecidos pelo perfil. Os scripts locais
são programas executáveis; a confirmação de origem confiável é obrigatória.

- STM32: ELF/HEX carregam endereços; BIN exige endereço explícito do projeto de firmware.
- ESP32: BIN com offset explícito; gravação usa `program_esp` com verificação. A consulta
  independente usa `esp verify_bank_hash`, disponível nas versões compatíveis da Espressif.
  Não se adivinha offset de bootloader/aplicação nem se gravam vários arquivos implicitamente.
- FPGA: SVF/XSVF são vetores fornecidos pela ferramenta do fabricante. Podem programar
  o dispositivo e executar operações permanentes, conforme os vetores. Revise sua origem; as verificações TDO dependem do arquivo. Não há uma verificação genérica
  independente para um arquivo arbitrário de bitstream (.bit/.sof não são aceitos diretamente).

**Preparar prévia** apresenta alvo, ação, argumentos e SHA-256. **Executar** exige
prévia atual e confirmação. Gravação exige o hash da imagem revisada. O programa fixa
um único CH347T M3 por série/localização USB, copia a imagem para um arquivo privado,
confere novamente seu hash e executa argumentos sem shell. Servidores GDB/Tcl/Telnet
ficam desativados; log limitado a 64 KiB e execução a 120 s na interface (CLI: 1–600 s).
Cancelamento encerra o grupo do processo; não desfaz apagamento/gravação parcial.
Falha de verificação não é aprovação. O código de saída e o log ficam no painel.

## Compatibilidade com o Studio

O Studio 0.4.0 publica operações e limites no .bancada e exige BPCB 0.14 para estes
projetos. Projetos antigos continuam com o comportamento legado; a seleção CH341
agora rejeita múltiplos dispositivos.
Veja o guia `COMUNICACAO.md` do Studio para autoria, simulação e exemplos IoT.

## Fontes técnicas

- https://www.openocd.org/doc/html/Debug-Adapter-Configuration.html
- https://www.openocd.org/doc/html/Flash-Programming.html
- https://www.openocd.org/doc/html/Boundary-Scan-Commands.html
- https://github.com/espressif/openocd-esp32/blob/master/tcl/target/esp_common.cfg
- https://www.waveshare.com/wiki/USB_TO_UART/I2C/SPI/JTAG
- https://www.ti.com/lit/ds/symlink/tca9534a.pdf
- https://www.ti.com/lit/gpn/ads7924
- https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
- https://www.ti.com/lit/ds/symlink/ina219.pdf
- https://www.microchip.com/en-us/product/MCP4728
- https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/
- https://www.solomon-systech.com/product/ssd1306/
