// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// db.cpp
#include "db.h"
#include "error.h"

#include <cstdint>
#include <fstream>   // Requerido para std::ofstream
#include <string>


/**
 * @brief Construye la conexión a PostgreSQL delegando en libpqxx.
 *
 * @details
 * El cuerpo del constructor está vacío porque no hay nada que hacer más allá
 * de inicializar el miembro `conexion_` con la cadena proporcionada. Toda la
 * lógica de conexión —resolución de parámetros, establecimiento del socket,
 * negociación del protocolo, autenticación— reside en `pqxx::connection`, y
 * cualquier fallo en ese proceso se manifiesta como una excepción lanzada
 * durante la inicialización del miembro.
 *
 * @par Por qué no hay try/catch
 * No envolver la inicialización del miembro en un bloque `try` es una
 * decisión consciente. Si `pqxx::connection` lanza `pqxx::broken_connection`,
 * la excepción se propaga tal cual al llamador, conservando el tipo original
 * y toda la información de diagnóstico que libpqxx adjunta (código de error,
 * mensaje del servidor si lo hay). Envolverla en `Error` aplastaría esa
 * información sin ganar nada: el llamador que necesite distinguir fallos de
 * conexión de otros tipos puede capturar `pqxx::broken_connection` directamente,
 * y el que no necesite distinguir puede capturar `std::exception`.
 *
 * Esta misma política se aplica a `guardar()`. `exportar()` es la excepción:
 * envuelve sus errores en `Error`.
 *
 * @par Estado del objeto si el constructor lanza
 * Si la inicialización de `conexion_` falla, el objeto `Db` nunca llega a
 * existir. Los miembros ya construidos —ninguno en este caso, porque
 * `conexion_` es el único— se destruyen por unwinding, y el control vuelve
 * al llamador con la excepción. No hay estado parcial que limpiar, no hay
 * destructor que invocar, y no hay ninguna conexión abierta que cerrar:
 * libpqxx garantiza que un `pqxx::connection` que no se construyó no deja
 * recursos colgando.
 *
 * @throws pqxx::broken_connection Propagada desde la inicialización de
 *               `conexion_`. Ver la documentación de la clase en db.h para
 *               el contrato completo de errores.
 *
 * @note La inicialización se realiza en la lista de inicialización de
 *       miembros, no en el cuerpo del constructor. Esta es la forma correcta
 *       de construir miembros que no son trivialmente inicializables: el
 *       cuerpo vacío con `conexion_(cadena_conexion)` sería equivalente pero
 *       requeriría que `pqxx::connection` fuera asignable, lo cual no es el
 *       caso.
 */
Db::Db(const std::string &cadena_conexion) : conexion_(cadena_conexion) {
    // La conexión se establece aquí, en la inicialización del miembro. Si
    // falla, pqxx lanzará pqxx::broken_connection, que se propaga al
    // llamador sin transformación (ver documentación del método).
}

