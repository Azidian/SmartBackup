# SmartBackup

AdaptGuard es una herramienta de respaldo inteligente y robusta escrita en C (estándar C11) para sistemas basados en POSIX (Linux/Unix). Su objetivo principal es optimizar el proceso de copia de archivos y directorios seleccionando dinámicamente la estrategia de E/S (Input/Output) más eficiente —ya sea en el espacio de usuario (User Space) o en el espacio del núcleo (Kernel Space)— basándose en el tamaño de los archivos.

## Autores
* Isabella Cadavid
* Isabella Ocampo
* Wendy Atehortua
* Juan Manuel Hernández

---

##  Tabla de Contenidos
1. [Características Principales]
2. [Algoritmo y Estrategias de Copia]
3. [Arquitectura del Proyecto]
4. [Seguridad y Validaciones]
5. [Dependencias y Requisitos]
6. [Instalación y Compilación]
7. [Guía de Usuario y Uso]
8. [Ejemplos de Ejecución y Benchmark]
9. [Conclusiones]

---

## 1. Características Principales
* **Menú Interactivo:** Interfaz de consola (CLI) amigable con colores ANSI para guiar al usuario paso a paso.
* **Respaldo Inteligente:** Analiza archivos o directorios, detectando cuáles son nuevos o han sido modificados (basado en tamaño y `mtime`), omitiendo los ya actualizados para ahorrar tiempo.
* **Manejo Dinámico de E/S:** Cambia automáticamente entre la API estándar de C (`stdio.h`) y llamadas directas al sistema (Syscalls POSIX) para maximizar el rendimiento.
* **Creación Rápida de Archivos:** Utilidad integrada para crear archivos de texto plano directamente desde la consola.
* **Historial Detallado:** Mantiene un registro de auditoría local y se integra nativamente con el `syslog` del sistema operativo.

---

## 2. Algoritmo y Estrategias de Copia

El corazón de AdaptGuard es su motor de decisión (`choose_strategy.c`), el cual evalúa el tamaño de cada archivo para minimizar los *context switches* (cambios de contexto entre Ring 3 y Ring 0) y optimizar el uso de los buffers.

| Estrategia | Umbral de Tamaño | Capa utilizada | API / Función | Tamaño de Buffer |
| :--- | :--- | :--- | :--- | :--- |
| **User Space** | `< 1 MB` | User Space | `fread` / `fwrite` (`stdio.h`) | Interno de stdio (~8 KB) |
| **Kernel Small**| `1 MB - 100 MB` | Kernel Space | Syscalls (`read` / `write`) | 4 KB (1 Página de Kernel) |
| **Kernel Large**| `>= 100 MB` | Kernel Space | Syscalls (`read` / `write`) | 64 KB |

> **Nota:** El usuario siempre tiene la opción de sobrescribir la recomendación del algoritmo y forzar una estrategia específica.

---

##  3. Arquitectura del Proyecto

El código fuente está modularizado para separar la lógica de negocio, la interfaz y las operaciones de bajo nivel:

```text
smart_backup/
 ┣  bin/
 ┃ ┗ adaptguard
 ┣ docs/
 ┃ ┣  ARCHITECTURE.md/
 ┃ ┗  README.md
 ┣ include/
 ┃ ┣  smart_copy.h    # Contratos globales, estructuras y macros (TIMER_START).
 ┃ ┗  menu.h          # Prototipos de la interfaz interactiva.
 ┣ src/
 ┃ ┣  choose_strategy.c # Lógica de decisión matemática y recomendación de copia.
 ┃ ┣  backup_engine.c   # Motor Kernel: syscalls puras (open, read, write) y logs.
 ┃ ┣  menu.c            # Interacción CLI, escaneo de directorios (dirent.h).
 ┃ ┗  main.c            # Punto de entrada y orquestador.
 ┣ tests/
 ┃ ┣ bench_1gb.sh
 ┃ ┣ bench_1kb.sh
 ┃ ┗ bench_1mb.sh
 ┗ Makefile
 Seguridad y Validaciones
```
---

## 4. Seguridad y Validaciones

Para garantizar la integridad de los datos, implementamos estrictas "Reglas de Oro":

*Anti-Sobrescritura Directa (Same-Path):* Bloquea la operación si origen y destino son la misma ruta, evitando la truncación accidental (pérdida a 0 bytes).

*Prevención de Bucles Infinitos (Anti-Recursión):* Verifica que la carpeta destino no esté contenida dentro de la carpeta fuente durante copias masivas.

*Validación de Integridad:* Comprueba la existencia y permisos de acceso de los archivos mediante stat() y access() antes de la E/S.

