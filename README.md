# utem-cpyd-2602

**Ejemplo didáctico del uso de `curl`, `libpqxx` y `nlohmann/json` en C++17**

---

## 1. Propósito del recurso

Este repositorio constituye un **material docente** orientado a que el estudiante del curso de computación paralela de la Universidad Tecnológica Metropolitana del Estado de Chile (UTEM) observe, en un caso real y acotado, la integración de tres bibliotecas fundamentales del ecosistema C++ moderno:

- **`libcurl`** para la comunicación HTTP con una API pública.
- **`nlohmann/json`** para el análisis sintáctico y semántico de documentos JSON.
- **`libpqxx`** para la persistencia relacional en PostgreSQL.

El programa implementa un *pipeline* completo de extracción, validación, transformación y carga (*ETL*) del valor histórico de la **Unidad de Fomento (UF)** publicada por la Comisión para el Mercado Financiero (CMF).

Se recomienda al estudiante leer íntegramente este documento antes de compilar, y en particular la **Sección 7**, donde se exponen —con intención pedagógica— los errores deliberados y las decisiones mejorables en el código.

---

## 2. Descripción funcional

La aplicación de línea de comandos:

1. **Descarga** el historial de la UF desde la API de la CMF en formato JSON.
2. **Valida** cada registro:
   - fecha en formato `YYYY-MM-DD`,
   - valor finito, estrictamente positivo.
   - El valor puede arribar como número JSON o como cadena con formato numérico chileno (`"5.435,28"`).
3. **Persiste** cada registro en una tabla PostgreSQL mediante *upsert* por fecha.
4. **Exporta** el historial almacenado a `ufs.csv` (separador `;`, cabecera `"FECHA";"VALOR"`).

### 2.1 Estructura del repositorio

```
app/            Código fuente C++ y Makefile
  .env          Configuración y secretos (excluido de Git)
  json.hpp      nlohmann/json (cabecera única vendorizada)
db/
  01-model.sql  Esquema de la base de datos
```

---

## 3. Requisitos del entorno (Debian 13)

```bash
sudo apt update
sudo apt install build-essential pkg-config libpqxx-dev libcurl4-openssl-dev postgresql-client
```

Si el servidor de base de datos se ejecuta localmente:

```bash
sudo apt install postgresql
```

**Observaciones técnicas:**

- `build-essential` aporta `g++` (con soporte OpenMP vía `libgomp`) y `make`.
- El `Makefile` verifica la disponibilidad de `libpqxx` y `libcurl` mediante `pkg-config` y aborta con un mensaje explícito si faltan. Puede verificarse manualmente con:
  ```bash
  pkg-config --exists libpqxx libcurl && echo ok
  ```

---

## 4. Preparación de la base de datos

**Paso 1.** Creación de usuario y base de datos:

```bash
sudo -u postgres psql -c "CREATE USER uf_user WITH PASSWORD 'cambie_esta_clave';"
sudo -u postgres psql -c "CREATE DATABASE uf_db OWNER uf_user;"
```

**Paso 2.** Aplicación del esquema (tabla `ufs`, índice único sobre `uf_date`, *trigger* que actualiza la columna `updated`):

```bash
psql -h localhost -U uf_user -d uf_db -f db/01-model.sql
```

> ⚠️ **Advertencia:** el script ejecuta `DROP TABLE IF EXISTS ufs`; su aplicación **destruye** los datos preexistentes de dicha tabla. En entornos productivos esta práctica es inaceptable y se analiza como **Oportunidad de mejora n.º 2** en la Sección 7.

---

## 5. Configuración (`app/.env`)

El programa recibe como argumento la ruta a un archivo `CLAVE=VALOR`. Las líneas vacías y las iniciadas por `#` se ignoran; los valores admiten comillas simples o dobles. Cree el archivo en `app/` (excluido de Git):

```ini
# API de la CMF
CMF_API_URL=https://api.cmfchile.cl/api-sbifv3/recursos_api/uf/posteriores/1985
CMF_API_KEY=su_clave_de_api

# PostgreSQL
DB_HOST=localhost
DB_PORT=5432
DB_NAME=uf_db
DB_USER=uf_user
DB_PASS=cambie_esta_clave
```