/**
 * @brief Persiste un registro de UF mediante UPSERT transaccional.
 *
 * @details
 * La operación se ejecuta dentro de una transacción explícita `pqxx::work`,
 * que garantiza atomicidad: o el UPSERT se completa íntegramente, o no
 * modifica la base de datos. El uso de `pqxx::work` —frente a una sentencia
 * suelta sobre la conexión— es deliberado aunque el cuerpo actual contenga
 * una única sentencia, porque:
 *
 *   1. **Sienta precedente para extensiones.** Si en el futuro se añadiera
 *      una segunda sentencia relacionada (por ejemplo, registrar la
 *      operación en una tabla de auditoría), la transacción ya está abierta
 *      y basta con añadir el `exec` correspondiente antes del `commit`.
 *   2. **Uniforma la semántica de error.** Cualquier fallo en la sentencia
 *      SQL aborta la transacción y deja la conexión en estado limpio,
 *      evitando transacciones colgadas que bloquearían filas hasta el
 *      timeout del servidor.
 *   3. **Aísla la operación según el nivel configurado.** PostgreSQL aplica
 *      el nivel de aislamiento por defecto (típicamente *read committed*) a
 *      la transacción, no a la sentencia suelta, y ese aislamiento es el
 *      que da sentido al UPSERT bajo concurrencia.
 *
 * @par Prevención de inyección SQL
 * La sentencia se construye con marcadores posicionales (`$1`, `$2`) y los
 * valores se pasan como `pqxx::params`. Esta es la única forma correcta de
 * interpolar datos en SQL: libpqxx serializa cada valor según su tipo y lo
 * envía al servidor como parámetro separado, no como texto concatenado. La
 * consecuencia práctica es que un valor como `'; DROP TABLE ufs; --` se
 * almacena como literal, sin ninguna posibilidad de ser interpretado como
 * SQL.
 *
 * @par Semántica del UPSERT
 * `ON CONFLICT (uf_date) DO UPDATE SET uf_value = EXCLUDED.uf_value` realiza
 * un *insert-or-update* sobre la columna de fecha. La referencia `EXCLUDED`
 * designa la fila que se habría insertado, de modo que el valor actualizado
 * es el que se acaba de proporcionar. Si la tabla no tuviera una restricción
 * de unicidad sobre `uf_date`, la cláusula `ON CONFLICT` fallaría en tiempo
 * de ejecución con un error de PostgreSQL, lo cual es preferible a un
 * comportamiento silenciosamente incorrecto.
 *
 * @par Ciclo de vida de la transacción
 * El objeto `tx` de tipo `pqxx::work` gestiona el ciclo completo:
 *
 *   - Al construirse sobre `conexion_`, abre una transacción en el servidor.
 *   - Si el cuerpo de `guardar` termina sin excepción, `tx.commit()` cierra
 *     la transacción y persiste los cambios.
 *   - Si una excepción escapa del cuerpo —por ejemplo, desde `tx.exec`—, el
 *     destructor de `tx` realiza un `rollback` automático antes de propagar
 *     la excepción. No se necesita un `try/catch` explícito para garantizar
 *     esta limpieza: es la propiedad RAII de `pqxx::work`.
 *
 * @par Política de errores
 * Esta función **no** captura excepciones. Cualquier fallo de libpqxx se
 * propaga con su tipo original, conservando el código SQLSTATE y el mensaje
 * del servidor. La política de `exportar()` es distinta: envuelve sus errores
 * en `Error`.
 *
 * @throws Error Si no hay conexión activa en el momento de la invocación.
 *               Este caso no debería producirse bajo uso correcto (el
 *               constructor garantiza una conexión activa), pero la
 *               comprobación se mantiene como salvaguarda contra un estado
 *               inesperado de libpqxx —por ejemplo, si una operación previa
 *               hubiera cerrado la conexión como efecto secundario.
 * @throws pqxx::sql_error Si la sentencia es rechazada por el servidor.
 * @throws pqxx::broken_connection Si la conexión se pierde durante la
 *               ejecución del UPSERT.
 *
 * @note `pqxx::params` es la API moderna de libpqxx para paso de parámetros
 *       (introducida en la versión 7.x). La API anterior basaba el paso en
 *       `operator()` con argumentos variádicos; el cambio elimina ambigüedades
 *       de resolución y hace explícito el tipo de cada parámetro en el
 *       mensaje.
 */
void Db::guardar(const Uf &uf) {
    if (!conexion_.is_open()) {
        throw Error("No hay conexión a la base de datos activa al intentar guardar UF.");
    }

    // Transacción explícita (pqxx::work) para garantizar atomicidad. El
    // destructor de 'tx' realiza rollback automático si una excepción escapa
    // del cuerpo, sin necesidad de try/catch explícito.
    pqxx::work tx{conexion_};

    // Consulta parametrizada: prevención absoluta de Inyección SQL (SQLi).
    // Los marcadores posicionales $1 y $2 se sustituyen en el servidor por
    // los valores serializados de pqxx::params; nunca se concatenan al texto
    // de la consulta.
    tx.exec(
        "INSERT INTO ufs (uf_date, uf_value) VALUES ($1, $2) "
        "ON CONFLICT (uf_date) DO UPDATE SET uf_value = EXCLUDED.uf_value",
        pqxx::params{uf.obtener_fecha(), uf.obtener_valor()}
    );

    tx.commit();
}

