// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// cliente_http.h
#ifndef APP_CLIENTE_HTTP_H
#define APP_CLIENTE_HTTP_H

#include <string>

/**
 * @brief Cliente HTTP sin estado para operaciones de red seguras y concurrentes.
 *
 * @details
 * Esta clase implementa un cliente HTTP de propósito general cuyo diseño gira en
 * torno a dos principios no negociables:
 *
 *   1. **Ausencia total de estado compartido.** La clase no almacena ningún
 *      atributo de instancia mutable; todos los parámetros de configuración son
 *      `static constexpr` resueltos en tiempo de compilación. Cada invocación a
 *      get() construye y destruye su propio manejador `CURL*`, de modo que el
 *      ciclo de vida del recurso queda confinado al ámbito de la llamada.
 *
 *   2. **Seguridad concurrente por construcción.** Al no existir escritura sobre
 *      miembros compartidos ni cachés internas, una misma instancia puede ser
 *      invocada simultáneamente desde N hilos sin sincronización adicional
 *      (sin mutex, sin atómicos, sin `thread_local`). El coste de esta garantía
 *      es un `curl_easy_init()`/`curl_easy_cleanup()` por petición, despreciable
 *      frente al coste de red del propio intercambio HTTP.
 *
 * @par Ejemplo de uso concurrente
 * @code
 *   ClienteHttp cliente;                     // una única instancia compartida
 *   #pragma omp parallel for
 *   for (int i = 0; i < urls.size(); ++i) {
 *       try {
 *           std::string body = cliente.get(urls[i]);
 *           procesar(body);
 *       } catch (const ErrorHttp& e) {
 *           registrar_fallo(urls[i], e.what());
 *       }
 *   }
 * @endcode
 *
 * @par Precondiciones críticas de concurrencia
 * El cumplimiento de estas dos reglas es **responsabilidad exclusiva del
 * llamador**; la clase no puede verificarlas ni corregirlas en tiempo de
 * ejecución:
 *
 *   1. `curl_global_init(CURL_GLOBAL_DEFAULT)` debe haberse invocado
 *      **exactamente una vez** en `main()`, **antes** de crear cualquier hilo
 *      que use esta clase. libcurl documenta esta función como no segura para
 *      hilos; invocarla de forma perezosa desde varios hilos provoca
 *      corrupción del estado global de la biblioteca.
 *
 *   2. `curl_global_cleanup()` debe invocarse **solo después** de que todos
 *      los hilos hayan finalizado (vía `join()` o equivalente) y de que no
 *      quede ningún `CURL*` activo. Liberar la biblioteca con transferencias
 *      en vuelo produce comportamiento indefinido (típicamente
 *      `SIGSEGV` en el hilo de trabajo).
 *
 * @par Contrato de errores
 * Cualquier fallo de transporte —resolución DNS, handshake TLS, timeout de
 * conexión, conexión rechazada, transferencia abortada— así como cualquier
 * código de estado HTTP fuera del rango `2xx`, se traduce en una excepción
 * ErrorHttp. La clase **nunca** devuelve una cadena vacía como señal de error,
 * lo que evita la ambigüedad clásica entre "respuesta vacía legítima" y
 * "fallo silencioso".
 *
 * @note La clase no es copiable ni movible de forma explícita, pero al carecer
 *       de estado tampoco lo necesita: puede construirse una instancia por
 *       hilo a coste cero sin penalización de memoria apreciable.
 *
 * @warning No se debe invocar get() desde un manejador de señal ni desde un
 *          callback de libcurl reentrante: libcurl no es async-signal-safe.
 *
 * @see https://curl.se/libcurl/c/threadsafe.html
 * @see https://curl.se/libcurl/c/curl_global_init.html
 */
