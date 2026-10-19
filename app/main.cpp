// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>
#include <curl/curl.h>
#include "cliente_http.h"
#include "configuracion_entorno.h"
#include "db.h"
#include "error.h"
#include "json.hpp"
#include "uf_holder.h"
#include "uf.h"



/**
 * @brief Muestra por consola los datos de identificación del grupo.
 *
 * @details
 * Función auxiliar de presentación, sin efectos sobre el estado del
 * programa. Se invoca una única vez al final del flujo principal, después
 * del bloque try/catch y antes del curl_global_cleanup() final. Su
 * propósito es dejar constancia del autor del taller en la salida estándar
 * del programa, con independencia de si el flujo principal terminó con
 * éxito o con excepción.
 *
 * @par Por qué se declara 'static'
 * La función no forma parte de la interfaz del programa ni se exporta a
 * otras unidades de traducción. El calificador 'static' con enlace interno
 * le da visibilidad exclusiva a este archivo, permite al compilador
 * inlinearla agresivamente si lo considera oportuno, y evita colisiones de
 * símbolo con funciones homónimas en otros módulos del proyecto.
 *
 * @par Independencia del flujo principal
 * La invocación a esta función se realiza fuera del bloque try/catch. La
 * decisión es deliberada: si el flujo principal falla —por ejemplo, porque
 * la API devuelve un JSON malformado— la función se ejecuta igualmente y
 * el programa deja constancia de sus autores. Se considera información de
 * contexto que debe aparecer siempre, no un resultado del procesamiento.
 *
 * @param programa Nombre del ejecutable, tal como llega en 'argv[0]'. Se
 *                 pasa por referencia constante porque solo se lee; el
 *                 llamador conserva la propiedad del argumento.
 *
 * @note La salida usa 'std::cout' sin verificar errores. Si la salida
 *       estándar está cerrada o redirigida a un destino no escribible, los
 *       mensajes se pierden silenciosamente, pero el programa continúa. Esta
 *       política es aceptable para una función puramente informativa.
 */
static void participantes(const std::string &programa);

