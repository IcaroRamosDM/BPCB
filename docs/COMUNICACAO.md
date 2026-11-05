# Comunicação da PCB — BPCB 0.13.0

Abra **Ferramentas → Comunicação da PCB**. Escolha modelo e barramento, atualize os dispositivos e selecione a porta/caminho. Confira as ligações, GND e tensão lógica do módulo e da PCB, marque a conferência e clique **Usar seleção**. A escolha vale para esta abertura do BPCB. Uma edição posterior invalida a conferência; reaplique antes de executar. Não há salvamento automático de parâmetros nem ativação automática do histórico.

**Conferir acesso** consulta disponibilidade e permissão, sem enviar comandos I²C/SPI ou configurar GPIO. No caso UART não abre a porta: abrir uma porta pode alterar DTR/RTS. Isso não comprova que a PCB está conectada corretamente. A lista nunca escolhe outro dispositivo para substituir um que desapareceu. Com dois adaptadores iguais, informe caminho ou série; uma seleção ambígua é recusada.

| Seleção | Barramentos | Implementação e limites por operação |
|---|---|---|
| CH341T (`1a86:5512`) | I²C | libusb; 20/100/400 kHz; escrita/leitura até 32 bytes; sem clock stretching declarado |
| FT232H (`0403:6014`) | I²C, SPI, UART | MPSSE por libusb; I²C 1–400 kHz, sem clock stretching; SPI 1 kHz–30 MHz, modos 0–3, CS AD3…AD7 ativo em zero, MSB/LSB, até 4096 bytes; UART pela porta VCP do sistema |
| MCP2221A (`04d8:00dd`) | I²C, UART | HID nativo para I²C, 50–400 kHz, escrita até 60 bytes/leitura até 4096; UART CDC até 460800 baud |
| CP2112 (`10c4:ea90`) | SMBus/I²C | HID nativo; 10–400 kHz, escrita até 61 bytes, prefixo combinado até 16, leitura até 512; timeout SCL de 25 ms habilitado |
| CH340 / CH341 em UART | UART | Driver serial do sistema; não equivale ao CH341T em modo I²C |
| CP210x | UART | Driver serial do sistema |
| FT232R / FT232RL | UART | Driver serial do sistema |
| Waveshare CH347T | I²C, SPI, UART, JTAG | Quatro modos M0/M1/M2/M3, conforme tabela abaixo; detecção por USB e interface; acesso vendor ou HID nativo, sem DLL proprietária |
| Porta serial genérica | UART | Porta COM ou dispositivo de caracteres `/dev`; permite outros bridges com driver serial |

A frequência efetiva pode ser inferior à solicitada por arredondamento dos divisores. O resultado manual informa o clock configurado. O timeout por transação é configurável de 50 a 10000 ms. Chamadas de feature HID ao driver do sistema podem terminar somente após o timeout interno do próprio driver; cancelamento e prazo são conferidos ao retornar, sem iniciar o próximo comando. UART oferece 7/8 bits, paridade nenhuma/par/ímpar, 1/2 bits de parada, fluxo nenhum/RTS+CTS e DTR/RTS. No Linux são aceitas velocidades termios padrão de 300 a 3000000 baud (300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 500000, 576000, 921600, 1000000, 1500000, 2000000 e 3000000); o driver e o módulo podem impor limites menores. USART síncrona, RS-232/RS-485 sem transceptor, SWD e protocolos proprietários não estão incluídos. JTAG básico está disponível no CH347T M3; depuração de CPU e gravação de firmware não fazem parte deste transporte.

## Waveshare CH347T — quatro modos

Selecione **Waveshare CH347T** e o barramento. **Modo CH347** aceita Automático ou M0/M1/M2/M3. Automático filtra interfaces compatíveis; com mais de uma, exige caminho/série. A escolha explícita exige que o dispositivo conectado esteja naquele modo. O modo efetivamente usado entra no resultado/PDF. Trocar o barramento entre UART e outra interface limpa o caminho anterior; importar/editar exige reconferir as conexões.

