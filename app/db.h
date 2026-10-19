// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

//
// Created by seba on 11-10-26.
//

#ifndef APP_DB_H
#define APP_DB_H

#include <string>

// Inclusión diferida: <pqxx/pqxx> se incluye aquí porque pqxx::connection
// es un miembro por valor, lo cual es correcto y necesario para la RAII.
#include <pqxx/pqxx>

#include "uf.h"

/**
 * @brief Gestor de persistencia y exportación de datos de UF hacia PostgreSQL.
 *
 * @details
 * Esta clase encapsula una conexión única a una base de datos PostgreSQL
 * utilizando la biblioteca libpqxx. Implementa el patrón RAII de forma
 * estricta: la conexión se establece en el constructor y se cierra
 * automáticamente al destruirse la instancia, sin intervención del llamador
 * y sin posibilidad de olvidar el cierre en alguna ruta de salida.
 *
 * @par Modelo de propiedad
 * La clase posee **una y solo una** conexión, almacenada como miembro por
 * valor (`pqxx::connection`). Esta decisión tiene consecuencias que conviene
 * tener presentes:
 *
 *   - **No es copiable.** `pqxx::connection` no define constructor de copia;
 *     por transitividad, `Db` tampoco. Intentar copiar una instancia produce
 *     un error de compilación, que es el comportamiento correcto: dos
 *     objetos con la misma conexión subyacente darían lugar a dobles cierres
 *     o a usos después del cierre.
 *   - **No es movible.** `pqxx::connection` tampoco define constructor de
 *     movimiento en versiones recientes de libpqxx, porque su semántica de
 *     cierre en el destructor haría ambiguo el estado del objeto movido.
 *     Como consecuencia, `Db` debe construirse *in situ*, no puede
 *     almacenarse en contenedores que requieran movimiento, y no puede
 *     devolverse por valor desde una función factoría sin envolverlo en
 *     `std::unique_ptr` o similar.
 *   - **El ciclo de vida de la conexión coincide con el de `Db`.** No hay
 *     un método `abrir()` ni `cerrar()` explícitos; el diseño delega el
 *     control al alcance léxico del objeto, que es la forma más robusta de
 *     gestionar recursos en C++.
 *
 * @par Concurrencia (thread-safety)
 * Las instancias de esta clase **no** son seguras para hilos (not
 * thread-safe). Una instancia de `Db` debe ser utilizada exclusivamente por
 * un único hilo de ejecución durante toda su vida útil.
 *
 * La razón no es una decisión de diseño arbitraria: `pqxx::connection`
 * mantiene un estado interno asociado a la sesión (transacciones en curso,
 * cursores abiertos, tipado de parámetros preparados) que no está protegido
 * por mutex. Compartir una conexión entre hilos produciría condiciones de
 * carrera en ese estado interno, con errores que van desde mensajes de
 * protocolo confusos hasta caídas del proceso.
 *
 * @par Patrón recomendado para procesamiento paralelo
 * Para escenarios con OpenMP u otro modelo de paralelismo, se debe instanciar
 * un objeto `Db` **independiente** por cada hilo (patrón *thread-local
 * connection*). Este patrón, además de eliminar las condiciones de carrera,
 * tiene la propiedad deseable de no agotar el pool de conexiones del
 * servidor:
 *
 * @code
 *   #pragma omp parallel
 *   {
 *       // Cada hilo construye su propia conexión al entrar en la región
 *       // paralela y la libera al salir. El coste de establecimiento de
 *       // conexión se amortiza durante la vida del hilo.
 *       Db db(cadena_conexion);
 *       #pragma omp for
 *       for (int i = 0; i < n; ++i) {
 *           db.guardar(datos[i]);
 *       }
 *   }
 * @endcode
 *
 * @warning El número de hilos activos debe dimensionarse teniendo en cuenta
 *          el límite de conexiones del servidor PostgreSQL (`max_connections`
 *          en `postgresql.conf`). Un pool de hilos sobredimensionado agotará
 *          las conexiones disponibles y los hilos sobrantes fallarán al
 *          construir su `Db` con un error de conexión rechazada. Como regla
 *          general, `n_hilos <= max_connections - conexiones_reservadas`.
 *
 * @par Contrato de errores
 * Los fallos pueden originarse en dos capas distintas, y la clase no intenta
 * unificarlos bajo un único tipo de excepción:
 *
 *   1. **Fallos propios de la clase** —conexión no activa, archivo CSV no
 *      escribible— se traducen en `Error`, la excepción personalizada del
 *      proyecto.
 *   2. **Fallos propagados de libpqxx** —errores de sintaxis SQL, violación
 *      de restricciones, caída de la conexión a mitad de operación— se
 *      propagan sin envolver en el constructor y en `guardar()`, conservando
 *      su tipo original (`pqxx::sql_error`, `pqxx::broken_connection`, etc.).
 *      La excepción es `exportar()`, que envuelve también estos errores en
 *      `Error` (ver su documentación).
 *
 * La decisión de no envolver es deliberada en las rutas de escritura: la
 * jerarquía de excepciones de libpqxx transporta información valiosa (código
 * SQLSTATE, consulta que falló, detalle del servidor) que se perdería al
 * aplastarla dentro de un `Error` genérico. El llamador que necesite
 * uniformar el tratamiento puede capturar `std::exception` en la frontera de
 * su módulo.
 *
 * @par Inclusión de <pqxx/pqxx> en la cabecera
 * La cabecera incluye `<pqxx/pqxx>` aunque sea una dependencia pesada. La
 * alternativa —declaración adelantada de `pqxx::connection` y puntero— se
 * descartó porque rompería la RAII directa y obligaría a gestionar
 * manualmente la liberación en el destructor, exactamente el tipo de carga
 * que el diseño pretende eliminar. El coste de compilación añadido se acepta
 * como precio de la corrección.
 *
 * @see Uf
 * @see Error
 */
