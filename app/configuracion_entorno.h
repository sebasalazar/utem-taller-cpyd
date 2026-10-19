// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// configuracion_entorno.h
#ifndef APP_CONFIGURACION_ENTORNO_H
#define APP_CONFIGURACION_ENTORNO_H

#include <string>
#include <unordered_map>

/**
 * @brief Gestor de configuración inmutable basado en archivos de entorno (formato .env).
 *
 * @details
 * Esta clase carga las variables de entorno desde un archivo de texto plano en el
 * momento de su instanciación y, a partir de ese punto, expone exclusivamente una
 * interfaz de consulta. El diseño sigue un patrón *load-once, read-many*: toda la
 * complejidad —acceso a disco, parseo, normalización— queda confinada al
 * constructor, de modo que los métodos de consulta son operaciones puras sobre
 * una estructura en memoria.
 *
 * @par Inmutabilidad como decisión de diseño
 * No existe ningún método modificador (`set`, `update`, `reload`). Esta ausencia
 * es deliberada y no una limitación pendiente: una configuración que cambia
 * durante la ejecución es una fuente estructural de condiciones de carrera, de
 * comportamientos no reproducibles y de fallos dependientes del orden de
 * ejecución. Si se necesita recargar la configuración, la política recomendada
 * es destruir la instancia actual y construir una nueva en un punto controlado
 * del ciclo de vida de la aplicación (por ejemplo, entre iteraciones de un bucle
 * principal, nunca en mitad de un cómputo paralelo).
 *
 * @par Concurrencia (thread-safety)
 * Al carecer de métodos modificadores y de miembros mutables, las instancias de
 * esta clase son **completamente seguras para lecturas concurrentes** desde
 * múltiples hilos sin ningún mecanismo de sincronización: no hacen falta
 * `std::mutex`, `std::shared_mutex` ni estructuras atómicas. Esta garantía es
 * la que habilita el patrón habitual en aplicaciones OpenMP de construir una
 * única instancia antes de la región paralela y consultarla desde todos los
 * hilos:
 *
 * @code
 *   ConfiguracionEntorno config("app.env");   // construcción monohilo
 *   #pragma omp parallel for
 *   for (int i = 0; i < n; ++i) {
 *       const auto endpoint = config.obtener("API_ENDPOINT"); // lectura concurrente segura
 *       // ...
 *   }
 * @endcode
 *
 * @warning La garantía de thread-safety cubre únicamente **lecturas concurrentes
 *          sobre una instancia ya construida**. La construcción de la instancia
 *          no es segura frente a escrituras del sistema de archivos concurrentes
 *          ni frente a la destrucción simultánea desde otro hilo.
 *
 * @par Formato de archivo esperado
 * El parser acepta un subconjunto deliberadamente conservador de la sintaxis
 * `.env`, suficiente para la mayoría de configuraciones de servicio pero exento
 * de las ambigüedades de las implementaciones más permisivas:
 *
 *   - **Entradas**: líneas con el patrón `CLAVE=VALOR`. La clave se toma tal cual,
 *     sin normalización de mayúsculas ni *trimming* implícito salvo el que exija
 *     la implementación de cargarArchivo().
 *   - **Comentarios**: cualquier línea cuyo primer carácter no blanco sea `#`
 *     se descarta íntegramente. No se admiten comentarios al final de línea.
 *   - **Líneas vacías**: se ignoran sin generar error.
 *   - **Sin interpolación**: secuencias como `$OTRA_VARIABLE` o `${OTRA_VARIABLE}`
 *     **no se resuelven**. El valor se almacena literalmente. Esta decisión es
 *     intencional: la interpolación introduce dependencias de orden entre claves,
 *     dificulta el razonamiento sobre el contenido final y complica la detección
 *     de ciclos. Si se requiere composición de valores, debe hacerse en el código
 *     de la aplicación, no en el formato de configuración.
 *
 * @par Contrato de errores
 * La clase distingue dos categorías de fallo y las trata de forma coherente:
 *
 *   1. **Fallos en construcción** — archivo inexistente, sin permisos de lectura,
 *      error de I/O durante el parseo. Se traducen en una excepción Error desde
 *      el constructor, de modo que es imposible obtener una instancia en estado
 *      "a medio cargar".
 *
 *   2. **Fallos en consulta** — solicitud de una clave inexistente mediante la
 *      sobrecarga estricta de obtener(). Se traduce también en Error, aplicando
 *      un patrón *fail-fast* que expone los errores de configuración en el punto
 *      donde ocurren, no como valores vacíos propagados silenciosamente.
 *
 * @note La sobrecarga de obtener() con valor por defecto existe precisamente
 *       para los casos en los que la ausencia de una clave es un escenario
 *       legítimo y esperado; ver su documentación individual.
 *
 * @see Error
 */
