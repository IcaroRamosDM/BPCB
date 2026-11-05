# BPCB 0.16.0 — preparação, quadros UART e entrega de firmware

Abra **Laboratório** diretamente na tela inicial ou na lateral de uma bancada.
**Preparar teste…** reúne o procedimento, os parâmetros atuais, a seleção de
comunicação e as conexões. Os botões de configuração e associação retornam à
preparação; executar continua sendo uma ação explícita. A conferência das conexões
vale somente para a execução iniciada naquele diálogo.

## Mapa de conexões

O Studio 0.6 permite cadastrar um mapa por teste. Cada linha identifica conector e
pino da PCB, pino do adaptador, sinal, função, canal e tensões declaradas.
São recusados pinos repetidos, sinais associados a barramentos incompatíveis e
tensões esperadas acima dos limites informados. GND deve declarar zero volts.

Esses valores são declarações do autor. O programa não mede automaticamente
alimentação, polaridade, pinagem ou tolerância elétrica. Confira os dados do módulo
real e da PCB. Não deduza os níveis elétricos apenas pelo nome do chip USB.

## Quadros binários UART

Na aba **Protocolos UART**, escolha **Quadro binário** e **Editar formato binário…**.
O editor permite importar/exportar o formato JSON e decodificar bytes de exemplo
sem abrir uma porta. Configure antes o barramento e os limites da comunicação.

Formato de comprimento fixo, de 1 a 4096 bytes, com 1 a 32 campos. Cada campo tem
identificador, posição em bytes a partir de zero, tipo, ordem dos bytes, escala,
ajuste, unidade e limites opcionais. Tipos: u8/s8, u16/s16, u32/s32 e f32.
O valor físico é bruto × escala + ajuste.

Prefixo e checksum são opcionais. Checksums: soma de 8 bits, XOR de 8 bits ou
CRC-16 Modbus. Informe posição do checksum, início e comprimento da faixa
coberta. O checksum não pode cobrir seus próprios bytes. Prefixo, campos e
checksum não podem se sobrepor.

O exemplo AA 55 C4 09 E4 0C 01 BD representa 25 °C, 3,3 V e estado 1.
Todos os campos com limites participam da avaliação. No Studio, selecione qual
campo alimenta a variável da etapa; as demais leituras continuam na aquisição.
Quadros incompletos, prefixos/checksums divergentes e valores não finitos falham.
Não há repetição automática nem busca automática por sincronismo. Para receber sem enviar comando, deixe a escrita vazia e escolha terminador Nenhum.

## Registro de compatibilidade

Na aba **Qualificação**, avalie o roteiro e abra **Registro de compatibilidade…**.
Informe modelo/revisão, sistema e driver/versão. Salvar é explícito; apenas abrir
ou avaliar não grava um registro.

Estados: sem ensaio, parcial, validado e reprovado. A consulta compara a seleção
normalizada, sistema, driver e roteiro. Mudanças de configuração deixam a nova
seleção sem ensaio; registros feitos com outra versão do aplicativo não são
promovidos automaticamente a validados. Evidências simuladas ou de PTY não
qualificam hardware. Registros são declarações do operador, sem assinatura ou
certificação elétrica automática.

Os registros ficam em compatibility.json, entram no backup de cadastros e podem
ser importados/exportados individualmente. As avaliações são recalculadas ao
importar. Eles não ativam o histórico de resultados nem reaplicam consentimentos.
O histórico continua opcional e inicia desligado.

## Pacote portátil de firmware

Na aba **Firmware**, use **Exportar pacote completo…** para gerar .bpcbfirmware.
Ele reúne manifesto, versão, família/alvo, imagens, endereços e SHA-256.
As imagens de origem não precisam acompanhar o pacote.

**Importar pacote completo…** inspeciona uma cópia privada do arquivo e apresenta
alvo, imagens e hash antes da extração. Escolha uma pasta existente e o nome de
um novo diretório. Destinos existentes não são substituídos. A importação preserva
firmware.json com caminhos relativos, além de images/.

Limites: manifesto até 64 KiB, até 16 imagens, 16 MiB de imagens e arquivo até
17 MiB. O formato usa ZIP padrão, manifesto primeiro e nomes canônicos gerados;
ZIP64, volumes múltiplos, vínculos, criptografia, entradas extras/repetidas,
caminhos ambíguos e divergências entre cabeçalhos ou hashes são recusados.
Leitura/cópia usa blocos de 64 KiB.

STM32: BIN com endereço explícito, HEX ou ELF compatível. ESP32: BIN com endereço
de setor. FPGA: SVF/XSVF. As restrições do planejador de firmware continuam
vigentes. Ferramentas de gravação, drivers e scripts não são incluídos nem
considerados confiáveis pelo pacote. Após importar, prepare uma nova prévia;
consultar/verificar/gravar continua dependendo da revisão e confirmação do
operador. A importação nunca inicia uma gravação.

## Versões de arquivo

.bancada conserva schema 1 e API de execução 2. Mapa estruturado ou quadro UART
exige BPCB 0.16 ou superior. Projetos Studio novos usam schema 3; schemas 1 e 2
continuam legíveis e os antigos conservam sua marca enquanto não adotam recursos
novos.