class Db {
public:
    /**
     * @brief Construye el gestor y establece la conexión con la base de datos.
     *
     * @details
     * La conexión se establece de forma **síncrona y bloqueante** durante la
     * construcción. Si el servidor no responde, la red está caída o las
     * credenciales son rechazadas, el constructor lanza y no se obtiene
     * ninguna instancia utilizable. Este comportamiento es coherente con el
     * principio de que un objeto `Db` en existencia es, por definición, un
     * objeto con conexión activa: no hay estado intermedio "construido pero
     * sin conectar" que el llamador deba comprobar.
     *
     * @param cadena_conexion Cadena de conexión en formato libpq, del estilo
     *                        `"host=localhost port=5432 dbname=uf_db user=admin password=secret"`.
     *                        Los parámetros aceptados son los de libpq;
     *                        ver la documentación oficial de PostgreSQL para
     *                        la lista completa y sus valores por defecto.
     *
     * @throws pqxx::broken_connection Si el servidor rechaza la conexión por
     *               credenciales incorrectas, base de datos inexistente,
     *               límite de conexiones alcanzado, o cualquier otro fallo
     *               de establecimiento. La excepción se propaga sin envolver
     *               para preservar el código de error y el mensaje del
     *               servidor.
     *
     * @note El constructor está marcado `explicit` para prevenir conversiones
     *       implícitas desde `std::string` o `const char*`. Sin este marcador,
     *       una llamada como `Db db = "host=...";` compilaría y establecería
     *       una conexión que el lector del código podría confundir con una
     *       simple asignación.
     *
     * @warning No capturar la excepción y continuar con una instancia a medio
     *          construir: si el constructor lanza, el objeto no existe y no
     *          hay ninguna conexión que cerrar ni ningún estado que limpiar.
     *          El bloque `try` debe envolver la construcción completa.
     */
    explicit Db(const std::string &cadena_conexion);