class ConfiguracionEntorno {
public:
    /**
     * @brief Construye el gestor y carga el archivo de configuración en memoria.
     *
     * @details
     * Toda la actividad de I/O y de parseo ocurre aquí. Tras el retorno exitoso
     * del constructor, la instancia no volverá a tocar el disco ni el sistema de
     * archivos: es un objeto puramente en memoria. Esta separación estricta entre
     * fase de carga y fase de uso es lo que hace trivial razonar sobre su
     * comportamiento en entornos concurrentes.
     *
     * @param ruta_archivo Ruta absoluta o relativa al archivo de configuración
     *                     (`.env`). La ruta se interpreta según las reglas del
     *                     sistema operativo subyacente; no se realiza ninguna
     *                     canonicalización previa.
     *
     * @throws Error Si el archivo no puede ser abierto o leído por cualquier
     *               motivo: no existe, permisos insuficientes, es un directorio,
     *               fallo de I/O a mitad de lectura, etc. El mensaje de la
     *               excepción debe incluir la ruta para facilitar el diagnóstico.
     *
     * @warning El constructor no es `noexcept`: en particular, puede lanzar
     *          Error por causas de I/O y `std::bad_alloc` por causas de memoria
     *          si el archivo es anormalmente grande.
     *
     * @note El constructor está marcado `explicit` para prevenir conversiones
     *       implícitas desde `std::string` o `const char*`, que producirían
     *       cargas accidentales de archivos en contextos donde solo se espera
     *       una cadena.
     */
    explicit ConfiguracionEntorno(const std::string &ruta_archivo);

    /**
     * @brief Obtiene el valor de una variable de entorno, con semántica *fail-fast*.
     *
     * @details
     * Esta sobrecarga representa la política estricta: la ausencia de la clave
     * solicitada se considera un error de configuración y se manifiesta como
     * excepción inmediata. Es la sobrecarga preferida para claves que la
     * aplicación **necesita** para funcionar (URLs de servicio, credenciales,
     * rutas de recursos obligatorios) y cuyo silencioso valor vacío provocaría
     * fallos más difíciles de diagnosticar aguas abajo.
     *
     * @param clave Nombre de la variable a buscar. La búsqueda es sensible a
     *              mayúsculas y minúsculas, coherente con la ausencia de
     *              normalización durante el parseo.
     *
     * @return std::string El valor asociado a la clave, tal como aparece en el
     *         archivo. Puede ser una cadena vacía si el archivo contenía
     *         `CLAVE=` explícitamente; cadena vacía y clave ausente son
     *         escenarios distintos y esta sobrecarga solo falla en el segundo.
     *
     * @throws Error Si la clave no existe en la configuración cargada. El
     *               mensaje debe incluir el nombre de la clave ausente.
     *
     * @note El atributo `[[nodiscard]]` previene el error silencioso de invocar
     *       esta función sin usar su resultado, lo cual casi siempre indica que
     *       se pretendía otra cosa.
     *
     * @see obtener(const std::string&, const std::string&) para la variante
     *      tolerante a claves ausentes.
     */
    [[nodiscard]] std::string obtener(const std::string &clave) const;

    /**
     * @brief Obtiene el valor de una variable de entorno, con valor de respaldo.
     *
     * @details
     * Esta sobrecarga representa la política tolerante: la ausencia de la clave
     * no es un error, sino un escenario previsto que se resuelve devolviendo el
     * valor por defecto proporcionado por el llamador. Es la sobrecarga adecuada
     * para parámetros opcionales con un valor sensato predefinido (timeouts,
     * niveles de log, flags de comportamiento).
     *
     * @param clave Nombre de la variable a buscar.
     * @param valor_por_defecto Valor a retornar si la clave no existe. Se pasa
     *                          por valor y se copia al resultado; para cadenas
     *                          literales cortas el coste es despreciable.
     *
     * @return std::string El valor asociado a la clave si existe, o
     *         `valor_por_defecto` en caso contrario.
     *
     * @note Esta sobrecarga **no lanza excepciones** por clave ausente. Puede,
     *       en cambio, lanzar `std::bad_alloc` si la copia del valor (real o
     *       por defecto) agota la memoria disponible.
     *
     * @warning No confundir "clave ausente" con "clave presente con valor
     *          vacío". Si el archivo contiene `CLAVE=`, el valor retornado será
     *          la cadena vacía, no `valor_por_defecto`. Si la distinción es
     *          relevante para la lógica de la aplicación, consultar previamente
     *          mediante contiene().
     */
    [[nodiscard]] std::string obtener(const std::string &clave, const std::string &valor_por_defecto) const;

