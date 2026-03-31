/* src/backup_engine.c
 * ===================
 * Motor de copia del sistema AdaptGuard.
 *
 * Simula el comportamiento de una capa de kernel:
 * usa syscalls directas (open/read/write/close) sin pasar por stdio.h,
 * valida permisos explícitamente con access() antes de abrir,
 * registra toda actividad en DOS canales simultáneos:
 *   1. ./logs/backup.log  — log local visible en el proyecto
 *   2. syslog del SO      — log del sistema (journalctl / /var/log/syslog)
 *
 * Persona 2 — Motor del kernel
 *
 * Funciones públicas:
 *   ag_log()          — escribe en ./logs/backup.log
 *   log_activity()    — escribe en syslog del sistema
 *   sys_smart_copy()  — copia kernel-space, buffer dinámico 4KB/64KB
 *   user_smart_copy() — copia user-space con fread/fwrite (stdio)
 */

#include "../include/smart_copy.h"

/* ── Syscalls POSIX (capa kernel) ── */
#include <fcntl.h>      /* open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h>     /* read(), write(), close(), access()           */
#include <stdlib.h>     /* malloc(), free()                             */
#include <string.h>     /* strerror(), memset()                        */
#include <errno.h>      /* errno, ENOENT, EACCES, ENOSPC              */
#include <syslog.h>     /* openlog(), syslog(), closelog()             */
#include <sys/stat.h>   /* stat(), struct stat                         */
#include <time.h>       /* time(), localtime(), strftime()             */

/* stdio — SOLO para user_smart_copy y ag_log (escritura de log) */
#include <stdio.h>

/* =========================================================
 * ag_log()
 * --------
 * Log local visible en el proyecto: ./logs/backup.log
 *
 * Formato de cada línea:
 *   [2026-04-01 14:32:05] [INFO ] src=/ruta/a.txt dst=/backup/a.txt  Copia OK 4KB
 *   [2026-04-01 14:32:05] [ERROR] src=/ruta/b.txt  EACCES: permiso denegado
 *
 * Por qué dos canales (ag_log + syslog):
 *   - ag_log  → el evaluador del proyecto puede leer logs/backup.log directamente.
 *   - syslog  → integración real con el sistema operativo (journalctl, /var/log).
 *   Ambos registros son complementarios, no redundantes.
 * ========================================================= */
void ag_log(int level, const char *src, const char *dst, const char *msg) {
    /* Abrir en modo append — cada línea se agrega al final */
    FILE *lf = fopen(LOG_FILE_PATH, "a");
    if (lf == NULL) return;   /* Si no se puede escribir, continuar sin abortar */

    /* Timestamp legible: YYYY-MM-DD HH:MM:SS */
    time_t     now     = time(NULL);
    struct tm *tm_info = localtime(&now);
    char       ts[24];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Etiqueta de nivel */
    const char *lvl_str;
    switch (level) {
        case AG_LOG_INFO: lvl_str = "INFO "; break;
        case AG_LOG_WARN: lvl_str = "WARN "; break;
        case AG_LOG_ERR:  lvl_str = "ERROR"; break;
        default:          lvl_str = "?????"; break;
    }

    /* Línea con rutas opcionales */
    if (src != NULL && dst != NULL) {
        fprintf(lf, "[%s] [%s] src=%-35s dst=%-35s  %s\n",
                ts, lvl_str, src, dst, msg);
    } else if (src != NULL) {
        fprintf(lf, "[%s] [%s] src=%-35s                                       %s\n",
                ts, lvl_str, src, msg);
    } else if (dst != NULL) {
        fprintf(lf, "[%s] [%s]                                      dst=%-35s  %s\n",
                ts, lvl_str, dst, msg);
    } else {
        fprintf(lf, "[%s] [%s] %s\n", ts, lvl_str, msg);
    }

    fclose(lf);
}

/* =========================================================
 * log_activity()
 * --------------
 * Canal 2: syslog del sistema operativo.
 * Ver con: journalctl -t AdaptGuard   o   grep AdaptGuard /var/log/syslog
 * ========================================================= */