    /**
     * @brief Persiste un registro de UF en la base de datos mediante UPSERT.
     *
     * @details
     * La operación implementada es un *UPSERT* (insert-or-update): si ya
     * existe una fila para la fecha del registro de UF, sus valores se
     * actualizan; en caso contrario, se inserta una fila nueva. Esta
     * semántica es idempotente por construcción y permite reejecutar el
     * proceso de ingesta sin duplicar registros ni requerir un paso previo
     * de limpieza.
     *
     * La operación se ejecuta dentro de una transacción explícita
     * (`pqxx::work`) que garantiza atomicidad. El destructor de la
     * transacción realiza `rollback` automático si una excepción escapa del
     * cuerpo, de modo que no hay transacciones colgadas que bloqueen filas
     * hasta el timeout del servidor.
     *
     * En PostgreSQL, la operación se materializa con la cláusula
     * `INSERT ... ON CONFLICT (uf_date) DO UPDATE SET ...`, apoyándose en una
     * restricción de unicidad sobre la columna `uf_date`. La existencia de
     * esa restricción es una **precondición del esquema** que la clase no
     * verifica en tiempo de ejecución: si falta, el UPSERT falla con un error
     * de PostgreSQL, lo cual es preferible a un comportamiento silenciosamente
     * incorrecto.
     *
     * @param uf Instancia de Uf que contiene los datos a persistir. Su fecha
     *           actúa como clave de conflicto; el resto de campos son los
     *           que se actualizan o insertan.
     *
     * @throws Error Si no hay conexión activa en el momento de la
     *               invocación. Este caso no debería producirse bajo uso
     *               correcto —el constructor garantiza una conexión activa—
     *               pero la comprobación se mantiene como salvaguarda contra
     *               un estado inesperado de libpqxx.
     * @throws pqxx::sql_error Si la sentencia SQL es rechazada por el
     *               servidor: violación de restricción, tipo incompatible,
     *               columna inexistente, etc. La excepción transporta el
     *               código SQLSTATE y el texto de la consulta.
     * @throws pqxx::broken_connection Si la conexión se pierde durante la
     *               operación (caída del servidor, timeout de red, cierre
     *               del socket por el firewall). Tras esta excepción, la
     *               instancia `Db` queda inutilizable y debe descartarse.
     *
     * @note La operación es transaccional a nivel de sentencia. Si el
     *       llamador necesita agrupar varios `guardar` en una única
     *       transacción, debe abrir una explícitamente mediante
     *       `pqxx::work` —lo cual requiere acceso a la conexión subyacente
     *       que esta clase no expone— o ampliar la interfaz pública con un
     *       método que acepte un rango de registros.
     *
     * @warning Esta función **no** es segura frente a invocaciones
     *          concurrentes sobre la misma instancia. Ver la sección de
     *          concurrencia en la documentación de la clase.
     */
    void guardar(const Uf &uf);