/**
 * @brief Punto de entrada del programa.
 *
 * @details
 * Orquesta el flujo completo del taller: desde la lectura de la
 * configuración hasta la exportación final del CSV. Todas las etapas
 * comparten el mismo bloque try/catch, de modo que un fallo en cualquiera
 * de ellas aborta las restantes y produce un único mensaje de error por la
 * salida estándar de error.
 *
 * @par Flujo detallado
 *
 *   1. **Validación de argumentos.** Se comprueba que se haya proporcionado
 *      al menos un argumento —la ruta al archivo de secretos—. Si falta,
 *      se lanza 'Error' con un mensaje instructivo antes de tocar ninguna
 *      otra dependencia.
 *
 *   2. **Lectura de configuración.** Se construye un 'ConfiguracionEntorno'
 *      a partir del archivo indicado. Si el archivo no existe, no es
 *      legible o tiene un formato inválido, la construcción falla con
 *      'Error' y el bloque catch captura la excepción.
 *
 *   3. **Inicialización de libcurl.** Se invoca 'curl_global_init' con
 *      'CURL_GLOBAL_DEFAULT'. Esta llamada debe realizarse **antes** de
 *      crear cualquier hilo que use 'ClienteHttp'; de lo contrario, el
 *      estado global de libcurl se corrompería al inicializarse desde
 *      varios hilos a la vez. El resultado se verifica explícitamente y se
 *      traduce a 'Error' si no es 'CURLE_OK'.
 *
 *   4. **Descarga de la API.** Se construye la URL de la API de la CMF con
 *      la clave obtenida de la configuración y se realiza la petición GET
 *      mediante 'ClienteHttp'. La función 'get' es bloqueante y lanza
 *      'ErrorHttp' si la respuesta no tiene código 2xx o si falla el
 *      transporte.
 *
 *   5. **Parseo del JSON.** Se convierte la respuesta textual a un
 *      'nlohmann::json', y de ahí a un 'UfHolder' mediante 'get<UfHolder>',
 *      que activa la función 'from_json' declarada en uf_holder.h. Si la
 *      estructura del JSON no coincide con la esperada, se lanza la
 *      excepción correspondiente.
 *
 *   6. **Verificación de contenido.** Si el 'UfHolder' está vacío, se lanza
 *      'Error' antes de continuar. Es un caso anómalo —la API siempre
 *      debería devolver al menos una UF para el rango solicitado— y
 *      conviene detectarlo temprano en lugar de dejar que el bucle
 *      paralelo no haga nada y la exportación genere un CSV vacío.
 *
 *   7. **Construcción de la cadena de conexión.** Se leen cinco variables
 *      adicionales del archivo de configuración y se concatenan en el
 *      formato que libpq espera. Si alguna falta, 'ConfiguracionEntorno'
 *      lanza 'Error'.
 *
 *   8. **Persistencia paralela.** Se abre una región '#pragma omp parallel' en la que cada hilo construye su propio 'Db' y recorre el vector completo de UF con '#pragma omp parallel for', llamando a 'Db::guardar'. Este reparto es incorrecto; ver la advertencia 'Reparto incorrecto del lote'.
 *
 *   9. **Exportación a CSV.** Tras el bucle —y por tanto tras la barrera
 *      implícita de OpenMP que garantiza que todos los hilos han
 *      terminado— se abre una nueva conexión a la base de datos y se
 *      exporta el historial completo a 'ufs.csv'.
 *
 *  10. **Manejo global de errores.** El bloque try/catch envuelve todo el
 *      flujo anterior. Cualquier excepción derivada de 'std::exception' se
 *      captura y se imprime por 'std::cerr', sin propagarse fuera de
 *      'main'.
 *
 *  11. **Presentación de autores.** Se invoca 'participantes' fuera del
 *      try/catch, de modo que se ejecuta con independencia del resultado
 *      del flujo principal.
 *
 *  12. **Finalización de libcurl.** Se invoca 'curl_global_cleanup' como
 *      última operación. En este punto ya no existen hilos activos —el
 *      'parallel for' tiene una barrera implícita al final y no hay más
 *      paralelismo en el programa— ni objetos 'ClienteHttp' en uso, así
 *      que la llamada es segura según la documentación de libcurl.
 *
 * @par Política de manejo de errores
 * El programa sigue una estrategia *fail-fast* con captura de último
 * recurso: cada etapa valida sus precondiciones y lanza ante cualquier
 * anomalía, y el bloque catch de 'main' captura todas las excepciones
 * derivadas de 'std::exception' y las reporta sin distinguirlas. Esta
 * política prioriza la simplicidad —un único punto de reporte— sobre la
 * granularidad —mensajes diferenciados por tipo de error—. Si en el futuro
 * se necesitara aplicar acciones distintas según la categoría (por ejemplo,
 * reintentar la descarga ante un error de red, abortar ante un error de
 * base de datos), habría que sustituir este catch único por varios
 * específicos.
 *
 * @param argc Número de argumentos de línea de comandos. Debe ser al menos
 *             2 para que el programa pueda continuar: el primero es el
 *             nombre del ejecutable y el segundo la ruta al archivo de
 *             configuración.
 * @param argv Vector de argumentos. 'argv[0]' es el nombre del ejecutable,
 *             'argv[1]' la ruta al archivo de secretos. El resto se ignora.
 *
 * @return 'EXIT_SUCCESS' incondicionalmente. Este valor no indica el éxito
 *         del procesamiento —el programa puede haber registrado errores por
 *         'std::cerr' sin reflejarlos en el código de salida—. Si se
 *         necesitara que el shell pudiera distinguir entre ejecución con
 *         éxito y ejecución con fallos, habría que acumular un indicador
 *         durante el flujo y retornar 'EXIT_FAILURE' cuando corresponda.
 *
 * @par Concurrencia
 * El paso 8 abre la región paralela que persiste las UF, con un 'Db' por hilo. El número de hilos lo determina OpenMP según la variable
 * de entorno 'OMP_NUM_THREADS' y la configuración por defecto del sistema
 * —típicamente el número de núcleos disponibles—.
 *
 * El número de hilos es una variable crítica para el dimensionamiento del servidor de base de datos: la región externa abre un 'Db' por hilo, así que el número de conexiones simultáneas es igual al número de hilos. Debe tenerse en cuenta al configurar 'max_connections' en PostgreSQL.
 *
 * @warning **Inicialización de libcurl antes de cualquier hilo.** El orden
 *          actual del código es correcto: 'curl_global_init' se invoca
 *          antes del 'parallel for'. Cualquier refactorización que mueva
 *          esa llamada dentro del bucle, o que cree hilos antes de ella,
 *          introducirá corrupción del estado global de libcurl y
 *          comportamientos indeterminados que pueden manifestarse de formas
 *          muy variadas. Ver cliente_http.h para el análisis completo de la
 *          precondición.
 *
 * @warning **Llamada a curl_global_cleanup incluso sin inicialización
 *          exitosa.** La función se invoca siempre al final del programa,
 *          incluyendo los casos en que 'curl_global_init' falló o en que el
 *          bloque try lanzó antes de llegar a inicializar. La documentación
 *          de libcurl no garantiza explícitamente que llamar a
 *          'curl_global_cleanup' sin haber inicializado sea seguro, aunque
 *          en la práctica la implementación lo tolera. Si se quiere ser
 *          estrictamente correcto, la llamada debería condicionarse a que
 *          la inicialización haya tenido éxito, mediante un indicador
 *          booleano o envolviendo la llamada en su propio try/catch.
 *
 * @warning **Reparto incorrecto del lote.** Cada hilo de la región externa ejecuta el 'parallel for' interno sobre el vector completo, así que cada UF se escribe tantas veces como hilos existan. Si el anidamiento de regiones paralelas está activado, los hilos internos comparten el 'Db' del hilo externo, lo que produce una carrera sobre la conexión (ver 'db.h'). El patrón previsto es un único 'parallel for' con un 'Db' por hilo.
 *
 * @warning **Copia silenciosa del vector de UF.** La línea
 *          'std::vector<Uf> ufs = historial.obtener_ufs();' copia el vector
 *          completo, aunque 'obtener_ufs' devuelve una referencia constante
 *          precisamente para evitar esa copia. El efecto es una duplicación
 *          de memoria y tiempo de copia proporcional al tamaño del lote.
 *          La forma correcta sería 'const auto& ufs =
 *          historial.obtener_ufs();'. 
 *
 * @warning **Catch principal sin granularidad.** El bloque catch captura
 *          'std::exception&' sin 'const' y sin distinguir categorías. Todas
 *          las excepciones se reportan con el mismo formato, sin
 *          oportunidad de aplicar políticas específicas por tipo. Si el
 *          programa debiera distinguir entre errores recuperables y no
 *          recuperables, este es el punto donde debe introducirse la
 *          clasificación.
 *
 * @note El catch se declara sobre 'std::exception&' sin 'const'. Es una
 *       desviación estilística menor respecto a la convención habitual
 *       ('const std::exception&'), pero funcionalmente idéntica: no se
 *       modifica el objeto capturado, así que la ausencia de 'const' no
 *       produce ningún efecto observable.
 *
 * @see ClienteHttp::get para el contrato de la descarga HTTP.
 * @see UfHolder::from_json para el contrato de deserialización del JSON.
 * @see Db::guardar para el contrato de la persistencia individual.
 * @see Db::exportar para el contrato de la exportación a CSV.
 * @see ConfiguracionEntorno::obtener para el contrato de lectura de
 *      configuración.
 */
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            throw Error("Se necesita la ruta al archivo de secretos para continuar");
        }

        // 1. Lectura de configuración. Si el archivo no existe o no es
        //    legible, el constructor de ConfiguracionEntorno lanza Error y
        //    el bloque catch de más abajo lo reporta.
        ConfiguracionEntorno configuracion(argv[1]);

        // 2. Inicialización de libcurl. Debe ejecutarse ANTES de crear
        //    cualquier hilo que use ClienteHttp: la inicialización no es
        //    thread-safe y su invocación perezosa desde varios hilos
        //    corrompería el estado global de la biblioteca. Ver
        //    cliente_http.h para el análisis completo de esta precondición.
        const CURLcode resultado_init = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (resultado_init != CURLE_OK) {
            throw Error("Fallo en curl_global_init: " + std::string(curl_easy_strerror(resultado_init)));
        }

        /**
         * Incluir acá la lógica del programa
         *
         */

        // 3. Construcción de la URL de la API a partir de la clave de
        //    configuración. La URL apunta al endpoint de la CMF que
        //    devuelve el historial de UF desde 1985 en formato JSON.
        const std::string apiKey = configuracion.obtener("CMF_API_KEY");
        const std::string baseUrl = configuracion.obtener("CMF_API_URL");
        const std::string url = baseUrl + "&apikey=" + apiKey;

        // 4. Descarga del recurso. La petición es bloqueante y lanza
        //    ErrorHttp si la respuesta no está en el rango 2xx o si falla
        //    el transporte. La respuesta se imprime por stdout antes de
        //    parsearla: es útil durante el desarrollo, aunque en producción
        //    generaría una salida voluminosa.
        ClienteHttp http;
        const std::string respuesta = http.get(url);
        std::cout << respuesta << std::endl;

        // 5. Parseo del JSON y conversión a UfHolder. La función
        //    UfHolder::from_json aplica la validación estructural del campo
        //    "UFs" y valida cada elemento mediante crear_uf_desde_json.
        const nlohmann::json json = nlohmann::json::parse(respuesta);
        UfHolder historial = json.get<UfHolder>();

        // 6. Copia del vector de UF. NOTA: esta asignación copia el vector
        //    completo, aunque obtener_ufs devuelve una referencia. La copia no
        //    rompe el programa —el vector resultante es independiente y
        //    sigue siendo válido— pero duplica el uso de memoria y añade
        //    un coste de copia proporcional al tamaño del lote.
        std::vector<Uf> ufs = historial.obtener_ufs();
        if (ufs.empty()) {
            throw Error("No hay información de ufs");
        }

        std::cout << "Hay " << ufs.size() << " ufs" << std::endl;

        // 7. Construcción de la cadena de conexión a PostgreSQL a partir de
        //    cinco variables de configuración. Si alguna falta,
        //    ConfiguracionEntorno::obtener lanza Error y el bloque catch lo
        //    reporta. La cadena resultante sigue el formato que libpq
        //    espera: pares clave=valor separados por espacios.
        std::string pgHost = configuracion.obtener("DB_HOST");
        std::string pgPort = configuracion.obtener("DB_PORT");
        std::string pgName = configuracion.obtener("DB_NAME");
        std::string pgUser = configuracion.obtener("DB_USER");
        std::string pgPass = configuracion.obtener("DB_PASS");
        std::string conexion = "host=" + pgHost + " port=" + pgPort + " dbname=" + pgName + " user=" + pgUser +
                               " password="
                               + pgPass;

        // 8. Persistencia paralela. Cada hilo de la región externa crea su
        //    propio Db y ejecuta el 'parallel for' interno sobre todo el
        //    vector. Con el anidamiento desactivado, cada UF se escribe una
        //    vez por hilo; ver la advertencia 'Reparto incorrecto del lote'
        //    en la documentación de main(). La barrera implícita de cada
        //    región garantiza que los hilos han terminado antes del paso 9.
        #pragma omp parallel
        {
            Db db(conexion);
#pragma omp parallel for
        for (const Uf &uf: ufs) {
            db.guardar(uf);
        }
    }

        // 9. Exportación a CSV. Se abre una nueva conexión desde el hilo
        //    principal —no hay concurrencia aquí, el bucle paralelo ya ha
        //    concluido— y se exporta el historial completo persistido.
        //    Aunque el paso 8 ya guardó cada UF en la base, la exportación
        //    consulta la tabla de nuevo para generar el CSV, así que el
        //    resultado refleja el estado persistido, no el vector en
        //    memoria.
        Db db(conexion);
        db.exportar("ufs.csv");
    } catch (std::exception &e) {
        // Captura de último recurso: cualquier excepción derivada de
        // std::exception se reporta por stderr. El programa no reintenta ni
        // diferencia categorías; ver la política de errores documentada en
        // main().
        std::cerr << std::endl << e.what() << std::endl;
    }

    // 10. Presentación de autores. Se invoca fuera del try/catch para que
    //     aparezca en la salida con independencia del resultado del flujo
    //     principal.
    participantes(argv[0]);

    // 11. Finalización de libcurl. Es la última operación del programa
    //     porque, según la documentación de la biblioteca, solo debe
    //     invocarse cuando ya no quedan manejadores activos ni hilos que
    //     puedan crearlos. En este punto ambas condiciones se cumplen:
    //     el 'parallel for' tiene barrera implícita al final y no hay
    //     otros ClienteHttp en uso.
    curl_global_cleanup();

    return EXIT_SUCCESS;
}

static void participantes(const std::string &programa) {
    std::cout << std::endl << "=== Taller " << programa << " ===" << std::endl;
    std::cout << std::endl << "Sebastián Salazar Molina";
    std::cout << std::endl << "Académico Universidad Tecnológica Metropolitana" << std::endl;
}

