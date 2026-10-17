CXX = g++
CXXFLAGS = -g3 -Wall -Wpedantic -Wextra -fopenmp -std=c++17
LDFLAGS = -fopenmp

PAQUETES = libpqxx libcurl

ifneq ($(shell pkg-config --exists $(PAQUETES) && echo ok), ok)
$(error Faltan paquetes de desarrollo. Instale: $(PAQUETES))
endif

CXXFLAGS += $(shell pkg-config --cflags $(PAQUETES))
LDLIBS += $(shell pkg-config --libs $(PAQUETES))

FUENTES = main.cpp

OBJETOS = $(addprefix build/, $(FUENTES:.cpp=.o))
DEPENDENCIAS = $(OBJETOS:.o=.d)

.PHONY: all clean

all: dist/programa

dist/programa: $(OBJETOS) | dist
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJETOS) $(LDLIBS)

build/%.o: %.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	@mkdir -p $@

dist:
	@mkdir -p $@

clean:
	@echo "Limpiando artefactos de compilación..."
	rm -fr build dist *.csv

-include $(DEPENDENCIAS)

.DEFAULT_GOAL := all