/**
 * @brief Exporta la tabla de UF a un archivo CSV con separador ';'.
 *
 * @details
 * La operación combina dos recursos que deben gestionarse con cuidado: la
 * transacción de lectura sobre PostgreSQL y el archivo de salida en disco.
 * Ambos siguen el patrón RAII —la transacción a través de
 * `pqxx::nontransaction` y el archivo a través de `std::ofstream`—, y el
 * orden en que se declaran determina el orden en que se liberan si algo
 * falla a mitad de operación.
 *
 * @par Elección de `pqxx::nontransaction` frente a `pqxx::work`
 * La consulta es puramente de lectura y no requiere atomicidad
 * multi-sentencia. En PostgreSQL, toda sentencia se ejecuta dentro de una
 * transacción implícita aunque el cliente no la abra; usar
 * `pqxx::nontransaction` simplemente evita el `BEGIN`/`COMMIT` explícito y
 * el coste asociado. La semántica observable es idéntica a la de un `SELECT`
 * suelto, pero el código deja explícito que no se espera modificar nada.
 *
 * @par Orden de apertura de recursos
 * El archivo CSV se abre **antes** de ejecutar la consulta. Este orden tiene
 * una consecuencia práctica importante: si la ruta de destino no es
 * escribible —directorio inexistente, permisos insuficientes, ruta apuntando
 * a un directorio— el error se detecta antes de haber lanzado ninguna
 * consulta contra el servidor. El coste de un fallo se minimiza y el mensaje
 * de error apunta directamente al problema real, sin haber consumido
 * recursos de red ni tiempo de servidor.
 *
 * @par Formato del CSV
 * El archivo usa punto y coma (`;`) como separador en lugar de la coma
 * estándar de RFC 4180. Esta decisión responde a la compatibilidad con Excel
 * en configuraciones regionales donde la coma es el separador decimal: un
 * CSV con comas sería parseado incorrectamente por Excel en esas
 * configuraciones, mientras que un CSV con puntos y coma se abre sin
 * problemas.
 *
 * La cabecera y los valores van entre comillas dobles, lo que permite que
 * valores con el separador o con saltos de línea internos se interpreten
 * correctamente por consumidores conformes a RFC 4180. La codificación es
 * UTF-8 y el final de línea es `\n` (Unix).
 *
 * @par Ejemplo de salida
 * @code
 *   "FECHA";"VALOR"
 *   "2024-01-15";"38542"
 *   "2024-01-16";"38568"
 *   "2024-01-17";"38591"
 * @endcode
 *
 * @par Conversión de tipos en la consulta
 * La columna `uf_value` se proyecta como `bigint` en la propia consulta SQL,
 * no en C++. Esta decisión evita depender de la interpretación de tipos
 * numéricos por parte de libpqxx —que puede diferir entre versiones y
 * configuraciones— y garantiza que el valor llega al cliente ya como entero,
 * listo para ser formateado sin ambigüedad. Coherentemente, la lectura se
 * realiza con `as<int64_t>()`, que empareja con el tipo `bigint` del lado
 * del servidor.
 *
 * @par Verificación explícita de errores de E/S
 * Tras escribir todas las filas, la función fuerza el vaciado del búfer con
 * `flush()` y comprueba el estado del stream con `fail()`. Esta verificación
 * es necesaria porque `std::ofstream` **no** informa de errores de escritura
 * a menos que el llamador los consulte explícitamente: los fallos de disco
 * (disco lleno, cuota excedida, error de I/O del sistema de archivos) se
 * almacenan silenciosamente en el estado interno del stream y no se
 * manifiestan hasta el cierre o hasta una comprobación explícita. Sin esta
 * verificación, la función podría retornar éxito habiendo escrito un archivo
 * truncado o incompleto.
 *
 * @par Comportamiento de sobrescritura
 * La apertura usa `std::ios::trunc`, que trunca el archivo si ya existía. No
 * se realiza copia de seguridad ni escritura atómica mediante archivo
 * temporal. Si la operación falla a mitad de escritura, el archivo en disco
 * queda en estado inconsistente sin posibilidad de rollback.
 *
 * @par Política de errores
 * A diferencia de `guardar()`, esta función captura `pqxx::data_exception` y,
 * después, cualquier `std::exception`, y las envuelve en `Error` con un
 * mensaje unificado.
 * El efecto es que el llamador ve un único tipo de excepción para cualquier
 * fallo, a costa de perder el tipo original y la información estructurada
 * (SQLSTATE, código de error del servidor).
 *
 * @throws Error En los siguientes escenarios:
 *         - No hay conexión activa en el momento de la invocación.
 *         - El archivo no puede abrirse para escritura.
 *         - La operación de base de datos falla (envuelta desde
 *           cualquier excepción de libpqxx).
 *         - Ocurre cualquier otra excepción no prevista derivada de
 *           `std::exception` durante la ejecución del bloque `try`.
 *         - La verificación explícita de E/S detecta un fallo de escritura
 *           o de vaciado del búfer.
 *
 * @warning La operación no es transaccional desde el punto de vista del
 *          archivo en disco: la conexión sí se gestiona dentro de una
 *          transacción pqxx, pero el archivo no participa de esa transacción.
 *          Un fallo a mitad de escritura deja el archivo en estado
 *          inconsistente sin posibilidad de rollback.
 *
 * @warning El bloque `catch (const std::exception &e)` captura también la
 *          propia `Error` lanzada por la verificación de `flush()`, porque
 *          `Error` deriva de `std::exception`. El efecto observable es que
 *          el mensaje de error de E/S llega al llamador **envuelto** dentro
 *          del mensaje genérico "Error inesperado durante la exportación a
 *          CSV: ...". El comportamiento es funcionalmente correcto —la
 *          excepción se propaga y el llamador puede manejarla— pero el
 *          texto queda duplicado. Si se desea evitar esta duplicación,
 *          añadir un `catch (const Error &) { throw; }` antes del bloque
 *          `catch (const std::exception &)`.
 *
 * @note El archivo no se cierra explícitamente antes de retornar. El
 *       destructor de `std::ofstream` se encarga del cierre por RAII, y
 *       como el `flush()` se ha verificado explícitamente, los datos
 *       escritos están garantizadamente en el búfer del sistema operativo
 *       antes de que la función retorne.
 */