    /**
     * @brief Exporta el historial completo de UF almacenado a un archivo CSV.
     *
     * @details
     * Genera un archivo CSV con el contenido íntegro de la tabla de UF en el
     * momento de la llamada. La operación es **bloqueante** y su duración es
     * proporcional al número de filas de la tabla y al ancho de banda entre
     * el cliente y el servidor: para tablas con millones de filas, conviene
     * invocarla desde un hilo dedicado o desde un proceso batch, no desde el
     * camino crítico de una petición interactiva.
     *
     * @par Formato del archivo
     * El archivo se escribe en UTF-8, con codificación de final de línea
     * nativa de la plataforma (`\n` en sistemas tipo Unix) y **punto y coma**
     * (`;`) como separador en lugar de la coma estándar de RFC 4180. La
     * elección de `;` responde a la compatibilidad con Excel en
     * configuraciones regionales donde la coma es el separador decimal: un
     * CSV con comas sería parseado incorrectamente por Excel en esas
     * configuraciones.
     *
     * Los nombres de columna y los valores van entre comillas dobles,
     * siguiendo la convención que permite que valores con el separador o con
     * saltos de línea internos se interpreten correctamente por consumidores
     * conformes a RFC 4180.
     *
     * @par Comportamiento de sobrescritura
     * Si el archivo de destino ya existe, se sobrescribe sin advertencia ni
     * copia de seguridad. La clase no intenta hacer copia atómica (escribir
     * a un archivo temporal y renombrar al finalizar); si la operación falla
     * a mitad de escritura, el archivo puede quedar en un estado parcialmente
     * escrito. El llamador que necesite atomicidad debe escribir a una ruta
     * temporal y renombrar explícitamente tras el éxito.
     *
     * @param ruta_archivo Ruta absoluta o relativa donde se generará el
     *                     archivo CSV. La ruta se interpreta según las reglas
     *                     del sistema operativo subyacente; los directorios
     *                     intermedios deben existir previamente.
     *
     * @throws Error Si no hay conexión activa en el momento de la
     *               invocación; si el archivo no puede abrirse para
     *               escritura (directorio inexistente, permisos
     *               insuficientes, ruta apuntando a un directorio, etc.);
     *               o si falla la consulta o la escritura del archivo. Los
     *               errores de libpqxx (`pqxx::sql_error`,
     *               `pqxx::broken_connection`, etc.) se envuelven en `Error`
     *               conservando su mensaje original.
     *
     * @warning La operación **no** es transaccional desde el punto de vista
     *          del archivo resultante: la conexión sí se gestiona dentro de
     *          una transacción pqxx, pero el archivo en disco no participa
     *          de esa transacción. Un fallo a mitad de escritura deja el
     *          archivo en estado inconsistente sin posibilidad de rollback.
     *
     * @note Esta función **no** es segura frente a invocaciones concurrentes
     *       sobre la misma instancia. Ver la sección de concurrencia en la
     *       documentación de la clase.
     */
    void exportar(const std::string &ruta_archivo);

private:
    /**
     * @brief Conexión activa a la base de datos.
     *
     * @details
     * Se mantiene por valor para garantizar la propiedad exclusiva y el
     * cierre automático (RAII) al finalizar el ciclo de vida de la instancia.
     * Esta decisión condiciona el diseño completo de la clase y merece ser
     * entendida antes de modificarla:
     *
     *   - **Propiedad exclusiva**: no hay dos objetos `Db` que puedan
     *     compartir la misma conexión. La relación es 1-a-1 por construcción.
     *   - **Cierre automático**: el destructor de `pqxx::connection` se
     *     encarga de cerrar limpiamente al destruirse el `Db`. No hay
     *     destructor explícito en esta clase porque no se necesita.
     *   - **Orden de destrucción**: como único miembro, `conexion_` se
     *     destruye tras el cuerpo del destructor implícito de `Db`. Si en el
     *     futuro se añadieran otros miembros que dependan de la conexión,
     *     deben declararse **antes** que `conexion_` para que se destruyan
     *     después, respetando así el orden inverso de construcción.
     *
     * @warning El miembro es privado y no se expone ninguna referencia
     *          pública a él. Esto es intencional: cualquier acceso directo
     *          permitiría al llamador saltarse la política de errores de la
     *          clase y dejar la conexión en un estado que los métodos
     *          públicos asumen válido. Si en el futuro se necesitara acceso
     *          para transacciones multi-sentencia, debe hacerse mediante un
     *          método público que mantenga las invariantes, no exponiendo la
     *          conexión desnuda.
     *
     * @note El nombre `conexion_` sigue la convención del proyecto de sufijar
     *       con guion bajo los miembros privados, lo cual permite
     *       distinguirlos de variables locales y parámetros sin necesidad de
     *       prefijos más verbosos.
     */
    pqxx::connection conexion_;
};

#endif // APP_DB_H
