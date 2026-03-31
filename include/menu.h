#ifndef MENU_H
#define MENU_H

#include <stddef.h>     /* Para size_t */
#include <sys/types.h>  /* Para off_t */

/* ── Constantes compartidas ── */
#define MAX_FILES       1024
#define DEFAULT_BACKUP  "./backups"
#define HIST_FILE       "./backups/.historial.log"
#define LINE_W          64

/* ── Colores ANSI ── */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_RED     "\033[31m"
#define C_DIM     "\033[2m"

/* ── Registro de un archivo ── */
typedef struct {
    char   src_path[512];
    char   dst_path[512];
    char   name[256];
    int    strategy;
    int    strategy_rec;
    int    status;
    double time_used;
    off_t  size;
    int    needs_copy;
    char   change_reason[32];
} FileRecord;

/* =========================================================
 * FUNCIONES DISPONIBLES
 * ========================================================= */

/*
 * banner()
 * --------
 * Imprime el encabezado principal (título del sistema AdaptGuard) 
 * y la ruta de backup por defecto por pantalla usando colores ANSI.
 */
void banner(void);

/*
 * sep()
 * -----
 * Imprime una línea separadora en la consola para mejorar 
 * la legibilidad de la interfaz de usuario.
 *
 * Parámetros:
 * s - Cadena/carácter a repetir (ej. "-", "=")
 * w - Ancho de la línea (número de repeticiones)
 */
void sep(const char *s, int w);

/*
 * ensure_dir()
 * ------------
 * Verifica si un directorio existe en la ruta especificada.
 * Si no existe, intenta crearlo con los permisos adecuados (0755).
 *
 * Parámetros:
 * path - Ruta del directorio a verificar/crear.
 *
 * Retorna: 0 si el directorio existe o fue creado exitosamente, -1 en caso de error.
 */
int ensure_dir(const char *path);

/*
 * read_line()
 * -----------
 * Solicita entrada al usuario mostrando un mensaje y lee la línea
 * ingresada de forma segura mediante fgets, eliminando el salto de línea.
 *
 * Parámetros:
 * prompt - Texto a mostrar al usuario.
 * buf    - Buffer donde se almacenará el texto leído.
 * sz     - Tamaño máximo del buffer.
 *
 * Retorna: 0 en éxito, -1 si hubo un error al leer o fin de archivo (EOF).
 */
int read_line(const char *prompt, char *buf, size_t sz);

/*
 * menu_create_file()
 * ------------------
 * Menú interactivo que permite al usuario crear un archivo de texto 
 * en una ruta específica directamente desde la consola, introduciendo 
 * líneas de texto hasta teclear la palabra "FIN".
 */
void menu_create_file(void);

/*
 * menu_backup_file()
 * ------------------
 * Menú interactivo para respaldar un único archivo.
 * Analiza el archivo, sugiere una estrategia de copia (kernel vs user space),
 * permite al usuario elegir o forzar la estrategia y realiza la copia.
 */
void menu_backup_file(void);

/*
 * menu_backup_dir()
 * -----------------
 * Menú interactivo para respaldar un directorio completo.
 * Escanea la carpeta fuente, detecta archivos modificados/nuevos y 
 * realiza el proceso de respaldo secuencialmente informando un resumen al final.
 */
void menu_backup_dir(void);

/*
 * menu_show_historial()
 * ---------------------
 * Lee y muestra en pantalla el registro histórico de las operaciones 
 * de copia guardadas en el archivo de log (HIST_FILE).
 */
void menu_show_historial(void);

#endif /* MENU_H */
