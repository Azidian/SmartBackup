/*
 * include/smart_copy.h
 * ====================
 * Contratos globales del sistema AdaptGuard.
 * Este es el ÚNICO archivo que todos los módulos incluyen.
 *
 * Persona 1 — Arquitecto del sistema
 * Proyecto : Smart Backup Kernel-Space Utility
 *
 * REGLA DE EQUIPO: Si modificas constantes o firmas aquí,
 * avisa a P2, P3 y P4 de inmediato — todos dependen de este header.
 */

#ifndef SMART_COPY_H
#define SMART_COPY_H

#include <sys/types.h>   /* off_t, ssize_t                */
#include <sys/stat.h>    /* struct stat, stat()           */
#include <time.h>        /* time_t, struct timespec       */
#include <stdint.h>      /* uint8_t, int64_t             */

/* =========================================================
 * 1. CONSTANTES DE BUFFER Y UMBRALES
 * ========================================================= */

/* Tamaño de página del kernel — unidad base de I/O (4 KB) */
#define BUFFER_SIZE       4096

/* Buffer grande para archivos pesados — 64 KB */
#define BUFFER_LARGE      65536

/*
 * THRESHOLD_SMALL: archivos menores a 1 MB usan capa de usuario.
 * Motivo: fread/fwrite mantienen un buffer interno en espacio de usuario
 * (~8 KB). Las lecturas pequeñas se sirven sin cruzar al kernel,
 * eliminando el costo del context switch (ring3 → ring0 → ring3).
 */
#define THRESHOLD_SMALL   1048576      /* 1 MB  */

/*
 * THRESHOLD_LARGE: archivos mayores a 100 MB usan buffer de 64 KB.
 * Motivo: a este tamaño el número de syscalls domina el tiempo total;
 * un buffer 16× más grande reduce 16× las llamadas read/write.
 */
#define THRESHOLD_LARGE   104857600    /* 100 MB */

/* =========================================================
 * 2. CÓDIGOS DE RETORNO
 * Todos los módulos retornan estos valores. Nunca uses -1 directo.
 * ========================================================= */
#define COPY_OK           0    /* Éxito                               */
#define COPY_SKIP         2    /* Archivo sin cambios — omitido       */
#define COPY_ERROR       -1    /* Error genérico (ver errno)          */
#define COPY_ERR_SRC     -2   /* No se pudo abrir fuente             */
#define COPY_ERR_DST     -3   /* No se pudo abrir/crear destino      */
#define COPY_ERR_READ    -4   /* Fallo de lectura                    */
#define COPY_ERR_WRITE   -5   /* Fallo de escritura (¿disco lleno?)  */
#define COPY_ERR_MEM     -6   /* malloc() falló                      */
#define COPY_ERR_STAT    -7   /* stat() falló sobre el fuente        */

/* =========================================================
 * 3. FLAGS DE ESTRATEGIA
 * choose_strategy() retorna uno de estos. sys_smart_copy() los recibe.
 * ========================================================= */
#define FLAG_SKIP         0    /* No copiar                           */
#define FLAG_USER_SPACE   1    /* Capa de usuario: fread/fwrite       */
#define FLAG_KERNEL_SMALL 2    /* Syscalls, buffer 4 KB               */
#define FLAG_KERNEL_LARGE 3    /* Syscalls, buffer 64 KB              */

/* =========================================================
 * 4. RUTA DEL LOG LOCAL
 * El archivo de log queda dentro del proyecto, visible para el evaluador.
 * syslog() sigue activo en paralelo (registro del sistema operativo).
 * ========================================================= */
#define LOG_FILE_PATH   "./logs/backup.log"

/* Niveles de log propios (no colisionan con syslog.h) */
#define AG_LOG_INFO     0
#define AG_LOG_WARN     1
#define AG_LOG_ERR      2

/*
 * ag_log() — escribe en ./logs/backup.log con timestamp + nivel.
 * Declarada aquí; implementada en backup_engine.c.
 */
void ag_log(int level, const char *src, const char *dst, const char *msg);

/* =========================================================
 * 5. ESTRUCTURAS DE DATOS
 * ========================================================= */

/*
 * FileMetadata — metadatos para el algoritmo de detección de cambios.
 * choose_strategy() la rellena; file_needs_backup() la consulta.
 */
typedef struct {
    off_t   src_size;     /* Bytes del archivo fuente                 */
    off_t   dst_size;     /* Bytes del archivo destino                */
    time_t  src_mtime;    /* Marca de tiempo de modificación, fuente  */
    time_t  dst_mtime;    /* Marca de tiempo de modificación, destino */
    int     dst_exists;   /* 1 si el destino ya existe, 0 si no       */
} FileMetadata;