---

## 4. Dependencias y Requisitos

*Sistema Operativo:* Linux / macOS / Entornos POSIX (WSL en Windows).

*Compilador:* GCC o Clang.

*Estándar:* C11 (-std=c11).

*Bibliotecas:* Biblioteca estándar de C (glibc) y cabeceras POSIX (<sys/stat.h>, <unistd.h>, <fcntl.h>, <syslog.h>, <dirent.h>).

##  6. Instalación y Compilación
Asegúrate de tener las herramientas de compilación instaladas (ej. build-essential en Ubuntu/Debian).


```bash

# 1. Clonar o descargar el repositorio

# 2. Limpiar compilaciones anteriores
make clean

# 3. Compilar el proyecto
make

# 4. Ejecutar la aplicación
bin/adaptguard

```
---

## 7. Guía de Usuario

Al ejecutar AdaptGuard, se te presentará un menú principal con las siguientes opciones:
```
╔══════════════════════════════════════════════════╗
║                   SmartBackup                    ║
║               Kernel space utility               ║
╚══════════════════════════════════════════════════╝

  Carpeta de backups por defecto: ./backups

  ----------------------------------------------------------------
  MENÚ PRINCIPAL
  ----------------------------------------------------------------
  [1] Crear un archivo de texto
  [2] Respaldar un archivo
  [3] Respaldar una carpeta completa
  [4] Ver historial de backups
  [5] Benchmark: kernel-space vs user-space
  [6] Salir
============================================
```
# 1. Crear un archivo
Permite redactar notas o documentos directamente desde la terminal.

*Linux:*

Ingresa la ruta completa del archivo a crear, por ejemplo: /home/isa/EAFIT/isa_prueba.txt

Escribe el contenido línea por línea. Para guardar y salir, escribe FIN o fin (indistintamente) en una nueva línea.

*Windows (WSL o entornos similares):*

Ejemplo de ruta: /mnt/c/Users/Usuario/OneDrive/Documents/Prueba_SO/prueba_isa_wind.txt

El contenido se guarda igualmente escribiendo FIN.

# 2. Respaldar un archivo
Copia un archivo específico de un punto a otro.

*Linux:*

Origen: /home/isa/EAFIT/isa_prueba.txt

Destino: /home/isa/EAFIT/Respaldo

*Windows:*

Origen: /mnt/c/Users/Usuario/OneDrive/Documents/Prueba_SO/prueba_isa_wind.txt

Destino: /mnt/c/Users/Usuario/OneDrive/Documents/Prueba_SO/Respaldo

El sistema te mostrará si hubo cambios, te recomendará una estrategia (User vs Kernel Space) y te mostrará el tiempo exacto en microsegundos que tomó la operación.

# 3. Respaldar una carpeta completa
Sincroniza el contenido de un directorio completo.

*Linux:*

Origen: /home/isa/EAFIT/Pruebas

Destino: /home/isa/EAFIT/Respaldo

*Windows:*

Origen: /mnt/c/Users/Usuario/OneDrive/Documents/Prueba_SO/Pruebas

Destino: /mnt/c/Users/Usuario/OneDrive/Documents/Prueba_SO/Respaldo

El sistema iterará sobre todos los archivos, omitiendo los que no han sufrido cambios. Si hay archivos nuevos o modificados, calculará una estrategia global recomendada y procesará la copia por lotes, ofreciendo un resumen estadístico al final (archivos copiados, omitidos, con error y tiempo total).

# 4. Ejecutar Benchmark
Permite comparar el rendimiento entre las estrategias de User Space y Kernel Space para diferentes tamaños de archivo (1 KB, 1 MB, 100 MB y opcionalmente 1 GB). Los resultados se guardan en ./logs/backup.log.


```
==============================================================
 BENCHMARK — Kernel-space vs User-space
================================================================
 Este benchmark crea archivos de prueba en /tmp,
 ejecuta ambas estrategias de copia y compara tiempos.

 ¿Incluir prueba de 1 GB? (tarda ~10–30 s) [s/N]: s

 ----------------------------------------------------------------
 Generando archivos de prueba con datos aleatorios...
 ----------------------------------------------------------------
 Generando 1 KB     (/tmp/ag_bench_1kb.bin)... OK
 Generando 1 MB     (/tmp/ag_bench_1mb.bin)... OK
 Generando 100 MB   (/tmp/ag_bench_100mb.bin)... OK
 Generando 1 GB     (/tmp/ag_bench_1gb.bin)... OK

 ================================================================
 |Tamaño    | Kernel-sp.(s) |  User-sp.(s)  |  Ratio   | Más rápido
 ----------------------------------------------------------------
 |1 KB      |  0.004265 s   |   0.001063 s  |   4.012x | user-sp.  
 |1 MB      |  0.005629 s   |   0.002798 s  |   2.011x | user-sp.  
 |100 MB    |  0.481483 s   |   0.219457 s  |   2.194x | user-sp.  
 |1 GB      | 10.524779 s   |  41.980265 s  |   0.251x | kernel-sp.
 ================================================================
```