| Modo físico | USB VID:PID | Funções no BPCB | Acesso |
|---|---|---|---|
| M0 | `1a86:55d9` | UART0 e UART1, cada qual pela sua porta | VCP do sistema; escolha explicitamente COM/tty de cada canal |
| M1 | `1a86:55db` | UART1, I²C, SPI | UART VCP; I²C/SPI na interface USB 2, por libusb |
| M2 | `1a86:55dc` | UART1, I²C, SPI | HID do sistema; interface 0 UART e interface 1 I²C/SPI |
| M3 | `1a86:55dd` | UART1, I²C, JTAG | UART VCP; I²C/JTAG na interface USB 2, por libusb |

Ajuste as chaves de modo **com o USB desconectado** e reconecte. O programa não muda a alimentação, a tensão lógica nem as chaves. Confira o pinout do seu modelo e sua chave 3.3/5 V. O driver é para **CH347T**; não presume que outro produto Waveshare ou o CH347F tenha as mesmas interfaces. A validação física dos quatro modos permanece pendente.

**I²C (M1/M2/M3):** SCL/SDA/GND, com pull-ups compatíveis. Frequências 20, 100, 400 e 750 kHz; até 4096 bytes, reduzidos pelo pacote. ACK do CH347 difere do CH341: o driver usa a resposta própria do CH347, envia repeated START nas leituras combinadas e STOP ao terminar. Clock stretching não é declarado; onde o firmware permite, é desabilitado explicitamente. Streams USB menores não inserem STOP no meio da transação.

**SPI (M1/M2):** SCK, SDO/MOSI, SDI/MISO, CS0/CS1 e GND. Modos 0–3, MSB/LSB, CS ativo em zero; até 4096 bytes. Clock solicitado 468750 Hz…30 MHz, arredondado para baixo entre divisores de 60 MHz. Por exemplo, 1 MHz solicitado resulta em 937500 Hz. A faixa mínima declarada pelo `.bancada` considera esse arredondamento. As divisões em relatórios USB/HID mantêm o mesmo CS ativo até o fim da operação. No M2 cada quadro SPI tem até 507 bytes de dados.

**UART:** VCP em M0/M1/M3, com portas independentes no M0. No M2 selecione a interface HID UART1, mesmo sem porta COM/tty. Oferece 1200…3000000 baud, 8 bits, paridade nenhuma/par/ímpar e 1/2 stops, conforme suporte do sistema. O transporte HID M2 configura o enquadramento pelo feature report e transfere blocos de até 510 bytes por relatório. Fluxo RTS/CTS e linhas DTR/RTS não são configurados via HID nesta versão; os controles são desabilitados e o PDF informa essa condição. Para usar essas linhas pelo BPCB, selecione UART VCP em M0/M1/M3. A escrita aguarda um tempo conservador de serialização, limitado pelo timeout; somente uma resposta do protocolo comprova recebimento pelo alvo.

**JTAG (M3):** TCK, TMS, TDI, TDO e GND. TRST não é acionado. Clock solicitado 1.875…30 MHz, arredondado para um valor disponível no firmware, registrado como configurado, não medido. A abertura inicializa o adaptador e coloca TAP em Run-Test/Idle por seis clocks TMS=1 e um TMS=0. Operações disponíveis: reset TAP, varredura IR, varredura DR e clocks em Idle. Bits são transmitidos LSB primeiro dentro de cada byte; o tamanho pode ser 1…32768 bits, incluindo valores não múltiplos de oito. Bits superiores não utilizados na escrita devem ser zero e são ignorados na comparação. O driver mantém TCK baixo entre comandos e termina varreduras em Run-Test/Idle.

Na janela, escolha **Operação JTAG**, **Bits JTAG** na aba manual e **Clocks Idle**. Para uma instrução seguida de leitura, escolha DR e preencha **IR antes de DR** e **Instrução IR hexadecimal**: ambas as varreduras ocorrerão na mesma sessão e no mesmo prazo. Sem essa opção, cada transferência manual abre uma sessão nova e reinicia TAP. Dentro de um `.bancada`, a sessão é persistente: uma operação IR seguida de DR conserva a instrução. Não há suposição automática do comprimento IR, código IDCODE, cadeia ou comando de programação do alvo.

```json
{"register":"dr","bits":32,"write":"00 00 00 00","ir_bits":5,"ir_write":"09","idle_cycles":1}
```