class ClienteHttp {
public:
    /**
     * @brief Realiza una petición HTTP GET a la URL especificada.
     *
     * @details
     * La función sigue un patrón estricto de adquisición y liberación de
     * recursos (RAII manual, dado que libcurl es una API C):
     *
     *   1. `curl_easy_init()` — obtiene un manejador nuevo y privado al hilo
     *      llamante.
     *   2. `curl_easy_setopt()` — aplica los parámetros de configuración de
     *      red documentados más abajo.
     *   3. `curl_easy_setopt(CURLOPT_WRITEFUNCTION, ...)` — instala un
     *      callback que acumula el cuerpo en un `std::string` local.
     *   4. `curl_easy_perform()` — ejecuta la transferencia de forma
     *      bloqueante.
     *   5. Verificación del código de respuesta y del código de retorno de
     *      libcurl.
     *   6. `curl_easy_cleanup()` — libera el manejador antes de retornar o
     *      lanzar, sin dejar recursos colgados en ninguna ruta de salida.
     *
     * @param url Dirección URL completa del recurso a solicitar. Debe incluir
     *            el esquema (`http://` o `https://`). No se realiza ninguna
     *            normalización ni validación sintáctica previa; una URL
     *            malformada se manifestará como ErrorHttp procedente de
     *            libcurl.
     *
     * @return std::string Cuerpo íntegro de la respuesta HTTP. Solo se retorna
     *         cuando el código de estado está en el rango `[200, 299]`. El
     *         contenido puede ser binario; no se aplica decodificación de
     *         charset ni decompresión más allá de la que libcurl negocie
     *         automáticamente con `CURLOPT_ACCEPT_ENCODING`.
     *
     * @throws ErrorHttp En los siguientes escenarios, entre otros:
     *         - Fallo de resolución DNS (`CURLE_COULDNT_RESOLVE_HOST`).
     *         - Timeout de conexión agotado (ver timeout_conexion_segundos).
     *         - Timeout total agotado (ver timeout_total_segundos).
     *         - Fallo del handshake TLS (certificado inválido, CA desconocida).
     *         - Conexión rechazada o interrumpida por el par.
     *         - Código de estado HTTP fuera del rango 2xx (3xx no seguido por
     *           exceder maximo_redirecciones, 4xx, 5xx).
     *
     * @note El atributo `[[nodiscard]]` previene errores silenciosos donde la
     *       respuesta es ignorada accidentalmente por el llamador. Dado que
     *       get() puede fallar lanzando excepción, descartar el valor de
     *       retorno casi siempre indica un error de programación.
     *
     * @warning Esta función es bloqueante. En contextos con SLA estricto,
     *          considere invocarla desde un pool de hilos cuyo tamaño esté
     *          dimensionado para absorber el peor caso `timeout_total_segundos`.
     *
     * @par Garantía de excepción
     * Fuerte: si get() lanza, no queda ningún `CURL*` sin liberar y el
     * `std::string` interno utilizado como buffer de respuesta se destruye
     * correctamente por unwinding.
     */
    [[nodiscard]] std::string get(const std::string &url) const;

private:
    // ==========================================================================
    // PARÁMETROS DE CONFIGURACIÓN DE RED (SLA Y TOLERANCIA A FALLOS)
    // ==========================================================================
    // NOTA DE MANTENIMIENTO: Estos valores son de tipo 'long' para cumplir
    // estrictamente con la firma de la API de libcurl (curl_easy_setopt).
    // No cambiar a 'int', 'auto' o 'std::size_t' para evitar truncamiento o
    // advertencias de conversión en plataformas de 64 bits.
    //
    // NOTA DE DISEÑO: Se declaran como 'static constexpr' y no como miembros
    // de instancia para reforzar la naturaleza sin estado de la clase y
    // permitir su uso como argumentos de plantilla o en contextos constantes.
    // ==========================================================================

    /**
     * @brief Tiempo máximo para establecer la conexión (TCP + TLS).
     *
     * @details
     * Se mapea a `CURLOPT_CONNECTTIMEOUT` y acota exclusivamente la fase de
     * establecimiento: resolución DNS, handshake TCP y, si procede, handshake
     * TLS. No limita la transferencia posterior de datos.
     *
     * @par Justificación
     * 17 segundos permiten absorber 3-4 retransmisiones TCP ante micro-cortes
     * típicos de redes públicas (por ejemplo, APIs gubernamentales como la CMF
     * chilena, expuestas a picos de latencia y throttling intermitente),
     * aplicando un patrón *fail-fast* que evita agotar el pool de hilos de
     * OpenMP esperando a un host inalcanzable.
     *
     * La elección de un número primo reduce la probabilidad de colisión
     * periódica de reintentos entre múltiples clientes que compartan una
     * cadencia fija de sondeo, mitigando el efecto *thundering herd* contra el
     * servidor remoto.
     */
    static constexpr long timeout_conexion_segundos = 17L;

    /**
     * @brief Tiempo máximo para la operación completa (conexión + transferencia).
     *
     * @details
     * Se mapea a `CURLOPT_TIMEOUT`. A diferencia de timeout_conexion_segundos,
     * este valor acota la transferencia íntegra: establecimiento, envío de la
     * petición, espera de la respuesta y descarga del cuerpo.
     *
     * @par Justificación
     * Otorga un margen de ~30 segundos adicionales tras la conexión para la
     * transferencia del payload. Para respuestas JSON pequeñas (<100 KB),
     * habituales en APIs REST de consulta, este margen es más que suficiente
     * para absorber throttling severo del servidor remoto sin colgar el
     * proceso local indefinidamente.
     *
     * La asimetría respecto a timeout_conexion_segundos es intencional: un
     * servidor puede tardar en *responder* aunque la conexión TCP/TLS haya
     * sido instantánea, y forzar un timeout agresivo de transferencia
     * produciría falsos negativos contra backends con latencia de cola alta.
     */
    static constexpr long timeout_total_segundos = 47L;

    /**
     * @brief Número máximo de redirecciones HTTP (3xx) permitidas.
     *
     * @details
     * Se mapea a `CURLOPT_MAXREDIRS` junto con `CURLOPT_FOLLOWLOCATION`
     * habilitado. Al agotarse el cupo, libcurl aborta la transferencia y la
     * clase traduce el fallo en ErrorHttp.
     *
     * @par Justificación
     * El valor por defecto de libcurl es 50, lo cual constituye un riesgo de
     * seguridad y de rendimiento: una mala configuración del servidor remoto
     * puede desencadenar bucles de redirección que consumen ancho de banda y
     * mantienen ocupado un hilo durante tiempo indefinido.
     */
    static constexpr long maximo_redirecciones = 5L;
};

#endif // APP_CLIENTE_HTTP_H