void log_activity(int level, const char *src, const char *dst,
                  const char *message) {
    openlog("AdaptGuard", LOG_PID, LOG_USER);

    if (src != NULL && dst != NULL)
        syslog(level, "%s | src=%s -> dst=%s", message, src, dst);
    else if (src != NULL)
        syslog(level, "%s | src=%s", message, src);
    else if (dst != NULL)
        syslog(level, "%s | dst=%s", message, dst);
    else
        syslog(level, "%s", message);

    closelog();
}

/*
 * log_both() — macro interna que escribe en los dos canales a la vez.
 * Evita repetir la llamada doble en cada punto del código.
 */
#define log_both(ag_level, syslog_level, src, dst, msg) \
    do { \
        ag_log((ag_level), (src), (dst), (msg)); \
        log_activity((syslog_level), (src), (dst), (msg)); \
    } while (0)

/* =========================================================
 * validate_src_permissions()
 * --------------------------
 * Valida ANTES de open() que el archivo fuente:
 *   - Existe (ENOENT)
 *   - Tiene permiso de lectura para el proceso (EACCES)
 *
 * Por qué hacerlo con access() y no dejar que open() falle:
 *   access() distingue si el problema es existencia o permisos,
 *   lo que permite dar un mensaje de error preciso al usuario
 *   y al log antes de intentar abrir el descriptor.
 *
 * Retorna: COPY_OK | COPY_ERR_SRC (errno activo)
 * ========================================================= */
static int validate_src_permissions(const char *src) {
    char msg[256];

    /* ¿Existe el archivo? */
    if (access(src, F_OK) == -1) {
        /* errno == ENOENT: no existe */
        snprintf(msg, sizeof(msg), "ENOENT: el archivo no existe (%s)", src);
        log_both(AG_LOG_ERR, LOG_ERR, src, NULL, msg);
        errno = ENOENT;
        return COPY_ERR_SRC;
    }

    /* ¿Tiene permiso de lectura? */
    if (access(src, R_OK) == -1) {
        /* errno == EACCES: sin permiso de lectura */
        snprintf(msg, sizeof(msg), "EACCES: sin permiso de lectura (%s)", src);
        log_both(AG_LOG_ERR, LOG_ERR, src, NULL, msg);
        errno = EACCES;
        return COPY_ERR_SRC;
    }

    return COPY_OK;
}

/* =========================================================
 * validate_dst_permissions()
 * --------------------------
 * Valida que el directorio padre del destino existe y que
 * el proceso tiene permiso de escritura.
 *
 * Si el archivo destino ya existe, verifica permiso W_OK.
 * Si no existe, verifica W_OK en el directorio padre.
 *
 * Retorna: COPY_OK | COPY_ERR_DST (errno activo)
 * ========================================================= */
static int validate_dst_permissions(const char *dst) {
    char msg[256];

    /* Caso 1: el destino ya existe → verificar permiso de escritura */
    if (access(dst, F_OK) == 0) {
        if (access(dst, W_OK) == -1) {
            snprintf(msg, sizeof(msg),
                     "EACCES: destino existe pero sin permiso de escritura (%s)", dst);
            log_both(AG_LOG_ERR, LOG_ERR, NULL, dst, msg);
            errno = EACCES;
            return COPY_ERR_DST;
        }
        return COPY_OK;
    }

    /*
     * Caso 2: el destino no existe → verificar permiso W_OK
     * en el directorio padre.
     * Extraemos el directorio padre copiando dst y cortando en el
     * último '/'. Si no hay '/', el padre es "." (directorio actual).
     */
    char parent[512];
    strncpy(parent, dst, sizeof(parent) - 1);
    parent[sizeof(parent)-1] = '\0';

    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';               /* Recortar al directorio padre    */
        if (parent[0] == '\0')
            strcpy(parent, "/");     /* dst era "/archivo" → padre "/"  */
    } else {
        strcpy(parent, ".");         /* Sin '/' → padre es cwd          */
    }

    if (access(parent, W_OK) == -1) {
        snprintf(msg, sizeof(msg),
                 "EACCES: sin permiso de escritura en directorio destino (%s)", parent);
        log_both(AG_LOG_ERR, LOG_ERR, NULL, dst, msg);
        errno = EACCES;
        return COPY_ERR_DST;
    }

    return COPY_OK;
}

