/*
 * src/choose_strategy.c
 * =====================
 * Lógica de decisión del sistema de backup inteligente.
 *
 * Persona 1 — Arquitecto del sistema
 *
 * Responsabilidades:
 *  - file_needs_backup() : detecta si el archivo cambió
 *  - choose_strategy()   : decide qué capa de copia usar
 *
 * Algoritmo de detección de cambios (por qué tamaño Y mtime):
 *  El mtime puede ser alterado por herramientas externas (tar, rsync, touch).
 *  El tamaño es más difícil de falsificar y detecta cambios reales
 *  en el contenido binario. Combinar ambos cubre los casos comunes.
 *
 * Política de selección de estrategia:
 *  tamaño < 1 MB   → FLAG_USER_SPACE   (stdio evita context switches)
 *  tamaño < 100 MB → FLAG_KERNEL_SMALL (syscalls, buffer de página 4 KB)
 *  tamaño >= 100MB → FLAG_KERNEL_LARGE (syscalls, buffer 64 KB)
 */

#include "../include/smart_copy.h"

#include <string.h>     /* strerror(), memset()  */
#include <errno.h>      /* errno                 */
#include <syslog.h>     /* syslog(), LOG_ERR     */

/* =========================================================
 * file_needs_backup()
 * -------------------
 * Reglas en orden de prioridad:
 *   1. Destino no existe          → copiar siempre (archivo nuevo)
 *   2. Tamaños distintos          → copiar (contenido cambió)
 *   3. mtime fuente > mtime dest  → copiar (modificado más tarde)
 *   4. Todo igual                 → omitir
 * ========================================================= */
int file_needs_backup(const FileMetadata *meta) {
    if (meta == NULL) {
        return 1;   /* Sin información: copiar por seguridad */
    }

    /* Regla 1: primer backup de este archivo */
    if (!meta->dst_exists) {
        DBG("Destino no existe → backup necesario");
        return 1;
    }

    /*
     * Regla 2: tamaños distintos.
     * Cubre tanto archivos que crecieron (se añadió contenido)
     * como archivos que encogieron (contenido fue recortado o reemplazado).
     * Un archivo de 1.2 MB que ahora mide 1.1 MB fue modificado.
     */
    if (meta->src_size != meta->dst_size) {
        DBG("Tamaño difiere: src=%lld dst=%lld → backup necesario",
            (long long)meta->src_size, (long long)meta->dst_size);
        return 1;
    }

    /*
     * Regla 3: el fuente es más nuevo que el destino.
     * Si los tamaños son iguales pero mtime cambió, el archivo
     * fue sobreescrito con contenido del mismo largo (p. ej. editar
     * un campo de longitud fija en una base de datos binaria).
     */
    if (meta->src_mtime > meta->dst_mtime) {
        DBG("mtime fuente más reciente → backup necesario");
        return 1;
    }

    /* Regla 4: sin cambios detectados */
    DBG("Archivo sin cambios → omitiendo copia");
    return 0;
}

/* =========================================================
 * choose_strategy()
 * -----------------
 * Pasos:
 *   1. stat() del archivo fuente  (obligatorio; error si falla)
 *   2. stat() del archivo destino (opcional; puede no existir)
 *   3. Llama a file_needs_backup() para decidir si copiar
 *   4. Si hay que copiar, selecciona la estrategia por tamaño
 *
 * Por qué fread/fwrite gana en archivos pequeños:
 *   FILE* tiene un buffer interno de ~8 KB en espacio de usuario.
 *   Múltiples llamadas a fread() se sirven desde ese buffer sin
 *   invocar al kernel (sin context switch). Para archivos pequeños,
 *   el costo de cruzar la frontera ring3↔ring0 puede superar el
 *   tiempo real de copiar los datos.
 *
 * Por qué syscalls ganan en archivos grandes:
 *   Con archivos de cientos de MB, el buffer de stdio se recarga
 *   tantas veces como las syscalls directas. Usamos read/write
 *   porque controlamos exactamente el tamaño del buffer (alineado
 *   con el page cache del kernel: 4 KB) y eliminamos la capa
 *   intermedia de stdio, reduciendo copias de memoria.
 * ========================================================= */
int choose_strategy(const char *src, const char *dst, FileMetadata *meta) {
    struct stat src_stat;
    struct stat dst_stat;

    /* Validación de parámetros */
    if (src == NULL || dst == NULL || meta == NULL) {
        syslog(LOG_ERR, "choose_strategy: parámetros NULL");
        return COPY_ERROR;
    }

    memset(meta, 0, sizeof(FileMetadata));

    /* ── Paso 1: stat() del fuente ── */
    if (stat(src, &src_stat) == -1) {
        /*
         * Puede fallar con:
         *   ENOENT  → el archivo no existe
         *   EACCES  → sin permiso de búsqueda en algún directorio padre
         *   ENOTDIR → un componente de la ruta no es directorio
         */
        syslog(LOG_ERR, "choose_strategy: stat(%s) falló: %s",
               src, strerror(errno));
        return COPY_ERROR;
    }

    meta->src_size  = src_stat.st_size;
    meta->src_mtime = src_stat.st_mtime;

    /* ── Paso 2: stat() del destino (puede no existir) ── */
    if (stat(dst, &dst_stat) == 0) {
        meta->dst_exists = 1;
        meta->dst_size   = dst_stat.st_size;
        meta->dst_mtime  = dst_stat.st_mtime;
        DBG("Destino '%s' existe (size=%lld, mtime=%ld)",
            dst, (long long)dst_stat.st_size, (long)dst_stat.st_mtime);
    } else {
        /* ENOENT es normal aquí: primer backup de este archivo */
        meta->dst_exists = 0;
        meta->dst_size   = 0;
        meta->dst_mtime  = 0;
        DBG("Destino '%s' no existe (primer backup)", dst);
    }

    /* ── Paso 3: ¿Necesita backup? ── */
    if (!file_needs_backup(meta)) {
        return FLAG_SKIP;
    }

    /* ── Paso 4: Selección de estrategia ── */
    if (meta->src_size < THRESHOLD_SMALL) {
        return FLAG_USER_SPACE;    /* < 1 MB  → stdio        */
    }
    if (meta->src_size < THRESHOLD_LARGE) {
        return FLAG_KERNEL_SMALL;  /* < 100 MB → syscalls 4 KB */
    }
    return FLAG_KERNEL_LARGE;      /* >= 100 MB → syscalls 64 KB */
}