void Db::exportar(const std::string &ruta_archivo) {
    // 1. Validación de precondiciones (fail-fast). Comprobar la conexión
    //    antes de abrir el archivo evita crear un archivo vacío cuando el
    //    fallo va a producirse igualmente en el primer acceso a la base.
    if (!conexion_.is_open()) {
        throw Error("No hay conexión a la base de datos activa al intentar exportar.");
    }

    // 2. Apertura del archivo de salida. std::ios::trunc asegura que se
    //    sobrescriba si el archivo ya existe, coherente con la semántica
    //    documentada de "regenerar el archivo en cada exportación".
    std::ofstream archivo_csv(ruta_archivo, std::ios::out | std::ios::trunc);
    if (!archivo_csv.is_open()) {
        throw Error("No se pudo abrir el archivo para escritura: " + ruta_archivo);
    }

    try {
        // 3. Ejecución de la consulta de solo lectura. nontransaction evita
        //    el BEGIN/COMMIT explícito: la consulta es de lectura pura y no
        //    necesita atomicidad multi-sentencia.
        pqxx::nontransaction transaccion(conexion_);

        const std::string consulta_sql =
                "SELECT uf_date AS fecha, CAST(uf_value AS bigint) AS valor "
                "FROM ufs ORDER BY uf_date ASC;";

        const pqxx::result resultado = transaccion.exec(consulta_sql);

        // 4. Escritura de la cabecera. Formato CSV con delimitador ';' para
        //    compatibilidad regional con Excel en configuraciones donde la
        //    coma es el separador decimal. Los nombres van entre comillas
        //    dobles siguiendo RFC 4180.
        archivo_csv << "\"FECHA\";\"VALOR\"\n";

        // 5. Iteración y escritura de filas con tipos explícitos. La
        //    proyección CAST(... AS bigint) de la consulta empareja con
        //    int64_t, evitando depender de la interpretación de tipos
        //    numéricos de libpqxx.
        for (const pqxx::row &fila: resultado) {
            const std::string fecha_uf = fila["fecha"].as<std::string>();
            const int64_t valor_uf = fila["valor"].as<int64_t>();

            archivo_csv << "\"" << fecha_uf << "\";\"" << valor_uf << "\"\n";
        }

        // 6. Verificación explícita de errores de E/S antes de que el
        //    destructor cierre el archivo. std::ofstream no señala errores
        //    de escritura a menos que se consulten explícitamente; sin esta
        //    comprobación, un fallo de disco (disco lleno, cuota excedida)
        //    pasaría desapercibido y la función retornaría éxito con un
        //    archivo truncado.
        //
        //    NOTA: la expresión usa '||' con efecto de cortocircuito: si
        //    flush() falla, no se evalúa fail(), evitando una comprobación
        //    redundante sobre un stream ya sabido en mal estado.
        if (!archivo_csv.flush() || archivo_csv.fail()) {
            throw Error("Error de E/S al escribir o vaciar el buffer del archivo CSV: " + ruta_archivo);
        }
    } catch (const pqxx::data_exception &e) {
        // Traducción de excepción: se unifica el contrato de error del
        // módulo envolviendo cualquier excepción de libpqxx en Error.
        //
        // CONTRASTE CON guardar(): allí las excepciones de pqxx se propagan
        // sin envolver.
        throw Error("Fallo en la operación de base de datos durante la exportación: " + std::string(e.what()));
    } catch (const std::exception &e) {
        // Captura de red de seguridad para cualquier std::exception no
        // derivada de pqxx::exception. Incluye —entre otras— la propia
        // Error lanzada por la verificación de flush(), que se re-envuelve
        // con un mensaje genérico. Ver el @warning de la documentación.
        throw Error("Error inesperado durante la exportación a CSV: " + std::string(e.what()));
    }

    // NOTA: No se llama a archivo_csv.close() explícitamente. El destructor
    // de std::ofstream se encarga del cierre por RAII. Como el flush() se
    // verificó explícitamente en el paso 6, los datos están garantizadamente
    // en el búfer del sistema operativo antes del retorno, y el cierre
    // implícito solo añade la liberación del descriptor de archivo.
}
