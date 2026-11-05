#!/usr/bin/env bash
# Executar manualmente se o diagnóstico apontar falta de permissão. Não acessa a PCB.
set -euo pipefail
bpcb_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
bpcb_rule="$bpcb_root/ambiente/99-bpcb-comunicacao.rules"
bpcb_destination=/etc/udev/rules.d/99-bpcb-comunicacao.rules
if [ -e "$bpcb_destination" ] && ! cmp -s "$bpcb_rule" "$bpcb_destination"; then
    echo "Existe uma regra diferente em $bpcb_destination; confira manualmente. Nenhuma alteração feita." >&2
    exit 1
fi
if [ ! -e "$bpcb_destination" ]; then
    sudo install -m 0644 "$bpcb_rule" "$bpcb_destination"
fi
sudo groupadd -f plugdev
sudo groupadd -f dialout
sudo usermod -aG plugdev,dialout "$(id -un)"
sudo udevadm control --reload-rules
echo 'Encerre a sessão, entre novamente e reconecte os adaptadores.'
echo 'Abra Comunicação da PCB e use Atualizar dispositivos e Conferir acesso.'
echo 'O script não transmite comandos nem troca/desassocia drivers.'