O exemplo é apenas o formato; **5 bits e 09 não são universais**. Use exclusivamente o manual do seu alvo. `register` pode ser `dr`, `ir`, `reset` ou `idle`; `idle` exige `idle_cycles` de 1…4096. `expect`/`mask` são opcionais e têm exatamente `ceil(bits/8)` bytes. Uma IR opcional é aceita somente antes de DR. Reset/Idle não leem bytes. A falha encerra a operação, sem repetição ou reconexão automática; o driver tenta reset TAP com prazo curto se a conexão ainda permitir.

**Drivers:** M0/M1/M3 UART usa o VCP do fabricante/sistema. Em Linux, M1/M3 I²C/SPI/JTAG usa libusb com permissão; se existir driver associado, a liberação temporária precisa ser autorizada na janela e afeta somente a interface 2. Em Windows configure WinUSB/libusbK **somente para a interface 2**, preservando as interfaces UART do dispositivo composto. M2 usa hidraw/HID nativo e não exige trocar esse driver. O instalador do BPCB não instala nem substitui drivers. As regras udev opcionais incluem os PIDs vendor e HID do CH347T.

Há modelos JSON para todas as combinações suportadas em `examples/communication/waveshare-ch347-m*-*.json`. Para M0 use a porta UART0 ou UART1 que deseja testar; para VCP ajuste `COM3` ao seu sistema. O `.bancada` **Comunicacao-JTAG** faz reset TAP e lê DR de 32 bits sem critério de aprovação. Alguns alvos selecionam IDCODE após reset, outros BYPASS; esse exemplo não identifica nem aprova uma cadeia automaticamente.

