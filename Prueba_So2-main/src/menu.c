/*
 * src/menu.c
 * ==========
 * Implementacion de los menus y utilidades interactivas de AdaptGuard.
 */

#include "../include/menu.h"
#include "../include/smart_copy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>

/* ── Prototipos privados (static) ── */
static void        human_size(off_t b, char *buf, size_t sz);
static int         scan_dir(const char *dir, FileRecord *recs, int max);
static void        build_dst(const char *dst_dir, const char *src_path, char *out, size_t out_sz);
static const char *strat_name(int flag);
static const char *strat_desc(int flag);
static int         menu_strategy(int recommended);
static void        ask_dst_dir(char *buf, size_t sz);
static void        do_backup_records(FileRecord *recs, int total, const char *dst_dir, int user_strategy);
static void        log_historial(const char *src, const char *dst, int strategy, int status, off_t bytes, double secs);

/* =========================================================
 * FUNCIONES PUBLICAS (exportadas via menu.h)
 * ========================================================= */

void banner(void) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║                   " C_BOLD C_CYAN "SmartBackup" C_RESET "                    ║\n");
    printf("  ║               " C_DIM "Kernel space utility" C_RESET "               ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  " C_DIM "Carpeta de backups por defecto: %s\n" C_RESET, DEFAULT_BACKUP);
}

void sep(const char *s, int w) {
    printf("  ");
    for (int i = 0; i < w; i++) printf("%s", s);
    putchar('\n');
}

int read_line(const char *prompt, char *buf, size_t sz) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)sz, stdin) == NULL) { buf[0] = '\0'; return -1; }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return 0;
}

int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0755);
}

/* =========================================================
 * [1] CREAR ARCHIVO DE TEXTO
 * ========================================================= */
void menu_create_file(void) {
    char path[512];
    char content[4096];

    printf("\n");
    sep("-", LINE_W);
    printf(C_BOLD "  CREAR ARCHIVO DE TEXTO\n" C_RESET);
    sep("-", LINE_W);

    read_line("  Ruta del archivo a crear (ej: ./docs/notas.txt): ", path, sizeof(path));

    char parent[512];
    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        ensure_dir(parent);
    }

    printf("  Escribe el contenido (termina con una linea que solo diga FIN):\n");
    printf(C_DIM "  ────────────────────────────────────────\n" C_RESET);

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        printf(C_RED "  Error: no se pudo crear '%s': %s\n" C_RESET, path, strerror(errno));
        return;
    }

    char line[512];
    size_t total_written = 0;
    while (1) {
        printf("  ");
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        if (strcmp(line, "FIN\n") == 0 || strcmp(line, "fin\n") == 0) break;
        fputs(line, f);
        total_written += strlen(line);
    }
    fclose(f);

    char sz[16];
    struct stat st;
    if (stat(path, &st) == 0) human_size(st.st_size, sz, sizeof(sz));
    else                      snprintf(sz, sizeof(sz), "?");

    printf(C_DIM "  ────────────────────────────────────────\n" C_RESET);
    printf(C_GREEN "  ✓ Archivo creado: %s  (%s)\n" C_RESET, path, sz);
    (void)content;
    (void)total_written;
}

/* =========================================================
 * [2] RESPALDAR UN ARCHIVO
 * ========================================================= */
