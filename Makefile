# ===========================================================
# Makefile — Smart Backup Kernel-Space Utility (AdaptGuard)
# ===========================================================
# Uso:
#   make            → compila en modo release
#   make debug      → compila con -DDEBUG y -g
#   make clean      → elimina binarios y objetos
#   make test       → compila y ejecuta prueba rápida
#
# Salida: bin/adaptguard

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude -D_POSIX_C_SOURCE=199309L
DBGFLAGS = -DDEBUG -g -O0
RELFLAGS = -O2
LDFLAGS  = -lrt

SRCS    = src/choose_strategy.c \
          src/backup_engine.c   \
          src/main.c  \
		  src/menu.c

OBJS    = $(SRCS:.c=.o)
TARGET  = bin/adaptguard # Aquí se nomba el ejecutable final

# ── Regla por defecto ──
all: bin $(TARGET)

bin:
	mkdir -p bin

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(RELFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Compilado: $@"

%.o: %.c
	$(CC) $(CFLAGS) $(RELFLAGS) -c $< -o $@

# ── Modo debug ──
debug: CFLAGS += $(DBGFLAGS)
debug: bin $(TARGET)

# ── Limpieza ──
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "✓ Todos los archivos generados han sido eliminados. :)"

# ── Prueba rápida ──
test: all
	@echo "── Prueba de backup (archivo pequeño) ──"
	echo "Archivo de prueba SmartBackup" > /tmp/smartBackup_test_src.txt
	./$(TARGET) /tmp/smartBackup_test_src.txt /tmp/smartBackup_test_dst.txt
	@echo ""
	@echo "── Segunda ejecución (sin cambios, debe omitir) ──"
	./$(TARGET) /tmp/smartBackup_test_src.txt /tmp/smartBackup_test_dst.txt
	@echo ""
	@echo "── Benchmark de ambas capas ──"
	./$(TARGET) --bench /tmp/smartBackup_test_src.txt /tmp/smartBackup_test_dst2.txt

.PHONY: all debug clean test