| Variable       | Descripción                                                                 |
|----------------|-----------------------------------------------------------------------------|
| `CMF_API_URL`  | URL del recurso de la CMF **sin** la clave. El programa concatena `&apikey=`. |
| `CMF_API_KEY`  | Clave de acceso a la API de la CMF.                                          |
| `DB_HOST`      | Servidor PostgreSQL.                                                         |
| `DB_PORT`      | Puerto PostgreSQL (habitualmente `5432`).                                    |
| `DB_NAME`      | Nombre de la base de datos.                                                  |
| `DB_USER`      | Usuario de la base de datos.                                                 |
| `DB_PASS`      | Contraseña del usuario.                                                      |

Si falta alguna clave, el programa finaliza indicando cuál. **Nunca publique el contenido de `.env`.**

---

## 6. Compilación y ejecución

**Compilar** (desde `app/`):

```bash
cd app
make
```

Genera `app/dist/programa`; los objetos intermedios se dejan en `app/build/`.

**Limpiar artefactos** (`build/`, `dist/` y los `*.csv` de `app/`):

```bash
make clean
```

**Ejecutar** (desde `app/`, indicando la ruta al archivo de configuración):

```bash
./dist/programa .env
```

El programa, durante su ejecución:

- imprime por la salida estándar la respuesta JSON íntegra de la CMF y el número de UF recibidas;
- persiste las UF en la base de datos;
- escribe `ufs.csv` en el directorio de trabajo;
- imprime los datos de autoría al final, **incluso si ocurrió un error**.

Los mensajes de error se dirigen a la salida estándar de error.

---

## 7. Estado conocido y oportunidades de aprendizaje

Esta sección es el **núcleo pedagógico** del recurso. Los puntos que siguen no son meros defectos: son **ejercicios propuestos** para que el estudiantado diagnostique, discuta y rediseñe. Cada ítem indica el **concepto involucrado** y una **tarea sugerida**.

### 7.1 Errores documentados en el código

**Oportunidad 1 — Reparto incorrecto del lote en la persistencia paralela.**

> *Ubicación:* `app/main.cpp`, paso 8.
> *Síntoma:* cada hilo escribe el lote **completo** en lugar de un subconjunto, por lo que cada UF se persiste tantas veces como hilos haya.
> *Concepto involucrado:* paralelismo de datos, particionamiento de cargas de trabajo, coste de la contención en la base de datos.
> *Tarea Propuesta:* rediseñe el bucle paralelo para que cada hilo procese un intervalo disjunto del vector, verificando que la unión de intervalos cubra exactamente el total de registros y sin solapamientos.

### 7.2 Decisiones mejorables (no necesariamente errores)

**Oportunidad 2 — Esquema destructivo.**
`db/01-model.sql` ejecuta `DROP TABLE IF EXISTS ufs`. *Concepto:* migraciones idempotentes, control de versiones del esquema.
*Tarea Propuesta:* reescriba el script usando `CREATE TABLE IF NOT EXISTS` y, si procede, migraciones incrementales con una tabla de control (`schema_version`).

**Oportunidad 3 — Ausencia de transacciones explícitas.**
El *upsert* masivo no se envuelve en una transacción. *Concepto:* atomicidad, `BEGIN`/`COMMIT`/`ROLLBACK`, coste de `fsync`.
*Tarea Propuesta:* envuelva el lote en una sola transacción y compare el rendimiento frente al modo autocommit.

**Oportunidad 4 — Ausencia de *retry* y de *timeout* en la capa HTTP.**
Una falla transitoria de red aborta el proceso. *Concepto:* resiliencia, *backoff* exponencial, `CURLOPT_TIMEOUT` y `CURLOPT_CONNECTTIMEOUT`.
*Tarea Propuesta:* implemente un reintento acotado con espera exponencial y *jitter*.

**Oportunidad 5 — Ausencia de *rate limiting*.**
La API de la CMF impone cuotas. *Concepto:* cortesía con servicios externos, `429 Too Many Requests`.
*Tarea Propuesta:* introduzca una pausa configurable entre peticiones y maneje el código `429` leyendo la cabecera `Retry-After`.

**Oportunidad 6 — Parser de `.env` casero.**
La implementación manual es frágil (comillas anidadas, escapes, comentarios en línea). *Concepto:* separación de responsabilidades, uso de bibliotecas maduras.
*Tarea Propuesta:* evalúe alternativas (por ejemplo, `dotenv-cpp`) e implementela.

**Oportunidad 7 — Validación de la URL y de la clave.**
No se verifica que `CMF_API_URL` sea un URL bien formado ni que `CMF_API_KEY` tenga longitud razonable. *Concepto:* validación temprana (*fail fast*), *parsing* de URLs.
*Tarea Propuesta:* añada comprobaciones previas y mensajes de error específicos.

