// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// cliente_http.cpp
#include "cliente_http.h"
#include "error_http.h" // Inclusión explícita para garantizar la definición de ErrorHttp

#include <cstddef>
#include <memory>
#include <string>
#include <curl/curl.h>

namespace {
    /**
     * @brief Deleter personalizado que garantiza la liberación segura del manejador de libcurl.
     *
     * @details
     * Encapsula `curl_easy_cleanup` en un functor sin estado para poder integrarlo en
     * un `std::unique_ptr`. Es la pieza que cierra el ciclo RAII en torno a una API
     * que, por ser de origen C, carece de destructores propios.
     *
     * @par Contrato de seguridad
     * `curl_easy_cleanup(nullptr)` está documentado por libcurl como una operación
     * válida y sin efecto. Por tanto, este deleter **no** necesita comprobar la
     * nulidad antes de invocar la función subyacente; añadir dicha comprobación
     * sería código muerto y, peor aún, sugeriría una precondición inexistente.
     *
     * @par Garantía de excepción
     * `noexcept` explícito: la liberación de recursos nunca debe participar en el
     * desbobinado (unwinding). Si `curl_easy_cleanup` pudiera lanzar, se invocaría
     * `std::terminate` desde el destructor de `std::unique_ptr`, lo cual es
     * preferible a una fuga silenciosa de recursos.
     *
     * @note El functor es trivialmente copiable y sin estado; puede pasarse por
     *       valor sin coste apreciable y reutilizarse para múltiples instancias
     *       de `ManejadorCurl`.
     */
    struct LiberadorCurl {
        void operator()(CURL *manejador) const noexcept {
            curl_easy_cleanup(manejador);
        }
    };

    /**
     * @brief Alias de tipo que dota a `CURL*` de semántica RAII.
     *
     * @details
     * `std::unique_ptr` aporta tres propiedades críticas que `CURL*` desnudo no
     * ofrece:
     *
     *   1. **Liberación automática** en cualquier ruta de salida, ya sea retorno
     *      normal o excepción, eliminando la necesidad de `try/catch` o de
     *      `goto cleanup` en el cuerpo de `get()`.
     *   2. **No copiable por construcción**: impide duplicaciones accidentales del
     *      puntero que provocarían un doble `curl_easy_cleanup` (comportamiento
     *      indefinido en libcurl).
     *   3. **Intención explícita de propiedad exclusiva**: el lector del código
     *      sabe sin ambigüedad que el manejador pertenece a un único ámbito y a
     *      un único hilo.
     *
     * @note Este alias es local al espacio de nombres anónimo: no forma parte de
     *       la interfaz pública del módulo y no debe filtrarse a través de
     *       cabeceras.
     */
    using ManejadorCurl = std::unique_ptr<CURL, LiberadorCurl>;

    /**
     * @brief Callback de escritura invocado por libcurl para acumular el cuerpo de la respuesta.
     *
     * @details
     * libcurl puede invocar este callback **múltiples veces** durante una misma
     * transferencia, entregando fragmentos del cuerpo según lleguen por la red.
     * Su responsabilidad es puramente acumulativa: anexar cada fragmento al
     * `std::string` provisto por `get()` a través de `CURLOPT_WRITEDATA`.
     *
     * @param datos Puntero al búfer con los bytes recién recibidos. No se debe
     *              asumir alineación, ni terminación nula, ni que el contenido
     *              forme una cadena de texto válida: puede ser binario arbitrario.
     * @param tam   Tamaño de cada elemento en bytes. libcurl lo fija típicamente
     *              a 1, pero la multiplicación con `n` es obligatoria por contrato
     *              de la API.
     * @param n     Número de elementos recibidos en este fragmento.
     * @param destino Puntero opaco al `std::string` receptor, tal como se registró
     *                mediante `CURLOPT_WRITEDATA`. Su tipo real se reconstruye con
     *                un `static_cast` — no hay alternativa tipada en la frontera C.
     *
     * @return Número de bytes efectivamente consumidos (`tam * n` en caso de éxito).
     *         Retornar un valor distinto de `tam * n` es interpretado por libcurl
     *         como error de escritura y provoca el aborto de la transferencia con
     *         `CURLE_WRITE_ERROR`. En particular, **retornar 0** es el mecanismo
     *         documentado para señalizar el fallo.
     *
     * @par Por qué está marcado `noexcept`
     * Esta función cruza la frontera entre C++ y C: libcurl la invoca como
     * función con enlace C. Si una excepción C++ escapara a través de esta
     * frontera, el comportamiento sería indefinido y, en la práctica, se
     * invocaría `std::terminate` sin posibilidad de recuperación. Por eso, el
     * `try/catch(...)` interno **no es una omisión de manejo de errores**, sino
     * un patrón de contención de frontera obligatorio: cualquier excepción —en
     * particular `std::bad_alloc` al crecer el `std::string`— se traduce en un
     * retorno 0, que libcurl propaga como `CURLE_WRITE_ERROR` y `get()` acaba
     * convirtiendo en `ErrorHttp`.
     *
     * @note El cálculo `tam * n` se realiza en `std::size_t` antes de cualquier
     *       otro uso para evitar desbordamiento de tipos intermedios.
     *
     * @warning El puntero `destino` debe apuntar a un `std::string` vivo durante
     *          toda la transferencia. En `get()` esto queda garantizado porque
     *          el `std::string` es una variable local cuyo ciclo de vida envuelve
     *          la llamada a `curl_easy_perform`.
     */
    std::size_t acumularRespuesta(char *datos, std::size_t tam, std::size_t n, void *destino) noexcept {
        const std::size_t total = tam * n;
        try {
            auto *cadena_destino = static_cast<std::string *>(destino);
            cadena_destino->append(datos, total);
            return total;
        } catch (...) {
            // Contención estricta: prevenir el desbobinado (unwinding) a través de la frontera C.
            return 0;
        }
    }
} // namespace

