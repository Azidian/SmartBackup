#!/usr/bin/env bash
# tests/bench_1kb.sh
# ==================
# Benchmark de copia para archivo de 1 KB.
# Este tamaño debe elegir FLAG_USER_SPACE (< 1 MB).
# Ejecutar desde la raíz del proyecto: bash tests/bench_1kb.sh

set -euo pipefail

BIN="./bin/adaptguard"
mkdir -p ./tmp_bench  # Crea la carpeta local si no existe
SRC="./tmp_bench/adaptguard_bench_1kb_src.bin"
DST="./tmp_bench/adaptguard_bench_1kb_dst.bin"
REPS=10   # Repeticiones para promediar

echo "════════════════════════════════════════"
echo "  Benchmark SmartBackup — archivo 1 KB"
echo "════════════════════════════════════════"

# Crear archivo de prueba con datos aleatorios
dd if=/dev/urandom of="$SRC" bs=1024 count=1 status=none
echo "Tamaño fuente : $(du -h "$SRC" | cut -f1)"

echo ""
echo "── Modo automático ($REPS repeticiones) ──"
TOTAL=0
for i in $(seq 1 $REPS); do
    # Borrar destino para forzar copia real cada vez
    rm -f "$DST"
    $BIN "$SRC" "$DST"
done

echo ""
echo "── Benchmark comparativo (kernel-space vs user-space) ──"
rm -f "$DST"
$BIN --bench "$SRC" "$DST"

echo ""
echo "Nota: Para archivos < 1 MB se espera que user-space sea"
echo "      más rápido (evita context switches con buffer interno)."

# Limpieza
# rm -f "$SRC" "$DST"