## Por qué `fread` es más eficiente en archivos pequeños

La API estándar de C (`stdio.h`), a la cual pertenecen `fread` y `fwrite`, implementa por defecto un mecanismo de **buffering en el espacio de usuario (User Space)**. Típicamente, este buffer interno tiene un tamaño de 8 KB. 

Cuando solicitamos leer o escribir un archivo muy pequeño (por ejemplo, 1 KB o un par de MBs), `fread` realiza una única llamada al sistema (`read`) que cruza al kernel para llenar su buffer interno. A partir de ahí, las lecturas subsiguientes se sirven directamente desde la memoria RAM en el espacio de usuario. 

Por el contrario, usar llamadas puras del kernel para operaciones diminutas nos expone al sobrecosto del **Context Switch** (Cambio de Contexto). Cada vez que un programa invoca una syscall como `read()` o `write()`, el procesador debe detener la ejecución en modo usuario (Ring 3), guardar el estado de los registros, cambiar al modo privilegiado (Ring 0), ejecutar la rutina del kernel y luego regresar. Este proceso toma entre 1 y 10 microsegundos por llamada. En archivos menores a 1 MB, este sobrecosto acumulado de cruzar la frontera entre Kernel y Usuario es mayor que el tiempo real que toma mover los bytes, haciendo que la capa de usuario sea significativamente más rápida.

---

## Por qué `sys_smart_copy` es una "función de sistema"

Diseñamos `sys_smart_copy` para que se comporte arquitectónicamente como una extensión del sistema operativo, despojándola de cualquier abstracción de alto nivel:

1. **Uso de Descriptores de Archivo (FDs):** A diferencia de `fopen` que retorna un puntero opaco `FILE*`, nuestra función utiliza directamente `open()`, devolviendo enteros (`int fd`) que mapean de manera exacta a la Tabla de Descriptores de Archivo del kernel.
2. **Acceso Crudo (Raw I/O):** Se invoca a `read()` y `write()` usando buffers de 4 KB y 64 KB, alineados intencionalmente con los tamaños de página (*Page Size*) de la memoria virtual del kernel para evitar la fragmentación.
3. **Manejo de Errores a Bajo Nivel:** Se auditan las operaciones utilizando la variable global de sistema `errno`. Las fallas se capturan a nivel POSIX (por ejemplo, evaluando `EACCES` para permisos o `ENOENT` para inexistencia).
4. **Integración con Daemons del SO:** En lugar de simplemente imprimir errores por consola (`printf` o `fprintf`), la función despacha sus logs críticos utilizando la biblioteca `<syslog.h>`. Esto inserta los eventos directamente en el demonio de registro del sistema (accesible vía `journalctl` o `/var/log/syslog`), exactamente igual a como lo haría un módulo de hardware o un servicio nativo de Linux.

---

## 8. Conclusiones

Los resultados obtenidos validan contundentemente la arquitectura propuesta para **AdaptGuard**: una única estrategia estática de copiado es ineficiente en escenarios del mundo real. 

1. **Justificación del User-Space para cargas ligeras:** Ha quedado demostrado (con un rendimiento hasta 4 veces superior en archivos de 1 KB) que evitar los *context switches* mediante el uso de `fread/fwrite` es imperativo para copiar masivamente archivos de texto pequeños, código fuente o configuraciones.
2. **El colapso de stdio en cargas pesadas:** Para archivos masivos (como ISOs, bases de datos o videos de 1 GB), la capa de usuario es un cuello de botella crítico, tardando casi 42 segundos en mover lo que el kernel mueve en apenas 10 segundos. La sobrecarga de gestionar un buffer pequeño millones de veces destruye el rendimiento.
3. **Eficacia del Algoritmo Adaptativo:** Al delegar la decisión a la lógica en `choose_strategy.c`, nuestro sistema logra lo mejor de ambos mundos de forma transparente para el usuario final. AdaptGuard lee los metadatos del archivo (`stat`), identifica el umbral (1 MB o 100 MB) y rutea la operación hacia el motor más óptimo, reduciendo los tiempos globales de respaldo drásticamente frente a utilidades de copia ingenuas.

---