void menu_backup_file(void) {
    char src[512];
    char dst_dir[512];

    printf("\n");
    sep("-", LINE_W);
    printf(C_BOLD "  RESPALDAR UN ARCHIVO\n" C_RESET);
    sep("-", LINE_W);

    read_line("  Ruta del archivo fuente: ", src, sizeof(src));

    struct stat st;
    if (stat(src, &st) == -1) {
        printf(C_RED "  Error: '%s' no existe o no es accesible.\n" C_RESET, src);
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        printf(C_RED "  Error: '%s' no es un archivo regular.\n" C_RESET, src);
        return;
    }

    static FileRecord rec;
    memset(&rec, 0, sizeof(FileRecord));
    snprintf(rec.src_path, sizeof(rec.src_path), "%s", src);

    char tmp[512];
    strncpy(tmp, src, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    strncpy(rec.name, basename(tmp), sizeof(rec.name) - 1);
    rec.size = st.st_size;

    char sz[16];
    human_size(st.st_size, sz, sizeof(sz));
    printf("\n");
    printf("  PASO 1 · Archivo seleccionado\n");
    sep("-", LINE_W);
    printf("  Nombre : %s\n", rec.name);
    printf("  Tamano : %s\n", sz);

    printf("\n  PASO 2 · Donde guardar el backup?\n");
    sep("-", LINE_W);
    ask_dst_dir(dst_dir, sizeof(dst_dir));
    ensure_dir(dst_dir);
    build_dst(dst_dir, src, rec.dst_path, sizeof(rec.dst_path));

    printf("\n  PASO 3 · Analizando cambios...\n");
    sep("-", LINE_W);

    FileMetadata meta;
    int strategy_rec = choose_strategy(src, rec.dst_path, &meta);

    if (strategy_rec == COPY_ERROR) {
        printf(C_RED "  Error al analizar el archivo: %s\n" C_RESET, strerror(errno));
        return;
    }

    if (strategy_rec == FLAG_SKIP) {
        printf(C_YELLOW "  ✓ Sin cambios detectados — el backup ya esta actualizado.\n" C_RESET);
        printf("  Forzar copia de todas formas? [s/N]: ");
        char resp[8];
        if (fgets(resp, sizeof(resp), stdin) == NULL ||
            (resp[0] != 's' && resp[0] != 'S')) {
            printf("  Backup cancelado.\n");
            return;
        }
        strategy_rec = (st.st_size < THRESHOLD_SMALL)  ? FLAG_USER_SPACE
                     : (st.st_size < THRESHOLD_LARGE)  ? FLAG_KERNEL_SMALL
                     :                                   FLAG_KERNEL_LARGE;
    } else {
        const char *reason = !meta.dst_exists              ? "archivo nuevo (no existe en destino)"
                           : meta.src_size != meta.dst_size ? "tamano cambio"
                           :                                  "fecha de modificacion mas reciente";
        printf("  Cambio detectado : %s\n", reason);
    }

    rec.strategy_rec = strategy_rec;
    rec.needs_copy   = 1;

    printf("\n  PASO 4 · Estrategia de copia\n");
    sep("-", LINE_W);
    printf("  Estrategia recomendada: " C_GREEN "%s\n" C_RESET, strat_name(strategy_rec));
    printf("  Motivo: %s\n\n", strat_desc(strategy_rec));

    int strategy_final = menu_strategy(strategy_rec);
    rec.strategy = strategy_final;

    printf("\n  PASO 5 · Ejecutando backup...\n");
    sep("-", LINE_W);

    struct timespec t0, t1;
    int status;
    TIMER_START(t0);

    switch (strategy_final) {
        case FLAG_USER_SPACE:
            status = user_smart_copy(src, rec.dst_path); break;
        case FLAG_KERNEL_SMALL:
        case FLAG_KERNEL_LARGE:
            status = sys_smart_copy(src, rec.dst_path, strategy_final); break;
        default:
            status = COPY_ERROR;
    }

    TIMER_END(t1);
    double elapsed = TIMER_DIFF_SEC(t0, t1);
    rec.status    = status;
    rec.time_used = elapsed;

    printf("\n");
    sep("=", LINE_W);
    if (status == COPY_OK) {
        printf(C_GREEN "  ✓ Backup completado exitosamente\n" C_RESET);
        printf("  Origen   : %s\n",  src);
        printf("  Destino  : %s\n",  rec.dst_path);
        printf("  Estrategia usada: %s\n", strat_name(strategy_final));
        printf("  Tiempo   : %.6f s\n", elapsed);
        printf("  Bytes copiados  : ");
        printf(C_BOLD "%s\n" C_RESET, sz);
        log_historial(src, rec.dst_path, strategy_final, COPY_OK, st.st_size, elapsed);
    } else {
        printf(C_RED "  ✗ Error en el backup (codigo %d): %s\n" C_RESET, status, strerror(errno));
        log_historial(src, rec.dst_path, strategy_final, status, 0, elapsed);
    }
    sep("=", LINE_W);
}

/* =========================================================
 * [3] RESPALDAR CARPETA COMPLETA
 * ========================================================= */
void menu_backup_dir(void) {
    char src_dir[512];
    char dst_dir[512];

    printf("\n");
    sep("-", LINE_W);
    printf(C_BOLD "  RESPALDAR CARPETA COMPLETA\n" C_RESET);
    sep("-", LINE_W);

    read_line("  Ruta de la carpeta fuente: ", src_dir, sizeof(src_dir));

    struct stat st;
    if (stat(src_dir, &st) == -1 || !S_ISDIR(st.st_mode)) {
        printf(C_RED "  Error: '%s' no es una carpeta accesible.\n" C_RESET, src_dir);
        return;
    }

    printf("\n  PASO 1 · Escaneando carpeta fuente...\n");
    sep("-", LINE_W);

    static FileRecord records[MAX_FILES];
    int total = scan_dir(src_dir, records, MAX_FILES);

    if (total < 0) {
        printf(C_RED "  Error al leer la carpeta: %s\n" C_RESET, strerror(errno));
        return;
    }
    if (total == 0) {
        printf(C_YELLOW "  La carpeta esta vacia — nada que respaldar.\n" C_RESET);
        return;
    }
    printf("  %d archivo(s) encontrado(s)\n", total);

    printf("\n  PASO 2 · Donde guardar el backup?\n");
    sep("-", LINE_W);
    ask_dst_dir(dst_dir, sizeof(dst_dir));
    ensure_dir(dst_dir);

    for (int i = 0; i < total; i++)
        build_dst(dst_dir, records[i].src_path, records[i].dst_path, sizeof(records[i].dst_path));

    printf("\n  PASO 3 · Analizando cambios archivo por archivo...\n");
    sep("-", LINE_W);

    int n_changed = 0;
    int n_skip    = 0;

    for (int i = 0; i < total; i++) {
        FileMetadata meta;
        int srec = choose_strategy(records[i].src_path, records[i].dst_path, &meta);
        records[i].size        = meta.src_size;
        records[i].strategy_rec = srec;

        char sz[16];
        human_size(meta.src_size, sz, sizeof(sz));

        if (srec == FLAG_SKIP) {
            printf("  " C_DIM "[%d/%d] %-28s  OMITIR   sin cambios\n" C_RESET, i + 1, total, records[i].name);
            records[i].needs_copy = 0;
            records[i].status     = COPY_SKIP;
            n_skip++;
        } else if (srec == COPY_ERROR) {
            printf("  [%d/%d] %-28s  " C_RED "ERROR\n" C_RESET, i + 1, total, records[i].name);
            records[i].needs_copy = 0;
            records[i].status     = COPY_ERROR;
        } else {
            const char *reason =
                !meta.dst_exists               ? "nuevo"
              : meta.src_size != meta.dst_size ? "tamano"
              :                                  "mtime";
            strncpy(records[i].change_reason, reason, sizeof(records[i].change_reason) - 1);

            printf("  [%d/%d] %-28s  " C_GREEN "COPIAR   %-8s  %s  → %s\n" C_RESET,
                   i + 1, total, records[i].name, reason, sz, strat_name(srec));

            records[i].needs_copy = 1;
            n_changed++;
        }
    }
    sep("-", LINE_W);
    printf("  Necesitan backup: %d  |  Sin cambios: %d\n\n", n_changed, n_skip);

    if (n_changed == 0) {
        printf(C_YELLOW "  Todos los archivos estan actualizados.\n" C_RESET);

        printf("  Forzar copia de todos de todas formas? [s/N]: ");
        char resp[8];
        if (fgets(resp, sizeof(resp), stdin) != NULL &&
            (resp[0] == 's' || resp[0] == 'S')) {
            for (int i = 0; i < total; i++) {
                records[i].needs_copy   = 1;
                records[i].strategy_rec =
                    (records[i].size < THRESHOLD_SMALL)  ? FLAG_USER_SPACE
                  : (records[i].size < THRESHOLD_LARGE)  ? FLAG_KERNEL_SMALL
                  :                                        FLAG_KERNEL_LARGE;
            }
            n_changed = total;
        } else {
            return;
        }
    }

    printf("  PASO 4 · Estrategia de copia\n");
    sep("-", LINE_W);

    int votes[4] = {0, 0, 0, 0};
    for (int i = 0; i < total; i++) {
        if (records[i].needs_copy &&
            records[i].strategy_rec >= 0 &&
            records[i].strategy_rec <= 3)
            votes[records[i].strategy_rec]++;
    }
    int rec_global = FLAG_USER_SPACE;
    for (int f = FLAG_KERNEL_LARGE; f >= FLAG_USER_SPACE; f--)
        if (votes[f] >= votes[rec_global]) rec_global = f;

    printf("  Estrategia recomendada para la carpeta: " C_GREEN "%s\n" C_RESET, strat_name(rec_global));
    printf("  Motivo: %s\n\n", strat_desc(rec_global));
    printf("  Puedes elegir una estrategia distinta que se aplicara\n"
           "  a " C_BOLD "todos" C_RESET " los archivos de la carpeta.\n\n");

    int strategy_global = menu_strategy(rec_global);

    for (int i = 0; i < total; i++)
        if (records[i].needs_copy)
            records[i].strategy = strategy_global;

    printf("\n  PASO 5 · Ejecutando backups...\n");
    sep("-", LINE_W);

    do_backup_records(records, total, dst_dir, strategy_global);
}

/* =========================================================
 * [4] VER HISTORIAL
 * ========================================================= */
void menu_historial(void) {
    printf("\n");
    sep("-", LINE_W);
    printf(C_BOLD "  HISTORIAL DE BACKUPS\n" C_RESET);
    sep("-", LINE_W);

    FILE *f = fopen(HIST_FILE, "r");
    if (f == NULL) {
        printf("  (Sin registros todavia)\n");
        return;
    }

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        printf("  %s", line);
        count++;
    }
    fclose(f);

    if (count == 0)
        printf("  (Sin registros todavia)\n");
    else {
        sep("-", LINE_W);
        printf("  Total de operaciones registradas: %d\n", count);
    }
}

