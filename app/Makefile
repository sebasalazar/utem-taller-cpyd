# Autor: Sebastián Salazar Molina
# Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

# ==============================================================================
# Makefile — Compilación de "programa" en C++17 con OpenMP, libpqxx y libcurl
# ==============================================================================
# Flujo general:
#   1) Verifica dependencias externas con pkg-config (fail-fast).
#   2) Compila cada .cpp de FUENTES a build/*.o (con -MMD para dependencias).
#   3) Enlaza todos los objetos y genera dist/programa.
#
# Uso:
#   make            -> compila de forma incremental
#   make -j$(nproc) -> compila en paralelo
#   make clean      -> elimina build/ y dist/
# ==============================================================================


# ==============================================================================
# Configuración del Compilador y Banderas
# ==============================================================================
CXX      = g++
# Banderas de compilación:
#   -g3          -> máxima información de depuración (incluye macros)
#   -Wall        -> advertencias comunes
#   -Wextra      -> advertencias adicionales
#   -Wpedantic   -> advertencias estrictas del estándar
#   -std=c++17   -> estándar de C++ utilizado
#   -fopenmp     -> soporte para OpenMP
CXXFLAGS = -g3 -Wall -Wextra -Wpedantic -std=c++17 -fopenmp
# Banderas de enlazado: OpenMP también debe indicarse al enlazar
LDFLAGS  = -fopenmp


# ==============================================================================
# Dependencias Externas (pkg-config)
# ==============================================================================
# Paquetes requeridos; se resuelven rutas de cabeceras y librerías vía pkg-config
PAQUETES = libpqxx libcurl

# Verificación fail-fast: si falta algún paquete de desarrollo, se detiene la
# compilación con un mensaje indicando qué instalar y cómo verificarlo.
ifneq ($(shell pkg-config --exists $(PAQUETES) && echo ok),ok)
$(error Faltan paquetes de desarrollo. Instale: $(PAQUETES). Verifique con: pkg-config --exists $(PAQUETES))
endif

# Inyección explícita de rutas de cabeceras (--cflags) y librerías (--libs).
# Se agregan a CXXFLAGS y LDLIBS respectivamente, para que estén disponibles
# tanto en la compilación como en el enlazado.
CXXFLAGS += $(shell pkg-config --cflags $(PAQUETES))
LDLIBS   += $(shell pkg-config --libs $(PAQUETES))


# ==============================================================================
# Definición de Fuentes, Objetos y Dependencias
# ==============================================================================
# Lista explícita de fuentes (preferible a wildcards para control estricto
# del proceso de build y para que make detecte errores si falta un archivo).
FUENTES = main.cpp cliente_http.cpp configuracion_entorno.cpp uf.cpp uf_holder.cpp db.cpp utem.cpp

# Mapeo de fuentes a objetos en el directorio build/.
# Ejemplo: main.cpp -> build/main.o
OBJETOS = $(addprefix build/,$(FUENTES:.cpp=.o))

# Archivos .d generados por -MMD; registran las cabeceras de las que depende
# cada objeto, permitiendo recompilar si cambia algún .h.
DEPENDENCIAS = $(OBJETOS:.o=.d)


# ==============================================================================
# Objetivos (Targets)
# ==============================================================================
.PHONY: all clean

# Objetivo por defecto. No depende de 'clean' para permitir compilación
# incremental (recompilar solo lo que cambió).
all: dist/programa

# Regla de enlazado: combina todos los objetos y genera el ejecutable final
# en dist/. El prerrequisito de solo orden (| dist) garantiza que el
# directorio exista, sin forzar reconstrucciones por cambios de fecha.
dist/programa: $(OBJETOS) | dist
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJETOS) $(LDLIBS)

# Regla de compilación (patrón): cada .cpp se compila a su .o en build/.
#   $<  -> fuente correspondiente (p.ej. main.cpp)
#   $@  -> objeto destino         (p.ej. build/main.o)
#   -MMD -MP  -> genera archivos .d con dependencias de cabeceras y añade
#                reglas ficticias para evitar errores si una cabecera
#                desaparece del proyecto.
#   | build   -> prerrequisito de solo orden para asegurar el directorio.
build/%.o: %.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@


# ==============================================================================
# Gestión de Directorios (Prerequisitos de solo orden)
# ==============================================================================
# Al no ser .PHONY, make solo los creará si no existen.
build:
	@mkdir -p $@

dist:
	@mkdir -p $@


# ==============================================================================
# Limpieza y Mantenimiento
# ==============================================================================
clean:
	@echo "Limpiando artefactos de compilación..."
	rm -rf build dist *.csv


# ==============================================================================
# Inclusión condicional de dependencias de cabeceras
# ==============================================================================
# El guion (-) inicial evita errores si los archivos .d aún no existen
# (por ejemplo, en la primera compilación).
-include $(DEPENDENCIAS)

# Fuerza que el objetivo por defecto sea 'all', aunque no se indique en CLI.
.DEFAULT_GOAL := all