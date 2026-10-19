// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// configuracion_entorno.cpp
#include "configuracion_entorno.h"
#include "utem.h"      // Inclusión explícita de las utilidades de cadena
#include "error.h"     // Inclusión explícita de la excepción personalizada

#include <fstream>
#include <string>


namespace {
    /**
     * @brief Construye el mensaje de error de sintaxis para una línea concreta.
     *
     * @details
     * Todas las rutas de error del parser comparten la misma estructura de
     * mensaje: descripción del problema, número de línea y contenido literal
     * de la línea problemática entre comillas. Extraer la construcción a este
     * helper evita que la fórmula se desincronice entre las distintas rutas a
     * medida que se añadan nuevas validaciones.
     *
     * @param descripcion Qué ha fallado, en términos del contrato (por
     *                    ejemplo: "falta el caracter '='").
     * @param numero_linea Número de línea (1-based) donde se detectó el
     *                     problema.
     * @param linea_contenido Contenido **original** de la línea, sin recortar.
     *                        Se usa el original y no la versión limpia porque
     *                        el operador que depura el archivo necesita ver
     *                        exactamente lo que escribió, incluidos los
     *                        espacios en blanco que el parser ya ha descartado.
     *
     * @return Cadena lista para pasar al constructor de Error.
     *
     * @note El helper no es `noexcept` porque construye varias cadenas y
     *       `std::to_string` puede lanzar `std::bad_alloc` en casos extremos.
     *       Propagar esa excepción es correcto: coincide con la política del
     *       parser, que ya lanza Error en cualquier otra anomalía.
     */
    std::string mensajeErrorSintaxis(const std::string &descripcion,
                                     std::size_t numero_linea,
                                     const std::string &linea_contenido) {
        return "Error de sintaxis en el archivo de configuración (línea " +
               std::to_string(numero_linea) + "): " + descripcion +
               " en '" + linea_contenido + "'";
    }
} // namespace

/**
 * @brief Construye el gestor delegando toda la carga al método privado.
 *
 * @details
 * El constructor no contiene lógica propia más allá de la delegación. Se
 * mantiene esta separación porque `cargarArchivo` es el punto donde se
 * concentran todas las rutas de error y toda la actividad de I/O; aislarlo
 * facilita la lectura del ciclo de vida de la clase (construir = cargar) y
 * deja abierta la posibilidad de invocarlo desde otras rutas internas si en
 * el futuro se añadiera, por ejemplo, un mecanismo de recarga controlada.
 *
 * @throws Error Propagada desde `cargarArchivo` sin transformación. El
 *               constructor no añade contexto adicional porque el mensaje
 *               original ya incluye la ruta o el número de línea según el
 *               tipo de fallo.
 */
ConfiguracionEntorno::ConfiguracionEntorno(const std::string &ruta_archivo) {
    cargarArchivo(ruta_archivo);
}