**Oportunidad 8 — Salida de autoría impresa incluso en error.**
Comportamiento declarado como intencional, pero discutible en términos de UX y de scripting. *Concepto:* separación entre datos y metadatos en `stdout`, uso de `stderr` para diagnóstico.
*Tarea Propuesta:* proponga un indicador `--quiet` o redirija la firma a `stderr`.

**Oportunidad 9 — Ausencia de pruebas automatizadas.**
No se incluyen *unit tests* ni *integration tests*. *Concepto:* TDD, cobertura, *fixtures* de red.
*Tarea Propuesta:* añada pruebas con un marco como **Catch2** o **GoogleTest**, y simule la API con un servidor HTTP local (por ejemplo `ncat`).

**Oportunidad 10 — Ausencia de banderas de advertencia en la compilación.**
El `Makefile` no exige `-Wall -Wextra -Wpedantic` ni `-fsanitize=address,undefined`. *Concepto:* higiene del compilador, *undefined behavior*.
*Tarea Propuesta:* incorpore dichas banderas en un *target* `make check` y documente los avisos obtenidos.

**Oportunidad 11 — Ausencia de registro estructurado (*logging*).**
El programa usa `std::cout`/`std::cerr` directamente. *Concepto:* niveles de severidad, trazabilidad, integración con sistemas de observabilidad.
*Tarea Propuesta:* sustituya por **spdlog** o por una fachada propia con niveles `DEBUG`/`INFO`/`WARN`/`ERROR`.

**Oportunidad 12 — Persistencia en serie frente a paralela sin métrica.**
El código no mide el tiempo del *upsert* ni compara estrategias. *Concepto:* medición empírica, *benchmarking*.
*Tarea Propuesta:* instrumente con `std::chrono` y elabore una tabla comparativa (serie vs. paralelo, con y sin transacción).

**Oportunidad 13 — Nombre de archivo de salida fijo.**
`ufs.csv` se escribe en el directorio de trabajo sin posibilidad de reconfiguración. *Concepto:* configurabilidad, principio de mínima sorpresa.
*Tarea PRopuesta:* añada una variable `CSV_PATH` o un argumento de línea de comandos.

**Oportunidad 14 — Ausencia de contenedores ni de CI.**
No se ofrece `Dockerfile` ni flujo de integración continua. *Concepto:* reproducibilidad, entrega continua.
*Tarea PRopuesta:* proponga un `Dockerfile` multi-etapa y un flujo de GitHub Actions que compile y ejecute pruebas.

**Oportunidad 15 — Escritura directa a PostgreSQL sin *pool* de conexiones.**
Irrelevante en este ejemplo monohilo de escritura, pero inadecuado si el programa evoluciona. *Concepto:* gestión de recursos, `libpqxx::connection_pool`.
*Tarea Propuesta:* documente por qué no se usa *pool* aquí y bajo qué condiciones sí sería necesario.

---

## 8. Cómo abordar las oportunidades

Se sugiere al estudiantado proceder en el siguiente orden, registrando cada cambio en el historial de Git con mensajes descriptivos:

1. **Diagnóstico:** reproducir el comportamiento actual (incluido el error de la Oportunidad 1) y documentarlo con evidencia (capturas, *timings*, volcados de la tabla).
2. **Hipótesis:** formular por escrito la causa del defecto y el efecto esperado de la corrección.
3. **Corrección mínima:** aplicar el cambio más pequeño que resuelva el problema.
4. **Verificación:** añadir una prueba automatizada que falle antes del cambio y pase después.
5. **Refactorización:** mejorar legibilidad y documentación sin alterar el comportamiento observable.
6. **Informe:** entregar un breve documento (máximo dos páginas) que sintetice el diagnóstico, la solución y las lecciones aprendidas.

---

## 9. Autoría

**Sebastián Salazar Molina**, académico de la Universidad Tecnológica Metropolitana del Estado de Chile (UTEM).

---

> *Nota final para el estudiantado:* los errores contenidos en este repositorio son **intencionales** y forman parte del diseño didáctico. Se recomienda no corregirlos silenciosamente, ni pasarselos directamente a un agente de IA, sino hacer un fork **documentarlos** y **justificar** cada decisión de mejora con argumentos técnicos y una implementación realizada por cada uno.
