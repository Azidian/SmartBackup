#!/usr/bin/env bash
# tests/bench_1mb.sh
# ==================
# Benchmark de copia para archivo de 1 MB.
# Este tamaño debe elegir FLAG_KERNEL_SMALL (buffer 4KB).
# Ejecutar desde la raíz del proyecto: bash tests/bench_1mb.sh

set -euo pipefail

BIN="./bin/adaptguard"
mkdir -p ./tmp_bench
SRC="./tmp_bench/adaptguard_bench_1mb_src.bin"
DST="./tmp_bench/adaptguard_bench_1mb_dst.bin"

echo "════════════════════════════════════════"
echo "  Benchmark SmartBackup — archivo 1 MB"
echo "════════════════════════════════════════"

dd if=/dev/urandom of="$SRC" bs=1048576 count=1 status=none
echo "Tamaño fuente : $(du -h "$SRC" | cut -f1)"

echo ""
echo "── Benchmark comparativo ──"
rm -f "$DST"
$BIN --bench "$SRC" "$DST"

echo ""
echo "Nota: A 1 MB, syscalls con buffer 4KB y stdio"
echo "      deberían tener rendimiento muy similar."

# rm -f "$SRC" "$DST"