/**
 * @brief Carga el archivo `.env` línea a línea y puebla el mapa interno.
 *
 * @details
 * El parser es intencionadamente minimalista: no admite interpolación, no
 * admite comentarios al final de línea, no admite sintaxis alternativa para
 * asignación. Cada línea se procesa de forma independiente, y cualquier
 * anomalía estructural —línea sin '=', clave vacía— aborta la carga con
 * Error, sin dejar la instancia en un estado parcialmente poblado.
 *
 * @par Orden de las operaciones por línea
 * El orden es deliberado y no debe alterarse sin revisar el contrato:
 *
 *   1. **`eliminarEspacios` sobre la línea completa.** Permite que líneas
 *      con indentación (`   CLAVE=VALOR`) o con espacios de relleno al final
 *      se procesen como si estuvieran limpias. El primer carácter tras este
 *      paso es el que decide si la línea es un comentario.
 *   2. **Descarte de líneas vacías y de comentarios.** Se comprueba después
 *      del *trim* porque una línea que solo contiene espacios y un `#` al
 *      principio debe tratarse como comentario, no como línea con contenido.
 *   3. **Localización del '='.** El primer '=' de la línea delimita clave y
 *      valor. Si el valor contiene '=' adicionales (frecuente en URLs con
 *      *query strings*, tokens JWT, cadenas Base64), esos '=' forman parte
 *      del valor y se preservan intactos.
 *   4. **`eliminarEspacios` sobre cada mitad por separado.** Aplica el *trim*
 *      a la clave y al valor de forma independiente. El *trim* sobre la
 *      línea completa del paso 1 no garantiza que las mitades estén limpias:
 *      `CLAVE = VALOR` deja un espacio antes del '=' que va a parar al final
 *      de la clave, y otro después que va al principio del valor.
 *   5. **`eliminarComillas` sobre el valor.** Solo después del *trim*, para
 *      que `"  valor  "` (con comillas envolviendo espacios) preserve los
 *      espacios internos y produzca `  valor  ` sin las comillas.
 *   6. **Validación de clave no vacía.** Una línea como `=VALOR` pasa los
 *      pasos anteriores pero es sintácticamente inválida. La comprobación se
 *      hace aquí y no antes porque requiere que la clave ya esté limpia.
 *   7. **Inserción en el mapa.** Las claves duplicadas se resuelven con
 *      semántica de "última gana": `operator[]` sobrescribe el valor
 *      existente sin lanzar ni advertir. Ver el apartado «Claves duplicadas».
 *
 * @par Comentarios al final de línea
 * El parser **no** reconoce comentarios al final de línea. Una línea como
 * `CLAVE=VALOR # comentario` produce un valor literal `VALOR # comentario`,
 * con el `#` y todo lo que sigue incluidos. Esta decisión es coherente con
 * el formato `.env` canónico y evita la ambigüedad de decidir si un `#`
 * dentro de un valor entrecomillado es un comentario o contenido legítimo.
 *
 * @par Claves duplicadas
 * Si el archivo define la misma clave más de una vez, la última definición
 * prevalece. Este comportamiento **no** se considera un error, porque hay
 * casos de uso legítimos: un archivo base con valores por defecto seguido de
 * una sección de anulación al final, o un archivo generado por concatenación
 * de fragmentos donde el último gana. La decisión es deliberada y está
 * documentada aquí para que el contrato sea explícito; si en el futuro se
 * quisiera convertir en error, esta es la ruta a modificar.
 *
 * @throws Error En los siguientes escenarios:
 *         - El archivo no puede abrirse (no existe, permisos, es un
 *           directorio, etc.).
 *         - Una línea no vacía ni comentario carece de '='.
 *         - Una línea tiene '=' pero la clave resultante, tras *trim*, es
 *           vacía.
 *
 * @pre El archivo debe existir y ser legible. No se verifica el tamaño ni el
 *      contenido antes de abrirlo.
 *
 * @post En caso de retorno exitoso, `variables_` contiene exactamente las
 *       claves válidas del archivo con sus valores procesados (sin espacios
 *       en los extremos, sin comillas envolventes). En caso de excepción, el
 *       objeto queda en el estado en que se encontraba antes de la llamada;
 *       como se invoca desde el constructor, esto significa que la instancia
 *       nunca llega a completarse.
 *
 * @note El número de línea es 1-based para coincidir con la numeración que
 *       muestran los editores de texto, de modo que el mensaje de error se
 *       pueda correlacionar directamente con la posición en el archivo.
 *
 * @warning El parser no distingue entre un fallo de lectura a mitad de
 *          archivo y un fin de archivo limpio: `std::getline` retorna el
 *          stream en estado *fail* en ambos casos, y el bucle termina sin
 *          señalizar la diferencia. Si el archivo se corrompe durante la
 *          lectura (por ejemplo, se trunca desde otro proceso), la carga
 *          puede terminar con un subconjunto de las claves sin que se
 *          detecte el problema. Si este escenario es relevante para el
 *          entorno de despliegue, habría que verificar `archivo.eof()`
 *          tras el bucle.
 */
void ConfiguracionEntorno::cargarArchivo(const std::string &ruta_archivo) {
    std::ifstream archivo(ruta_archivo);
    if (!archivo.is_open()) {
        throw Error("No se pudo abrir el archivo de configuración: " + ruta_archivo);
    }

    std::string linea;
    std::size_t numero_linea = 0;

    while (std::getline(archivo, linea)) {
        ++numero_linea;
        const std::string linea_limpia = utem::eliminarEspacios(linea);

        // Descarte de líneas vacías y de comentarios. Se comprueba después del
        // *trim* para que una línea con solo espacios y un '#' inicial se
        // trate como comentario, no como contenido.
        if (linea_limpia.empty() || linea_limpia[0] == '#') {
            continue;
        }

        // El primer '=' delimita clave y valor. Los '=' adicionales en el
        // valor (tokens JWT, Base64, query strings) se preservan intactos.
        const std::size_t posicion_igual = linea_limpia.find('=');
        if (posicion_igual == std::string::npos) {
            // Fail-fast: rechazar líneas sin '=' en lugar de intentar
            // interpretarlas como clave con valor vacío implícito. Este
            // último comportamiento corrompería silenciosamente archivos
            // mal formados.
            throw Error(mensajeErrorSintaxis("falta el caracter '='",
                                             numero_linea,
                                             linea));
        }

        // *Trim* independiente de cada mitad: la limpieza de la línea completa
        // no garantiza que las mitades estén limpias (ver "Orden de las
        // operaciones" en la documentación del método).
        std::string clave = utem::eliminarEspacios(linea_limpia.substr(0, posicion_igual));
        if (clave.empty()) {
            throw Error(mensajeErrorSintaxis("la clave no puede estar vacía",
                                             numero_linea,
                                             linea));
        }

        std::string valor = utem::eliminarEspacios(linea_limpia.substr(posicion_igual + 1));

        // El despojado de comillas se aplica solo al valor y solo después del
        // *trim*, para que `"  valor  "` preserve los espacios internos.
        valor = utem::eliminarComillas(valor);



        // Semántica de "última gana" ante claves duplicadas: operator[]
        // sobrescribe el valor existente sin lanzar ni advertir. Ver la nota
        // correspondiente en la documentación del método.
        variables_[clave] = valor;
    }
}