/*
 * CopyResult — resultado completo de una operación de copia.
 * main.c lo usa para mostrar métricas y mensajes de error.
 */
typedef struct {
    int     status;           /* COPY_OK, COPY_SKIP o código de error */
    int     strategy_used;    /* FLAG_* efectivamente usado           */
    double  elapsed_seconds;  /* Tiempo real de la operación          */
    off_t   bytes_copied;     /* Bytes transferidos                   */
    char    error_msg[256];   /* Descripción si status != COPY_OK     */
} CopyResult;

/* =========================================================
 * 6. FIRMAS DE FUNCIONES PÚBLICAS
 * ========================================================= */

/*
 * choose_strategy()                                 [implementada en src/choose_strategy.c]
 * -----------------------------------------------------------------------------------------
 * Orquesta la decisión completa:
 *   1. stat() del fuente y del destino.
 *   2. Detección de cambios (file_needs_backup).
 *   3. Selección de estrategia por tamaño.
 *
 * Retorna: FLAG_SKIP | FLAG_USER_SPACE | FLAG_KERNEL_SMALL | FLAG_KERNEL_LARGE | COPY_ERROR
 */
int choose_strategy(const char *src, const char *dst, FileMetadata *meta);

/*
 * file_needs_backup()                               [implementada en src/choose_strategy.c]
 * -----------------------------------------------------------------------------------------
 * Algoritmo de detección de cambios.
 * Compara tamaño (detecta si el archivo creció o encogió)
 * y mtime (detecta si fue tocado después del último backup).
 *
 * Retorna: 1 → necesita backup | 0 → sin cambios
 */
int file_needs_backup(const FileMetadata *meta);

/*
 * sys_smart_copy()                                  [implementada en src/backup_engine.c]
 * -----------------------------------------------------------------------------------------
 * Motor de copia kernel-space.
 * Usa open / read / write / close directamente (sin stdio).
 * El tamaño del buffer lo controla el flag (4 KB o 64 KB).
 *
 * Retorna: COPY_OK o código COPY_ERR_* con errno activo.
 */
int sys_smart_copy(const char *src, const char *dst, int flags);

/*
 * user_smart_copy()                                 [implementada en src/backup_engine.c]
 * -----------------------------------------------------------------------------------------
 * Motor de copia user-space (stdio).
 * Más eficiente para archivos < 1 MB por el buffer interno de FILE*.
 *
 * Retorna: COPY_OK o COPY_ERROR con errno activo.
 */
int user_smart_copy(const char *src, const char *dst);

/*
 * log_activity()                                    [implementada en src/backup_engine.c]
 * -----------------------------------------------------------------------------------------
 * Registra en syslog la operación realizada.
 * Cualquier módulo puede llamarla.
 *
 * Parámetros:
 *   level   — LOG_INFO, LOG_ERR, etc. (de <syslog.h>)
 *   src     — ruta fuente (puede ser NULL)
 *   dst     — ruta destino (puede ser NULL)
 *   message — descripción del evento
 */
void log_activity(int level, const char *src, const char *dst,
                  const char *message);

/* =========================================================
 * 7. MACROS DE UTILIDAD
 * ========================================================= */

/*
 * Timer portátil basado en CLOCK_MONOTONIC.
 * Uso en main.c:
 *   struct timespec t0, t1;
 *   TIMER_START(t0);
 *   sys_smart_copy(src, dst, FLAG_KERNEL_SMALL);
 *   TIMER_END(t1);
 *   double secs = TIMER_DIFF_SEC(t0, t1);
 */
#define TIMER_START(ts)       clock_gettime(CLOCK_MONOTONIC, &(ts))
#define TIMER_END(ts)         clock_gettime(CLOCK_MONOTONIC, &(ts))
#define TIMER_DIFF_SEC(a, b)  \
    (((b).tv_sec  - (a).tv_sec) +  \
     ((b).tv_nsec - (a).tv_nsec) / 1e9)

/*
 * DBG — imprime trazas de depuración solo cuando se compila con -DDEBUG.
 * En producción (sin -DDEBUG) se expande a nada.
 */
#ifdef DEBUG
  #include <stdio.h>
  #define DBG(fmt, ...) \
      fprintf(stderr, "[DEBUG %s:%d] " fmt "\n", \
              __FILE__, __LINE__, ##__VA_ARGS__)
#else
  #define DBG(fmt, ...)   /* noop */
#endif

#endif /* SMART_COPY_H */
