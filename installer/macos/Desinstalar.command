#!/bin/bash
# Remove o MagicMIX do Mac (plugins VST3/AU e app).
fim() { echo; read -n 1 -s -r -p "Pressione qualquer tecla para fechar..."; echo; exit "${1:-0}"; }
clear
echo "=============================================="
echo "  Desinstalar MagicMIX"
echo "=============================================="
echo
echo "Isto remove os plugins VST3 e AU e o app."
read -r -p "Continuar? [s/N] " r
case "$r" in s|S) ;; *) echo "Cancelado."; fim 0 ;; esac

echo
echo "O macOS vai pedir a sua senha de administrador."
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/MagicMIX.vst3" \
            "/Library/Audio/Plug-Ins/Components/MagicMIX.component" \
            "/Applications/MagicMIX.app" \
            "/Library/Application Support/MagicMIX" || fim 1
for id in $(pkgutil --pkgs | grep '^com\.davisales\.magicmix'); do
  sudo pkgutil --forget "$id" >/dev/null
done
killall -9 AudioComponentRegistrar 2>/dev/null

USERDIR="$HOME/Library/Application Support/MagicMIX"
if [ -d "$USERDIR" ]; then
  echo
  read -r -p "Apagar também as suas configurações ? [s/N] " l
  if [ "$l" = "s" ] || [ "$l" = "S" ]; then
    rm -rf "$USERDIR"
  else
    echo "Mantido em: $USERDIR"
  fi
fi
echo
echo "Removido."
fim 0