/**
 * @brief Recupera el valor de una clave, fallando si no existe.
 *
 * @details
 * Política *fail-fast*: la ausencia de la clave se trata como un error de
 * configuración, no como un valor ausente que deba propagarse como cadena
 * vacía. El llamador que espere este comportamiento debe usar la sobrecarga
 * con valor por defecto.
 *
 * @return Valor asociado a la clave. Puede ser la cadena vacía si el archivo
 *         contenía `CLAVE=` explícitamente; cadena vacía y clave ausente son
 *         escenarios distintos y solo el segundo produce excepción.
 *
 * @throws Error Si la clave no existe en la configuración cargada. El
 *               mensaje incluye el nombre de la clave ausente.
 *
 * @note La implementación usa el iterador directamente en lugar de
 *       `variables_.at(clave)` porque `at` lanza `std::out_of_range`, que no
 *       lleva información sobre qué clave falló y obligaría a envolver la
 *       llamada en un `try` con reconstrucción del mensaje. El patrón
 *       `find` + comprobación de `end()` es más verboso pero produce
 *       mensajes de error más útiles sin coste adicional en el camino
 *       caliente.
 */
std::string ConfiguracionEntorno::obtener(const std::string &clave) const {
    std::unordered_map<std::string, std::string>::const_iterator iterador = variables_.find(clave);

    if (iterador == variables_.end()) {
        throw Error("Variable de entorno no encontrada en la configuración: '" + clave + "'");
    }
    return iterador->second;
}

/**
 * @brief Recupera el valor de una clave, con valor de respaldo si no existe.
 *
 * @details
 * Política tolerante: la ausencia de la clave es un escenario previsto que se
 * resuelve devolviendo `valor_por_defecto`. La función no lanza excepciones
 * por clave ausente; la única excepción plausible es `std::bad_alloc` durante
 * la copia del resultado (real o por defecto).
 *
 * @return Valor asociado a la clave si existe, o `valor_por_defecto` en caso
 *         contrario.
 *
 * @note La lógica es idéntica a la de la sobrecarga estricta, con un único
 *       cambio en la rama de fallo: retorno del valor por defecto en lugar de
 *       excepción. Se mantiene la duplicación deliberadamente —en lugar de
 *       factorizar a un helper común— porque las dos sobrecargas tienen
 *       políticas de error opuestas y fusionarlas añadiría un parámetro de
 *       control que oscurecería la intención de cada llamada.
 *
 * @warning No confundir clave ausente con valor vacío: si el archivo contenía
 *          `CLAVE=`, el retorno será la cadena vacía, no `valor_por_defecto`.
 *          Ver la documentación de la clase en la cabecera.
 */
std::string ConfiguracionEntorno::obtener(const std::string &clave, const std::string &valor_por_defecto) const {
    std::unordered_map<std::string, std::string>::const_iterator iterador = variables_.find(clave);

    if (iterador == variables_.end()) {
        return valor_por_defecto;
    }
    return iterador->second;
}

/**
 * @brief Verifica la presencia estructural de una clave.
 *
 * @details
 * Consulta puramente estructural: comprueba si la clave está presente en el
 * mapa interno, independientemente de cuál sea su valor asociado. Es la
 * herramienta adecuada cuando el código necesita distinguir los tres
 * escenarios posibles —ausente, presente con valor vacío, presente con valor
 * no vacío— y no puede permitirse la ambigüedad de la sobrecarga con valor
 * por defecto.
 *
 * @return `true` si la clave existe en la configuración cargada, `false` en
 *         caso contrario.
 *
 * @note Implementación en una sola expresión: la comparación del iterador
 *       con `end()` es la condición buscada, y devolverla directamente evita
 *       el `if` redundante que añadiría ruido sin mejorar la legibilidad.
 *       La declaración de la cabecera ya la marca `[[nodiscard]]`.
 */
bool ConfiguracionEntorno::contiene(const std::string &clave) const {
    std::unordered_map<std::string, std::string>::const_iterator iterador = variables_.find(clave);
    return iterador != variables_.end();
}