/* =========================================================
 * [5] BENCHMARK
 * ========================================================= */
void menu_benchmark(void) {
    printf("\n");
    sep("=", LINE_W);
    printf(C_BOLD "  BENCHMARK — Kernel-space vs User-space\n" C_RESET);
    sep("=", LINE_W);
    printf("  Este benchmark crea archivos de prueba en /tmp,\n");
    printf("  ejecuta ambas estrategias de copia y compara tiempos.\n");

    typedef struct {
        const char *label;
        size_t      size_bytes;
        const char *src;
        const char *dst_k;
        const char *dst_u;
    } BenchCase;

    BenchCase cases[] = {
        { "1 KB ",    1024UL,
          "/tmp/ag_bench_1kb.bin",
          "/tmp/ag_bench_1kb_k.bin",
          "/tmp/ag_bench_1kb_u.bin" },
        { "1 MB ",    1024UL * 1024,
          "/tmp/ag_bench_1mb.bin",
          "/tmp/ag_bench_1mb_k.bin",
          "/tmp/ag_bench_1mb_u.bin" },
        { "100 MB", 100UL * 1024 * 1024,
          "/tmp/ag_bench_100mb.bin",
          "/tmp/ag_bench_100mb_k.bin",
          "/tmp/ag_bench_100mb_u.bin" },
    };
    int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    printf("  Incluir prueba de 1 GB? (tarda ~10–30 s) [s/N]: ");
    char resp[8];
    int include_1gb = 0;
    if (fgets(resp, sizeof(resp), stdin) != NULL &&
        (resp[0] == 's' || resp[0] == 'S'))
        include_1gb = 1;

    BenchCase case_1gb = {
        "1 GB ", 1024UL * 1024 * 1024,
        "/tmp/ag_bench_1gb.bin",
        "/tmp/ag_bench_1gb_k.bin",
        "/tmp/ag_bench_1gb_u.bin"
    };
    if (include_1gb) n_cases = 4;

    printf("\n");
    sep("-", LINE_W);
    printf("  Generando archivos de prueba con datos aleatorios...\n");
    sep("-", LINE_W);

    for (int i = 0; i < n_cases; i++) {
        BenchCase *bc = (i < 3) ? &cases[i] : &case_1gb;

        printf("  Generando %-8s (%s)... ", bc->label, bc->src);
        fflush(stdout);

        int fd_rand = open("/dev/urandom", O_RDONLY);
        int fd_out  = open(bc->src, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd_rand == -1 || fd_out == -1) {
            printf(C_RED "ERROR al generar\n" C_RESET);
            if (fd_rand != -1) close(fd_rand);
            if (fd_out  != -1) close(fd_out);
            continue;
        }

        char *gbuf = (char *)malloc(BUFFER_LARGE);
        if (gbuf == NULL) {
            printf(C_RED "ERROR: sin memoria\n" C_RESET);
            close(fd_rand); close(fd_out);
            continue;
        }

        size_t remaining = bc->size_bytes;
        while (remaining > 0) {
            size_t chunk = (remaining > BUFFER_LARGE) ? BUFFER_LARGE : remaining;
            ssize_t r = read(fd_rand, gbuf, chunk);
            if (r <= 0) break;
            if (write(fd_out, gbuf, (size_t)r) != r) break;
            remaining -= (size_t)r;
        }
        free(gbuf);
        close(fd_rand);
        close(fd_out);
        printf(C_GREEN "OK\n" C_RESET);
    }

    printf("\n");
    sep("=", LINE_W);
    printf(C_BOLD "  %-8s  %-14s  %-14s  %-10s  %s\n" C_RESET,
           "Tamano", "Kernel-sp.(s)", "User-sp.(s)", "Ratio", "Mas rapido");
    sep("-", LINE_W);

    struct timespec t0, t1;

    for (int i = 0; i < n_cases; i++) {
        BenchCase *bc = (i < 3) ? &cases[i] : &case_1gb;

        int flag_k = (bc->size_bytes >= (size_t)THRESHOLD_LARGE)
                     ? FLAG_KERNEL_LARGE : FLAG_KERNEL_SMALL;

        TIMER_START(t0);
        int sk = sys_smart_copy(bc->src, bc->dst_k, flag_k);
        TIMER_END(t1);
        double tk = TIMER_DIFF_SEC(t0, t1);

        TIMER_START(t0);
        int su = user_smart_copy(bc->src, bc->dst_u);
        TIMER_END(t1);
        double tu = TIMER_DIFF_SEC(t0, t1);
        (void)su;

        if (sk != COPY_OK) {
            printf("  %-8s  " C_RED "ERROR en kernel-space\n" C_RESET, bc->label);
            continue;
        }

        double ratio = (tu > 1e-9) ? tk / tu : 0.0;
        const char *winner;
        if (tk < tu) winner = C_GREEN "kernel-sp." C_RESET;
        else         winner = C_CYAN  "user-sp.  " C_RESET;

        printf("  %-8s  %12.6f s  %12.6f s  %8.3fx  %s\n",
               bc->label, tk, tu, ratio, winner);

        char bench_msg[256];
        snprintf(bench_msg, sizeof(bench_msg),
                 "BENCHMARK %s | kernel=%.6fs user=%.6fs ratio=%.3fx",
                 bc->label, tk, tu, ratio);
        ag_log(AG_LOG_INFO, bc->src, NULL, bench_msg);

        unlink(bc->dst_k);
        unlink(bc->dst_u);
    }

    sep("=", LINE_W);
    printf("\n");
    printf(C_BOLD "  Interpretacion de resultados:\n" C_RESET);
    printf("  Ratio > 1.0 → user-space MAS rapido (kernel tarda mas)\n");
    printf("  Ratio < 1.0 → kernel-space MAS rapido (user tarda mas)\n\n");
    printf("  Por que user-space gana en archivos pequenos:\n");
    printf("  fread/fwrite usan un buffer interno de ~8KB en espacio de\n");
    printf("  usuario. Las lecturas se sirven SIN cruzar al kernel, evitando\n");
    printf("  el context switch (ring3→ring0→ring3 ≈ 1–10 µs por llamada).\n\n");
    printf("  Por que kernel-space gana en archivos grandes:\n");
    printf("  Con buffers de 4KB/64KB alineados al page cache del kernel,\n");
    printf("  se minimiza el numero total de syscalls. A >1MB el overhead\n");
    printf("  del buffer de stdio se recarga tantas veces que pierde ventaja.\n\n");
    printf("  Los resultados del benchmark se guardaron en: " C_BOLD "%s\n" C_RESET, LOG_FILE_PATH);
    sep("-", LINE_W);
}