/* =========================================================
 * sys_smart_copy()
 * ----------------
 * Motor de copia en capa de kernel.
 * Usa open/read/write/close directamente, sin stdio.
 *
 * Flujo:
 *   0. Validar permisos (access) antes de abrir
 *   1. Abrir src  con O_RDONLY
 *   2. Crear dst  con O_WRONLY | O_CREAT | O_TRUNC, permisos 0644
 *   3. malloc() buffer dinámico (4 KB o 64 KB según flag)
 *   4. Loop read→write hasta EOF; detectar ENOSPC en write
 *   5. close() SIEMPRE ambos descriptores (incluso en error)
 *   6. free() buffer; puntero a NULL para evitar dangling pointer
 *
 * Errores manejados con errno:
 *   ENOENT  — archivo fuente no existe
 *   EACCES  — sin permiso de lectura o escritura
 *   ENOSPC  — disco lleno durante write()
 *   EMFILE  — tabla de descriptores del proceso llena
 *   ENOMEM  — malloc falló (sin memoria disponible)
 * ========================================================= */
int sys_smart_copy(const char *src, const char *dst, int flags) {
    int     fd_src      = -1;
    int     fd_dst      = -1;
    char   *buffer      = NULL;   /* Puntero explícito — se libera en §6 */
    size_t  buf_size;
    ssize_t bytes_read;
    ssize_t bytes_written;
    int     return_code = COPY_OK;
    char    log_msg[256];

    /* ── §0: Guardia de punteros NULL ── */
    if (src == NULL || dst == NULL) {
        ag_log(AG_LOG_ERR, NULL, NULL,
               "sys_smart_copy: puntero src o dst es NULL — abortar");
        return COPY_ERROR;
    }

    /* ── §0b: Validar permisos antes de abrir descriptores ── */
    return_code = validate_src_permissions(src);
    if (return_code != COPY_OK) return return_code;

    return_code = validate_dst_permissions(dst);
    if (return_code != COPY_OK) return return_code;
    return_code = COPY_OK;   /* Reiniciar para el loop de copia */

    /* ── §1: Tamaño de buffer según estrategia ── */
    buf_size = (flags == FLAG_KERNEL_LARGE) ? BUFFER_LARGE : BUFFER_SIZE;

    snprintf(log_msg, sizeof(log_msg),
             "INICIO copia kernel-space | buffer=%zu B | flag=%d",
             buf_size, flags);
    log_both(AG_LOG_INFO, LOG_INFO, src, dst, log_msg);

    /* ── §2: Abrir fuente ── */
    fd_src = open(src, O_RDONLY);
    if (fd_src == -1) {
        snprintf(log_msg, sizeof(log_msg),
                 "open(src) falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        return COPY_ERR_SRC;
    }

    /* ── §3: Crear/truncar destino ── */
    /*
     * 0644 = rw-r--r-- : propietario puede leer/escribir,
     *                    grupo y otros solo leer.
     *                    Permisos estándar para archivos de datos.
     */
    fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst == -1) {
        snprintf(log_msg, sizeof(log_msg),
                 "open(dst) falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        close(fd_src);   /* Liberar fd ya abierto antes de salir */
        return COPY_ERR_DST;
    }

    /* ── §4: Asignar buffer dinámico en el heap ── */
    /*
     * Se usa malloc (heap) y no un array en el stack porque:
     *   - 64 KB en el stack puede desbordarlo en sistemas con límite bajo.
     *   - El puntero permite verificar fallo de asignación con NULL.
     *   - free() al final garantiza que no haya fuga de memoria.
     */
    buffer = (char *)malloc(buf_size);
    if (buffer == NULL) {
        log_both(AG_LOG_ERR, LOG_ERR, src, dst,
                 "malloc() falló: sin memoria disponible (ENOMEM)");
        close(fd_src);
        close(fd_dst);
        return COPY_ERR_MEM;
    }

    /* ── §5: Loop principal read → write ── */
    /*
     * read() retorna:
     *   > 0   bytes leídos (puede ser < buf_size en el último bloque)
     *   = 0   EOF — fin del archivo, copia completa
     *   = -1  error de lectura (errno activo)
     *
     * write() retorna:
     *   >= 0  bytes escritos (puede ser < bytes_read si disco casi lleno)
     *   = -1  ENOSPC (disco lleno) u otro error de escritura
     *
     * Verificamos que bytes_written == bytes_read para detectar
     * escrituras parciales silenciosas (disco casi lleno).
     */
    while ((bytes_read = read(fd_src, buffer, buf_size)) > 0) {

        bytes_written = write(fd_dst, buffer, (size_t)bytes_read);

        if (bytes_written == -1) {
            /* errno == ENOSPC: disco lleno */
            snprintf(log_msg, sizeof(log_msg),
                     "write() falló: %s", strerror(errno));
            log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
            return_code = COPY_ERR_WRITE;
            break;
        }

        if (bytes_written != bytes_read) {
            /*
             * Escritura parcial: write() no rechazó la llamada pero
             * escribió menos bytes de los pedidos.
             * Causa más común: disco lleno pero no del todo.
             */
            snprintf(log_msg, sizeof(log_msg),
                     "Escritura parcial: pedidos=%zd escritos=%zd (disco lleno?)",
                     bytes_read, bytes_written);
            log_both(AG_LOG_WARN, LOG_WARNING, src, dst, log_msg);
            return_code = COPY_ERR_WRITE;
            break;
        }
    }

    /* Verificar error de lectura (EOF da bytes_read==0, no -1) */
    if (bytes_read == -1) {
        snprintf(log_msg, sizeof(log_msg),
                 "read() falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        return_code = COPY_ERR_READ;
    }

    /* ── §6: Cerrar descriptores — SIEMPRE, incluso en error ── */
    /*
     * Si no se cierran, el SO mantiene la entrada en la tabla de
     * descriptores del proceso ("archivo fantasma").
     * Límite por defecto en Linux: 1024 descriptores abiertos.
     * Un programa que hace miles de copias sin close() → EMFILE.
     */
    if (close(fd_src) == -1) {
        snprintf(log_msg, sizeof(log_msg),
                 "close(fd_src) falló: %s", strerror(errno));
        log_both(AG_LOG_WARN, LOG_WARNING, src, NULL, log_msg);
    }
    if (close(fd_dst) == -1) {
        snprintf(log_msg, sizeof(log_msg),
                 "close(fd_dst) falló: %s", strerror(errno));
        log_both(AG_LOG_WARN, LOG_WARNING, NULL, dst, log_msg);
    }

    /* ── §7: Liberar buffer del heap ── */
    free(buffer);
    buffer = NULL;   /* Puntero a NULL evita uso accidental (dangling pointer) */

    /* Registro de resultado final */
    if (return_code == COPY_OK) {
        snprintf(log_msg, sizeof(log_msg),
                 "FIN copia kernel-space OK | buffer=%zu B", buf_size);
        log_both(AG_LOG_INFO, LOG_INFO, src, dst, log_msg);
    } else {
        snprintf(log_msg, sizeof(log_msg),
                 "FIN copia kernel-space ERROR (código=%d)", return_code);
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
    }

    return return_code;
}

/* =========================================================
 * user_smart_copy()
 * -----------------
 * Motor de copia en capa de usuario (stdio.h).
 *
 * Por qué es MÁS eficiente para archivos pequeños (< 1 MB):
 *
 *   Cuando un proceso llama read() directamente, cruza la frontera
 *   ring3 (espacio de usuario) → ring0 (espacio de kernel) y de vuelta.
 *   Ese cambio de contexto (context switch) cuesta ~1–10 µs por llamada.
 *
 *   fread() en cambio usa un buffer interno en espacio de usuario (~8 KB).
 *   Las primeras lecturas cargan el buffer con una sola syscall.
 *   Las siguientes lecturas pequeñas se sirven desde ese buffer
 *   SIN cruzar al kernel — no hay context switch.
 *
 *   Para un archivo de 4 KB leído en bloques de 64 bytes:
 *     read() directa:   64 syscalls × ~5 µs = ~320 µs de overhead
 *     fread() con buf:  1 syscall  × ~5 µs =   ~5 µs de overhead
 *
 *   Para archivos grandes (> 1 MB), esta ventaja desaparece porque
 *   el buffer de stdio se recarga muchas veces, generando tantos
 *   context switches como las syscalls directas pero con una capa
 *   de abstracción adicional que añade latencia. Ahí gana kernel-space.
 * ========================================================= */
int user_smart_copy(const char *src, const char *dst) {
    FILE  *f_src       = NULL;
    FILE  *f_dst       = NULL;
    char   buffer[BUFFER_SIZE];   /* Buffer estático de 4 KB en el stack */
    size_t bytes_read;
    int    return_code = COPY_OK;
    char   log_msg[256];

    /* ── Guardia de punteros ── */
    if (src == NULL || dst == NULL) {
        ag_log(AG_LOG_ERR, NULL, NULL,
               "user_smart_copy: puntero src o dst es NULL");
        return COPY_ERROR;
    }

    /* ── Validar permisos antes de fopen ── */
    return_code = validate_src_permissions(src);
    if (return_code != COPY_OK) return return_code;

    return_code = validate_dst_permissions(dst);
    if (return_code != COPY_OK) return return_code;
    return_code = COPY_OK;

    log_both(AG_LOG_INFO, LOG_INFO, src, dst,
             "INICIO copia user-space (stdio fread/fwrite)");

    /* ── Abrir fuente en modo binario lectura ── */
    f_src = fopen(src, "rb");
    if (f_src == NULL) {
        snprintf(log_msg, sizeof(log_msg),
                 "fopen(src, rb) falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        return COPY_ERR_SRC;
    }

    /* ── Abrir destino en modo binario escritura ── */
    f_dst = fopen(dst, "wb");
    if (f_dst == NULL) {
        snprintf(log_msg, sizeof(log_msg),
                 "fopen(dst, wb) falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        fclose(f_src);
        return COPY_ERR_DST;
    }

    /* ── Loop de copia ── */
    /*
     * fread(buf, 1, N, f) retorna el número de bytes leídos.
     * Retorna 0 tanto en EOF como en error; ferror() distingue.
     * fwrite(buf, 1, M, f) retorna M si todo fue bien, < M en error.
     */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f_src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, f_dst) != bytes_read) {
            snprintf(log_msg, sizeof(log_msg),
                     "fwrite falló: %s", strerror(errno));
            log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
            return_code = COPY_ERR_WRITE;
            break;
        }
    }

    /* Distinguir EOF limpio de error de lectura */
    if (ferror(f_src)) {
        snprintf(log_msg, sizeof(log_msg),
                 "fread falló: %s", strerror(errno));
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
        return_code = COPY_ERR_READ;
    }

    /* Cerrar siempre */
    fclose(f_src);
    fclose(f_dst);

    if (return_code == COPY_OK) {
        log_both(AG_LOG_INFO, LOG_INFO, src, dst,
                 "FIN copia user-space OK");
    } else {
        snprintf(log_msg, sizeof(log_msg),
                 "FIN copia user-space ERROR (código=%d)", return_code);
        log_both(AG_LOG_ERR, LOG_ERR, src, dst, log_msg);
    }

    return return_code;
}
