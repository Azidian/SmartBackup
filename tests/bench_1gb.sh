#!/usr/bin/env bash
# tests/bench_1gb.sh
# ==================
# Benchmark de copia para archivo de 1 GB.
# Este tamaño debe elegir FLAG_KERNEL_LARGE (buffer 64KB).
# ADVERTENCIA: requiere ~2 GB de espacio libre en /tmp.
# Ejecutar desde la raíz del proyecto: bash tests/bench_1gb.sh

set -euo pipefail

BIN="./bin/adaptguard"
mkdir -p ./tmp_bench
SRC="./tmp_bench/adaptguard_bench_1gb_src.bin"
DST="./tmp_bench/adaptguard_bench_1gb_dst.bin"

echo "════════════════════════════════════════"
echo "  Benchmark SmartBackup — archivo 1 GB"
echo "════════════════════════════════════════"
echo "Generando archivo de 1 GB (puede tardar unos segundos)..."

dd if=/dev/urandom of="$SRC" bs=1048576 count=1024 status=progress
echo ""
echo "Tamaño fuente : $(du -h "$SRC" | cut -f1)"

echo ""
echo "── Benchmark comparativo ──"
rm -f "$DST"
$BIN --bench "$SRC" "$DST"

echo ""
echo "Nota: A 1 GB, kernel-space con buffer 64KB debe superar"
echo "      claramente a stdio por menor overhead de syscalls."

# rm -f "$SRC" "$DST"