Fontes consultadas: [Waveshare — conversor e modos](https://www.waveshare.com/wiki/USB_TO_UART/I2C/SPI/JTAG), [manual de desenvolvimento WCH](https://files.waveshare.com/upload/2/2b/CH347-applicationa-development-manual-en.pdf), [driver publicado pela WCH](https://github.com/WCHSoftGroup/ch34x_mphsi_master_linux/tree/57be7ed01e2adb14d34bcca5e1eb71ff6247aba7/driver), [JTAG da WCH/OpenOCD](https://github.com/WCHSoftGroup/ch347/blob/main/OpenOCD_SourceCode_CH347/src/jtag/drivers/ch347.c) e [CH347-HIDAPI, referência de quadros HID](https://github.com/i2cy/CH347-HIDAPI/tree/be30eef5aa1881d476bb63bdebfdff957655e183). A implementação C do BPCB é independente; não incorpora biblioteca binária do fabricante nem os drivers de kernel dessas referências.

## Ligações e drivers

FT232H I²C: **AD0=SCL, AD1 e AD2 unidos=SDA**, GND comum e pull-ups externos adequados. SPI: AD0=SCK, AD1=MOSI, AD2=MISO, CS conforme seleção. Não una AD1/AD2 em SPI. O BPCB não altera a alimentação nem configura EEPROM do adaptador.

Linux: CH341T/FT232H precisam de acesso libusb; MCP2221A/CP2112 de leitura/escrita em `/dev/hidrawN`; UART normalmente usa o grupo `dialout`. As regras opcionais estão em `ambiente/99-bpcb-comunicacao.rules`. O script `scripts/configurar-comunicacao-linux.sh` instala essa regra e ajusta os grupos somente quando o próprio usuário o executar. Não há instalação automática. Se o driver VCP estiver associado ao FT232H, a opção de liberação temporária deve ser autorizada explicitamente; o BPCB tenta associá-lo novamente ao fechar a sessão.

Windows: CH341T/FT232H em I²C/SPI precisam de driver compatível com libusb (WinUSB/libusbK). MCP2221A e CP2112 usam o HID do Windows. UART usa uma porta COM com o driver VCP/CDC do fabricante. O instalador do BPCB não troca drivers. Trocar FT232H entre VCP/UART e MPSSE pode exigir ajuste manual do driver. Distribuições e `.bancada` são específicos de Linux ou Windows.

## Transferência manual

A aba **Transferência manual** aceita pares hexadecimais, como `01 A0 FF`. O endereço I²C é de **7 bits** (decimal ou `0x48`, faixa 0x08…0x77). Escrita seguida de leitura usa repeated START no mesmo endereço. Em SPI envie um byte por byte recebido, incluindo bytes de preenchimento, com CS mantido ativo durante a transação. UART envia o bloco e aguarda a quantidade exata informada, dentro do timeout; protocolos baseados em delimitadores precisam declarar seu tamanho ou de um executor específico. Antes de encerrar uma escrita, a fila do sistema é drenada com prazo e o enquadramento é mantido durante um tempo conservador de serialização; sem uma resposta do protocolo, bytes aceitos pelo transporte não comprovam recebimento pela PCB.

Resposta esperada e máscara são opcionais. `FF` compara todos os bits; `00` ignora o byte. A comparação exige o mesmo tamanho da leitura. **Transmitir uma vez** mostra uma prévia e exige uma ação explícita. Não há varredura automática de endereços, repetição automática de comandos ou reconexão silenciosa. **Parar** cancela a espera; STOP/CS são liberados quando a conexão ainda permite. Desconexão impede garantir o estado elétrico final.

## Contrato dos pacotes e novos executores

O aplicativo permanece na API de execução 2. A seleção de transporte tem API própria **1**, e pacotes que a utilizam devem declarar `min_app: "0.12.0"` ou superior. Pacotes que declaram CH347/JTAG exigem **0.13.0**; um executor antigo não recebe esse adaptador por seleção genérica. A declaração `communication` pode ficar no manifesto ou no teste; a declaração do teste substitui a do manifesto.

```json
{
  "api": 1,
  "bus": "i2c",
  "adapters": ["ch341t", "ft232h", "mcp2221a", "cp2112", "ch347"],
  "min_hz": 20000,
  "max_hz": 100000,
  "max_transfer": 32,
  "clock_stretching": false,
  "operations": [{"address":72,"write":"00","read":2,"label":"Registro"}]
}
```

O executor **communication_worker** executa essa sequência com uma sessão persistente, interrompe na primeira falha ou comparação divergente e publica os bytes recebidos e os critérios no resultado. Cada operação sem `expect` é uma leitura sem referência, não uma aprovação funcional. O núcleo envia no pedido os campos `communication` (configuração selecionada) e `communication_definition` (declaração do teste). Drivers e executores novos podem usar `core/communication.h`: `bc_comm_open`, `bc_comm_i2c`, `bc_comm_spi`, `bc_comm_uart`, `bc_comm_transaction` e `bc_comm_close`.

A camada está preparada para vários periféricos; o comando, registrador, conversão de unidade e critério de aprovação de cada sensor, memória ou rádio continuam pertencendo ao procedimento da PCB. Incluir um modelo de bridge não cria automaticamente procedimentos para todos os dispositivos conectados a ele.

## Exemplos e linha de comando

A distribuição contém **Comunicacao-I2C.bancada**, **Comunicacao-SPI.bancada** e **Comunicacao-UART.bancada** e **Comunicacao-JTAG.bancada**, cada qual com suas ligações e propósito. I²C é um exemplo de registro a adaptar ao periférico; SPI e UART são loopbacks com a PCB desconectada. Os manifestos editáveis ficam em `packages/communication-*`.

Modelos de configuração JSON em `examples/communication` começam com `connections_checked: false`. Edite caminho, série, parâmetros e, depois da conferência física, mude para `true`. Também é possível exportar/importar uma configuração pela janela. A importação para a interface sempre remove a conferência e a autorização de liberação do driver.

```sh
bancada-core report-record resultado.json relatorio.pdf
bancada-core comm-catalog
bancada-core comm-list
bancada-core comm-probe configuracao.json
bancada-core comm-exchange configuracao.json operacao.json
bancada-core run /caminho/dados communication-uart transferencia configuracao.json
```

O CLI retorna código diferente de zero em erro ou divergência de comparação. SIGINT/SIGTERM cancelam as esperas. No Windows use `bancada-core.exe`; no portátil Linux use `./bpcb-cli`. A seleção, porta, série e enquadramento entram no resultado em memória e no PDF; se o usuário optar por guardar o resultado, também no histórico.

## Referências dos adaptadores

Referências de implementação: [Microchip MCP2221A](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP2221A-Data-Sheet-20005565E.pdf), [Silicon Labs AN495/CP2112](https://www.silabs.com/documents/public/application-notes/an495-cp2112-interface-specification.pdf), [FTDI AN255/MPSSE I²C](https://ftdichip.com/wp-content/uploads/2020/08/AN_255_USB-to-I2C-Example-using-the-FT232H-and-FT201X-devices-1.pdf). A configuração CP2112 limita a uma tentativa do dispositivo, além dos timeouts; o BPCB não reenvia escritas automaticamente.