std::string ClienteHttp::get(const std::string &url) const {
    // 1. Inicialización del manejador local (thread-safe: no comparte estado con
    //    otras instancias ni con otras invocaciones concurrentes de esta misma
    //    instancia, porque ClienteHttp no posee miembros mutables).
    ManejadorCurl manejador(curl_easy_init());
    if (!manejador) {
        throw ErrorHttp(
            "Fallo crítico: no se pudo inicializar el manejador de libcurl (curl_easy_init retornó nullptr)");
    }

    std::string cuerpo_respuesta;
    // Inicialización explícita a cero para garantizar que la cadena esté null-terminated.
    // Si libcurl no escribe en el búfer (por ejemplo, porque falla antes de poder
    // rellenarlo), la comprobación posterior buffer_error[0] != '\0' sigue siendo segura.
    char buffer_error[CURL_ERROR_SIZE] = {0};

    // 2. Configuración explícita de opciones.
    //    Se agrupan aquí, tras la inicialización, para que el bloque de ejecución
    //    quede libre de ruido y la secuencia "configurar → ejecutar → validar"
    //    sea legible de un vistazo.
    CURL *m = manejador.get();
    curl_easy_setopt(m, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m, CURLOPT_WRITEFUNCTION, &acumularRespuesta);
    curl_easy_setopt(m, CURLOPT_WRITEDATA, static_cast<void *>(&cuerpo_respuesta));
    curl_easy_setopt(m, CURLOPT_ERRORBUFFER, buffer_error);
    curl_easy_setopt(m, CURLOPT_NOSIGNAL, 1L); // Obligatorio para prevenir fallos en entornos multihilo con DNS:
    // sin esta opción, libcurl instala manejadores de señal que
    // interfieren con los de la aplicación y pueden provocar
    // terminaciones espurias en programas multihilo.
    curl_easy_setopt(m, CURLOPT_FOLLOWLOCATION, 1L); // Permite seguir redirecciones HTTP 3xx.
    curl_easy_setopt(m, CURLOPT_MAXREDIRS, maximo_redirecciones);
    curl_easy_setopt(m, CURLOPT_CONNECTTIMEOUT, timeout_conexion_segundos);
    curl_easy_setopt(m, CURLOPT_TIMEOUT, timeout_total_segundos);

    // 3. Ejecución y validación de errores de transporte.
    const CURLcode resultado = curl_easy_perform(m);
    if (resultado != CURLE_OK) {
        // Prioridad al mensaje enriquecido de CURLOPT_ERRORBUFFER sobre la cadena
        // genérica de curl_easy_strerror: el primero suele incluir el nombre del
        // host, el puerto o el certificado problemático, información que acelera
        // el diagnóstico en producción.
        const std::string mensaje_detalle = (buffer_error[0] != '\0')
                                                ? std::string(buffer_error)
                                                : std::string(curl_easy_strerror(resultado));

        throw ErrorHttp("Error de transporte en libcurl: " + mensaje_detalle);
    }

    // 4. Validación del código de estado HTTP.
    // NOTA: curl_easy_getinfo exige explícitamente un puntero a 'long' para
    // CURLINFO_RESPONSE_CODE. Usar 'int' o 'auto' aquí no compila o produce
    // corrupción silenciosa según el compilador.
    long codigo_http = 0;
    const CURLcode resultado_info = curl_easy_getinfo(m, CURLINFO_RESPONSE_CODE, &codigo_http);

    if (resultado_info != CURLE_OK) {
        throw ErrorHttp(
            "No se pudo leer el código de respuesta HTTP: " + std::string(curl_easy_strerror(resultado_info)));
    }

    // Solo se consideran exitosos los códigos 2xx. Redirecciones no seguidas
    // (3xx remanentes tras agotar maximo_redirecciones), errores de cliente (4xx)
    // y errores de servidor (5xx) se tratan todos como fallo, con el código
    // numérico adjunto para que el llamador pueda discriminar por su cuenta.
    if (codigo_http < 200 || codigo_http > 299) {
        throw ErrorHttp("Respuesta HTTP inesperada. Código de estado: " + std::to_string(codigo_http), codigo_http);
    }

    return cuerpo_respuesta;
}