    /**
     * @brief Verifica la existencia de una clave en la configuración.
     *
     * @details
     * Consulta puramente estructural: comprueba si la clave está presente en el
     * mapa interno, independientemente de cuál sea su valor asociado. Es la
     * herramienta adecuada cuando el código necesita distinguir los tres
     * escenarios posibles —ausente, presente con valor vacío, presente con
     * valor no vacío— y no puede permitirse la ambigüedad que introduce la
     * sobrecarga con valor por defecto.
     *
     * @param clave Nombre de la variable a verificar.
     *
     * @return `true` si la clave existe en la configuración cargada, `false` en
     *         caso contrario.
     *
     * @note Esta función nunca lanza excepciones derivadas de la ausencia de la
     *       clave: la ausencia es precisamente la información que se está
     *       consultando.
     *
     * @see obtener(const std::string&, const std::string&) para el caso en que
     *      la ausencia deba resolverse con un valor por defecto.
     */
    [[nodiscard]] bool contiene(const std::string &clave) const;

private:
    /**
     * @brief Parsea el archivo y puebla el mapa interno de variables.
     *
     * @details
     * Este método concentra toda la lógica de parseo y es invocado exclusivamente
     * desde el constructor. Se separa de él por claridad estructural, no porque
     * se espere reutilización externa. Aplica las reglas descritas en la
     * documentación de la clase: descarte de líneas vacías y de comentarios,
     * reconocimiento del patrón `CLAVE=VALOR` y almacenamiento literal del valor
     * sin interpolación.
     *
     * @param ruta_archivo Ruta al archivo a procesar. Se asume ya resuelta por
     *                     el llamador; este método no realiza canonicalización.
     *
     * @throws Error Si ocurre cualquier error de I/O durante la lectura: fallo
     *               de apertura, fallo de lectura a mitad de archivo, etc.
     *
     * @pre El mapa interno `variables_` debe estar vacío o en un estado tal que
     *      una nueva carga no deje entradas obsoletas. En la práctica esto se
     *      garantiza por construcción, ya que el constructor invoca este método
     *      una única vez sobre una instancia recién creada.
     *
     * @post Al retorno exitoso, `variables_` contiene todas las claves válidas
     *       del archivo y ninguna otra. Las claves duplicadas, si las hubiera,
     *       se resuelven según la implementación (típicamente la última gana),
     *       comportamiento que debe documentarse en el .cpp si se desea
     *       garantizar contrato.
     */
    void cargarArchivo(const std::string &ruta_archivo);

    // Almacenamiento interno. Se prefiere unordered_map sobre map para garantizar
    // complejidad O(1) promedio en las búsquedas, optimizando el rendimiento en
    // aplicaciones que consultan la configuración frecuentemente.
    //
    // NOTA DE MANTENIMIENTO: El orden de iteración de unordered_map no está
    // definido y puede variar entre ejecuciones, entre plataformas y entre
    // versiones de la biblioteca estándar. Cualquier código que dependa del
    // orden de las claves (por ejemplo, para generar un informe determinista)
    // debe recopilarlas en un contenedor ordenado antes de iterarlas.
    //
    // NOTA DE RENDIMIENTO: El coste O(1) promedio asume una función de hash de
    // std::string de calidad razonable, lo cual se cumple en las
    // implementaciones estándar actuales. En el peor caso teórico (colisiones
    // adversarias) el coste degenera a O(n), escenario que no se considera
    // relevante porque las claves son controladas por el operador del servicio
    // y no por un atacante externo.
    std::unordered_map<std::string, std::string> variables_;
};

#endif // APP_CONFIGURACION_ENTORNO_H
