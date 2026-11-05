#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$project/build}"
sandbox="$(mktemp -d /tmp/bancada-ui-regression-XXXXXX)"
mkdir -p "$sandbox/bin"
cat >"$sandbox/bin/xdg-open" <<'READER'
#!/usr/bin/env sh
printf '%s\n' "$1" >> "$BC_DOCUMENT_LOG"
READER
chmod +x "$sandbox/bin/xdg-open"
export BC_DOCUMENT_LOG="$sandbox/documents.log"
export BANCADA_HOME="$sandbox/state"
export PATH="$sandbox/bin:$PATH"
# Servidor X isolado: nenhum clique na sessao real do operador.
xvfb-run -a -s "-screen 0 1920x1080x24" "$build/bancada_pcb" --ui-check "$build/capturas"