/* =========================================================
 * FUNCIONES PRIVADAS (static)
 * ========================================================= */

static void human_size(off_t b, char *buf, size_t sz) {
    double d = (double)b;
    if      (d < 1024.0)             snprintf(buf, sz, "%.0f B",   d);
    else if (d < 1024.0*1024.0)      snprintf(buf, sz, "%.2f KB",  d/1024.0);
    else if (d < 1024.0*1024.0*1024) snprintf(buf, sz, "%.2f MB",  d/(1024.0*1024.0));
    else                             snprintf(buf, sz, "%.3f GB",  d/(1024.0*1024.0*1024.0));
}

static int scan_dir(const char *dir, FileRecord *recs, int max) {
    DIR           *dp = opendir(dir);
    struct dirent *e;
    struct stat    st;
    int            n = 0;
    char           full[512];

    if (!dp) return -1;
    while ((e = readdir(dp)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%.*s/%.*s", 255, dir, 255, e->d_name);
        if (stat(full, &st) == -1 || !S_ISREG(st.st_mode)) continue;
        memset(&recs[n], 0, sizeof(FileRecord));
        snprintf(recs[n].src_path, sizeof(recs[n].src_path), "%s", full);
        snprintf(recs[n].name, sizeof(recs[n].name), "%s", e->d_name);
        recs[n].size = st.st_size;
        n++;
    }
    closedir(dp);
    return n;
}

static void build_dst(const char *dst_dir, const char *src_path, char *out, size_t out_sz) {
    char tmp[512];
    strncpy(tmp, src_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    snprintf(out, out_sz, "%.255s/%.255s", dst_dir, basename(tmp));
}

static const char *strat_name(int flag) {
    switch (flag) {
        case FLAG_SKIP:         return "Sin cambios";
        case FLAG_USER_SPACE:   return "User-space  (stdio / fread-fwrite)";
        case FLAG_KERNEL_SMALL: return "Kernel-space  buffer 4 KB";
        case FLAG_KERNEL_LARGE: return "Kernel-space  buffer 64 KB";
        default:                return "Desconocida";
    }
}

static const char *strat_desc(int flag) {
    switch (flag) {
        case FLAG_USER_SPACE:
            return "Archivo < 1 MB. fread/fwrite usan buffer interno\n"
                   "  en espacio de usuario, evitando context switches innecesarios.";
        case FLAG_KERNEL_SMALL:
            return "Archivo 1 MB–100 MB. Syscalls directas con buffer de 4 KB\n"
                   "  alineado con la pagina del kernel (page cache).";
        case FLAG_KERNEL_LARGE:
            return "Archivo >= 100 MB. Syscalls con buffer de 64 KB para\n"
                   "  minimizar el numero total de llamadas read/write.";
        default:
            return "";
    }
}

static int menu_strategy(int recommended) {
    printf("  Elige la estrategia de copia:\n\n");
    printf("  " C_CYAN "[1]" C_RESET " User-space   — fread/fwrite (stdio)\n");
    printf("       Mejor para archivos pequenos (< 1 MB). Evita context\n");
    printf("       switches usando el buffer interno de FILE*.\n\n");
    printf("  " C_CYAN "[2]" C_RESET " Kernel 4 KB  — syscalls open/read/write, buffer 4 KB\n");
    printf("       Optimo para archivos medianos (1–100 MB). Buffer\n");
    printf("       alineado con la pagina del kernel.\n\n");
    printf("  " C_CYAN "[3]" C_RESET " Kernel 64 KB — syscalls open/read/write, buffer 64 KB\n");
    printf("       Mas eficiente para archivos grandes (> 100 MB).\n");
    printf("       Reduce 16× el numero de llamadas al kernel.\n\n");
    printf("  " C_GREEN "[4]" C_RESET " Usar estrategia recomendada " C_BOLD "(%s)\n" C_RESET, strat_name(recommended));
    printf("\n");

    char opt[8];
    while (1) {
        read_line("  Tu eleccion [1-4]: ", opt, sizeof(opt));
        switch (opt[0]) {
            case '1': return FLAG_USER_SPACE;
            case '2': return FLAG_KERNEL_SMALL;
            case '3': return FLAG_KERNEL_LARGE;
            case '4': return recommended;
            default:
                printf(C_RED "  Opcion no valida. Elige entre 1 y 4.\n" C_RESET);
        }
    }
}

static void ask_dst_dir(char *buf, size_t sz) {
    printf("  " C_CYAN "[1]" C_RESET " Usar carpeta por defecto: " C_BOLD "%s\n" C_RESET, DEFAULT_BACKUP);
    printf("  " C_CYAN "[2]" C_RESET " Especificar una carpeta distinta\n\n");

    char opt[8];
    read_line("  Tu eleccion [1/2]: ", opt, sizeof(opt));

    if (opt[0] == '2') {
        read_line("  Ruta de la carpeta destino: ", buf, sz);
        if (strlen(buf) == 0)
            strncpy(buf, DEFAULT_BACKUP, sz - 1);
    } else {
        strncpy(buf, DEFAULT_BACKUP, sz - 1);
    }
    buf[sz - 1] = '\0';

    printf("  " C_GREEN "✓ Destino: %s\n" C_RESET, buf);
}

static void do_backup_records(FileRecord *recs, int total, const char *dst_dir, int user_strategy) {
    int    n_ok    = 0;
    int    n_skip  = 0;
    int    n_err   = 0;
    off_t  total_b = 0;
    double total_t = 0.0;
    struct timespec t0, t1;
    (void)dst_dir;
    (void)user_strategy;

    printf("  %-5s %-28s %-8s %-14s %s\n", "N", "Archivo", "Estado", "Tiempo", "Bytes copiados");
    sep("-", LINE_W);

    for (int i = 0; i < total; i++) {
        if (!recs[i].needs_copy) {
            printf("  [%d/%d] %-28s " C_DIM "OMITIDO  (sin cambios)\n" C_RESET, i + 1, total, recs[i].name);
            n_skip++;
            continue;
        }

        int status;
        TIMER_START(t0);

        switch (recs[i].strategy) {
            case FLAG_USER_SPACE:
                status = user_smart_copy(recs[i].src_path, recs[i].dst_path);
                break;
            case FLAG_KERNEL_SMALL:
            case FLAG_KERNEL_LARGE:
                status = sys_smart_copy(recs[i].src_path, recs[i].dst_path, recs[i].strategy);
                break;
            default:
                status = COPY_ERROR;
        }

        TIMER_END(t1);
        double elapsed = TIMER_DIFF_SEC(t0, t1);
        recs[i].status    = status;
        recs[i].time_used = elapsed;

        char sz[16];
        human_size(recs[i].size, sz, sizeof(sz));

        if (status == COPY_OK) {
            printf("  [%d/%d] %-28s " C_GREEN "%-8s" C_RESET " %.6f s   " C_BOLD "%s\n" C_RESET,
                   i + 1, total, recs[i].name, "OK", elapsed, sz);
            n_ok++;
            total_b += recs[i].size;
            total_t += elapsed;
            log_historial(recs[i].src_path, recs[i].dst_path, recs[i].strategy, COPY_OK, recs[i].size, elapsed);
        } else {
            printf("  [%d/%d] %-28s " C_RED "%-8s" C_RESET " %s\n",
                   i + 1, total, recs[i].name, "ERROR", strerror(errno));
            n_err++;
            log_historial(recs[i].src_path, recs[i].dst_path, recs[i].strategy, status, 0, elapsed);
        }
    }

    char sz_total[16];
    human_size(total_b, sz_total, sizeof(sz_total));

    printf("\n");
    sep("=", LINE_W);
    printf(C_BOLD "  Resumen del backup\n" C_RESET);
    sep("-", LINE_W);
    printf("  Archivos analizados : %d\n",    total);
    printf("  Copiados            : " C_GREEN "%d\n" C_RESET, n_ok);
    printf("  Omitidos            : " C_DIM "%d  (sin cambios)\n" C_RESET, n_skip);
    printf("  Errores             : ");
    if (n_err > 0) printf(C_RED "%d\n" C_RESET, n_err);
    else           printf(C_GREEN "0\n" C_RESET);
    printf("  Tiempo total        : %.6f s\n", total_t);
    printf("  " C_BOLD "Bytes transferidos  : %s\n" C_RESET, sz_total);
    sep("=", LINE_W);
}

static void log_historial(const char *src, const char *dst, int strategy, int status, off_t bytes, double secs) {
    ensure_dir(DEFAULT_BACKUP);
    FILE *f = fopen(HIST_FILE, "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    char sz[16];
    human_size(bytes, sz, sizeof(sz));

    fprintf(f, "[%s]  %-8s  %-12s  %s → %s  (%s  %.6fs)\n",
            ts,
            status == COPY_OK   ? "OK"    :
            status == COPY_SKIP ? "SKIP"  : "ERROR",
            strat_name(strategy),
            src, dst,
            sz, secs);

    fclose(f);
}