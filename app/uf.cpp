// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

#include "uf.h"

#include <cmath>
#include <regex>
#include <stdexcept>


namespace {

    /**
     * @brief Convierte un valor numérico en formato chileno a `double`.
     *
     * @details
     * El formato chileno usa el punto como separador de miles y la coma como
     * separador decimal, al revés que el formato anglosajón. Un valor como
     * `"5.435,28"` representa cinco mil cuatrocientos treinta y cinco con
     * veintiocho centésimas, no cinco punto cuatro tres cinco dos ocho.
     *
     * La función no realiza una conversión directa con `std::stod`, porque
     * `std::stod` interpretaría el punto como separador decimal y descartaría
     * silenciosamente la parte decimal al encontrar la coma. En su lugar,
     * aplica dos pasos: primero valida el formato con una expresión regular
     * que reconoce exclusivamente la gramática chilena, y después normaliza
     * la cadena —eliminando puntos, sustituyendo comas por puntos— para que
     * `std::stod` la interprete correctamente.
     *
     * @par Por qué validar antes de normalizar
     * La tentación de saltarse la validación y limitarse a normalizar es
     * fuerte, pero produce fallos silenciosos graves. Un valor en formato
     * anglosajón como `"5435.28"` —que un usuario podría introducir por
     * costumbre o que podría llegar desde una API con configuración regional
     * distinta— sería normalizado a `"543528"` si simplemente se descartaran
     * los puntos. El error no se manifestaría en forma de excepción sino como
     * un valor de UF mil veces mayor, imposible de detectar sin comparar
     * contra un valor esperado. La validación previa convierte ese fallo
     * silencioso en un error ruidoso que apunta al dato problemático.
     *
     * @par Estructura de la expresión regular
     * El patrón `^(\d{1,3}(\.\d{3})+|\d+)(,\d+)?$` acepta exactamente dos
     * formas, ambas con la parte entera y la parte decimal opcional:
     *
     *   - `\d{1,3}(\.\d{3})+` — parte entera con separadores de miles. El
     *     primer grupo exige entre 1 y 3 dígitos (no puede haber un separador
     *     antes de una cifra de cuatro dígitos seguidos), y cada separador
     *     posterior va seguido de exactamente 3 dígitos. Esto descarta casos
     *     como `"5435.28"` (cuatro dígitos seguidos antes del punto, que solo
     *     puede ser separador decimal, no de miles).
     *   - `\d+` — parte entera sin separadores de miles. Acepta tanto `"5435"`
     *     como `"543528"`, permitiendo valores sin puntuación por comodidad
     *     para quien los introduce a mano.
     *
     * La parte decimal `(,\d+)?` es opcional: el formato chileno admite
     * valores enteros sin coma. Cuando está presente, exige al menos un
     * dígito tras la coma, descartando entradas degeneradas como `"5435,"`.
     *
     * @par Normalización a formato anglosajón
     * El bucle recorre la cadena una única vez y construye la versión
     * normalizada en un `std::string` nuevo. Los puntos se descartan porque
     * son separadores de miles, y la coma se sustituye por un punto porque
     * `std::stod` —en la locale por defecto— espera ese carácter como
     * separador decimal.
     *
     * @par Dependencia de la locale "C" en std::stod
     * La función asume que la locale global del proceso no ha sido modificada
     * desde la inicialización (o que, si lo ha sido, sigue siendo una en la
     * que el punto es el separador decimal). `std::stod` consulta la locale
     * activa para determinar qué carácter se considera separador decimal; si
     * un módulo ajeno del programa invocara `std::locale::global(std::locale(""))`
     * y la locale del sistema fuera, por ejemplo, la chilena, `std::stod`
     * esperaría coma y fallaría sobre la cadena normalizada. Este acoplamiento
     * es invisible desde la firma de la función y merece quedar documentado.
     * Si la robustez frente a cambios de locale fuera un requisito, habría
     * que sustituir `std::stod` por `std::from_chars`, que no consulta locale
     * alguna.
     *
     * @param valor_texto Cadena con el valor en formato chileno.
     *
     * @return Valor numérico equivalente, en `double`.
     *
     * @throws std::invalid_argument Si la cadena no encaja con el patrón
     *               chileno admitido. El mensaje incluye la cadena recibida
     *               para facilitar el diagnóstico.
     * @throws std::out_of_range Si el valor, una vez normalizado, excede el
     *               rango representable por `double`. Esta excepción la lanza
     *               `std::stod` internamente y no se captura aquí, porque
     *               indica un problema real con el dato que el llamador debe
     *               conocer.
     *
     * @note La expresión regular se declara `static const` en lugar de
     *       construirse en cada llamada. `std::regex` es caro de construir
     *       —implica compilar el patrón a un autómata— y reutilizar la
     *       instancia amortiza ese coste a lo largo de todas las invocaciones.
     *       La inicialización de la variable local estática es
     *       thread-safe desde C++11, así que esta decisión no compromete la
     *       seguridad concurrente de la función.
     *
     * @warning Esta función es interna al módulo (espacio de nombres
     *          anónimo) y no forma parte del contrato público de `Uf`. Los
     *          detalles de su implementación —la expresión regular concreta,
     *          la elección de `std::stod`— pueden cambiar sin afectar a
     *          código cliente, siempre que se preserve el contrato de
     *          aceptación/rechazo observable.
     */
    double convertir_valor_chileno(const std::string &valor_texto) {
        // Expresión regular compilada una sola vez y reutilizada en todas las
        // llamadas. La inicialización es thread-safe (C++11 en adelante), así
        // que no se introduce ningún punto de serialización entre hilos.
        static const std::regex patron_valor_chileno(R"(^(\d{1,3}(\.\d{3})+|\d+)(,\d+)?$)");

        if (!std::regex_match(valor_texto, patron_valor_chileno)) {
            throw std::invalid_argument(
                "Formato de valor inválido. Se esperaba formato chileno (ej. 5.435,28), se recibió: " + valor_texto);
        }

        // Normalización a formato anglosajón: se descartan los puntos
        // (separadores de miles) y se sustituye la coma por punto (separador
        // decimal). El bucle recorre la cadena una única vez.
        std::string valor_normalizado;
        for (const char caracter: valor_texto) {
            if (caracter == '.') {
                continue; // separador de miles: se descarta
            }
            valor_normalizado += (caracter == ',') ? '.' : caracter;
        }

        // std::stod usa la configuración regional "C" (punto decimal) mientras
        // no se llame a setlocale. Ver la documentación de la función para el
        // análisis del acoplamiento con la locale global del proceso.
        return std::stod(valor_normalizado);
    }
}


/**
 * @brief Construye un `Uf` delegando la validación en los setters.
 *
 * @details
 * El constructor no valida directamente: construye los miembros por defecto
 * —`fecha_` como cadena vacía y `valor_` sin inicializar— y luego invoca los setters
 * públicos, que son los que aplican las validaciones. Esta delegación tiene
 * una propiedad valiosa: garantiza que constructor y setters comparten
 * exactamente la misma lógica de validación, sin posibilidad de divergencia
 * por olvido.
 *
 * @par Estado intermedio del objeto durante la construcción
 * Entre la inicialización por defecto de los miembros y la invocación de los
 * setters, el objeto atraviesa un estado en el que `fecha_` está vacía y `valor_` aún no tiene valor. Ese estado viola las invariantes de la clase, pero es
 * inobservable desde código cliente: el objeto no está completamente
 * construido y ninguna referencia a él ha escapado del constructor. Solo si
 * una de las validaciones lanza, el objeto se destruye sin haber sido nunca
 * accesible.
 *
 * @par Por qué no se inicializan los miembros en la lista de inicialización
 * Inicializar `fecha_` y `valor_` directamente con los argumentos —es decir,
 * `Uf::Uf(std::string f, double v) : fecha_(std::move(f)), valor_(v) {}`—
 * sería más eficiente pero obligaría a duplicar las validaciones aquí o a
 * invocarlas explícitamente antes de la asignación. La delegación en los
 * setters es ligeramente más costosa (una asignación adicional) pero
 * garantiza la coherencia entre ambas rutas de entrada.
 *
 *
 * @throws std::invalid_argument Si la fecha o el valor violan las
 *               invariantes correspondientes. La excepción se origina en el
 *               setter correspondiente y se propaga sin transformación.
 *
 * @note El parámetro `fecha` se recibe por valor, pero `establecer_fecha` lo recibe por referencia constante y copia internamente: el argumento se copia una vez y no se mueve hacia `fecha_`.
 */
Uf::Uf(std::string fecha, double valor) {
    // Delegación a los setters: validación y asignación en un solo sitio.
    // Si cualquiera de los dos lanza, el objeto se destruye sin haber sido
    // nunca observable desde fuera.
    establecer_fecha(fecha);
    establecer_valor(valor);
}

/**
 * @brief Retorna la fecha del registro por referencia constante.
 *
 * @details
 * Implementación trivial: retorno del miembro privado sin transformación.
 * La elección de retorno por referencia —frente a retorno por copia— está
 * documentada en el contrato de la función, en `uf.h`; aquí solo cabe
 * señalar que la implementación se limita a materializar esa decisión.
 *
 * @return Referencia constante a `fecha_`. La referencia es válida mientras
 *         la instancia siga viva y no se invoque `establecer_fecha`.
 *
 * @note La función es `noexcept` porque ningún paso de su implementación
 *       puede lanzar: no hay construcción de objetos, no hay asignación de
 *       memoria, no hay conversión de tipos.
 */
const std::string &Uf::obtener_fecha() const noexcept {
    return fecha_;
}

/**
 * @brief Retorna el valor de la UF por copia.
 *
 * @details
 * Implementación trivial: retorno del miembro privado por valor. Copiar un
 * `double` cuesta lo mismo que copiar una referencia y elimina la necesidad
 * de que el llamador se preocupe por la validez de la referencia, tal como
 * se documenta en el contrato de la función, en `uf.h`.
 *
 * @return Copia del valor almacenado en `valor_`.
 *
 * @note La función es `noexcept` porque copiar un `double` es una operación
 *       trivial que no puede fallar.
 */
double Uf::obtener_valor() const noexcept {
    return valor_;
}

/**
 * @brief Actualiza la fecha, validando antes de asignar.
 *
 * @details
 * La función sigue un patrón de dos pasos estricto: primero validar, después
 * asignar. El orden no es una preferencia estilística sino una garantía
 * semántica: si la validación falla, la instancia queda intacta. La
 * alternativa —asignar primero y validar después— dejaría la instancia en un
 * estado inconsistente cuando la excepción se propagara.
 *
 * @par Garantía de excepción fuerte
 * La combinación "validar → asignar" proporciona la garantía de excepción
 * fuerte: si `establecer_fecha` lanza, el objeto queda exactamente como
 * estaba antes de la llamada. Esta garantía es importante para el código
 * cliente que pueda querer intentar varias actualizaciones y quedarse con la
 * primera que tenga éxito, sin restaurar estado manualmente.
 *
 * @throws std::invalid_argument Si `nueva_fecha` no cumple el formato
 *               esperado. La instancia **no se modifica** en ese caso.
 *
 * @note La asignación de un `std::string` puede lanzar `std::bad_alloc` si
 *       no hay memoria disponible. La función no captura esa excepción; se
 *       propaga tal cual y deja la instancia en su estado previo, porque la
 *       asignación de `std::string` ofrece garantía de excepción fuerte.
 */
void Uf::establecer_fecha(const std::string &nueva_fecha) {
    // Orden estricto: validar primero, asignar después. Si la validación
    // lanza, la instancia queda intacta (garantía de excepción fuerte).
    validar_formato_fecha(nueva_fecha);
    fecha_ = nueva_fecha;
}

/**
 * @brief Actualiza el valor, validando antes de asignar.
 *
 * @details
 * Análoga a `establecer_fecha` en su estructura y garantías. El orden
 * "validar → asignar" preserva la invariante de la clase y proporciona
 * garantía de excepción fuerte.
 *
 * @throws std::invalid_argument Si `nuevo_valor` viola las restricciones de
 *               dominio. La instancia **no se modifica** en ese caso.
 *
 * @note La asignación de un `double` es una operación trivial que no puede
 *       lanzar. La única fuente de excepción en esta función es la
 *       validación previa.
 *
 * @note El parámetro se declara `const double` en lugar de solo `double`.
 *       Esta decisión es puramente estilística —el calificador `const` sobre
 *       un parámetro por valor no afecta a la firma ni al comportamiento
 *       observable— pero deja constancia de que el argumento no se modifica
 *       dentro del cuerpo.
 */
void Uf::establecer_valor(const double nuevo_valor) {
    // Mismo orden que en establecer_fecha: validar primero, asignar después.
    validar_valor(nuevo_valor);
    valor_ = nuevo_valor;
}

/**
 * @brief Valida el formato de una fecha candidata.
 *
 * @details
 * La validación es **sintáctica**, no semántica: comprueba que la cadena
 * tenga la forma `YYYY-MM-DD` con cuatro dígitos para el año, dos para el
 * mes y dos para el día, separados por guiones. No comprueba que la fecha
 * exista en el calendario: `"2024-13-45"` pasa la validación porque el mes
 * 13 y el día 45 tienen dos dígitos cada uno, aunque no sean fechas reales.
 *
 * @par Por qué no se valida la semántica del calendario
 * La validación semántica requeriría convertir la cadena a una representación
 * de fecha —`std::chrono::year_month_day`, `std::tm`, etc.—, comprobar el
 * resultado y decidir una política ante fechas inválidas. Esa complejidad
 * solo se justifica si el proyecto necesita distinguir, por ejemplo, entre
 * `"2024-02-29"` (válido en año bisiesto) y `"2023-02-29"` (inválido). Para
 * el uso actual —registrar el valor diario de la UF, que siempre es una fecha
 * real publicada por el Banco Central— la validación sintáctica es suficiente
 * y deja la responsabilidad de la semántica al origen de los datos.
 *
 * @param fecha Cadena a validar.
 *
 * @throws std::invalid_argument Si la cadena no encaja con el patrón
 *               `^\d{4}-\d{2}-\d{2}$`. El mensaje incluye la cadena recibida
 *               para facilitar el diagnóstico.
 *
 * @note La expresión regular se declara `static const` para compilarla una
 *       única vez. Su inicialización es thread-safe desde C++11, así que la
 *       función puede invocarse concurrentemente desde múltiples hilos sin
 *       serialización adicional.
 */
void Uf::validar_formato_fecha(const std::string &fecha) {
    // Formato YYYY-MM-DD. La validación es sintáctica: comprueba la forma,
    // no que la fecha exista en el calendario. Ver documentación para el
    // análisis de por qué esta decisión es adecuada para el caso de uso.
    static const std::regex patron_fecha(R"(^\d{4}-\d{2}-\d{2}$)");
    if (!std::regex_match(fecha, patron_fecha)) {
        throw std::invalid_argument("Formato de fecha inválido. Se esperaba YYYY-MM-DD, se recibió: " + fecha);
    }
}

/**
 * @brief Valida las restricciones de dominio de un valor de UF.
 *
 * @details
 * A diferencia de la fecha, cuyo formato se comprueba con una expresión
 * regular sobre la representación textual, el valor llega ya como `double` y
 * la validación opera sobre reglas de dominio numéricas. Se comprueban dos
 * condiciones en orden:
 *
 *   1. **Finitud**: el valor debe ser un número real finito. Esta
 *      comprobación cubre tres casos distintos con una sola llamada:
 *      `std::isnan(valor)` (NaN), `std::isinf(valor) && valor > 0` (infinito
 *      positivo) y `std::isinf(valor) && valor < 0` (infinito negativo).
 *      Ninguno de los tres es un valor de UF válido.
 *
 *   2. **Positividad estricta**: el valor debe ser estrictamente mayor que
 *      cero. La UF, por definición, es una unidad de cuenta cuyo valor es
 *      siempre positivo; un valor de cero o negativo indicaría un error de
 *      datos o un caso patológico que no corresponde a ningún escenario real.
 *
 * @par Por qué se comprueba `isfinite` antes que la positividad
 * El orden importa. Si se comprobara primero `valor <= 0.0`, una instancia
 * con `valor = NaN` pasaría esa comprobación —porque toda comparación con
 * NaN es falsa, incluida `NaN <= 0.0`— y se aceptaría como válida,
 * propagando NaN por todo el sistema. Comprobar `isfinite` primero descarta
 * NaN antes de llegar a cualquier comparación numérica.
 *
 * @par Por qué no se comprueba también un máximo
 * Un valor de UF tiene un rango plausible conocido: históricamente ha pasado
 * de unas pocas unidades a decenas de miles. Fijar un máximo —por ejemplo,
 * `1'000.000`— descartaría valores absurdos por error de datos, pero también
 * haría que el código dejara de funcionar si en el futuro la UF superara ese
 * umbral por inflación alta. La política del proyecto es no imponer cotas
 * superiores que puedan quedar obsoletas silenciosamente.
 *
 * @param valor Valor a validar.
 *
 * @throws std::invalid_argument Si `valor` no es finito o no es estrictamente
 *               positivo. El mensaje distingue ambos casos para facilitar el
 *               diagnóstico: saber si el problema es un NaN o un valor
 *               negativo orienta hacia causas muy distintas.
 *
 * @note La función es `static` porque no depende de ningún estado de
 *       instancia. Declararla así permite invocarla sin haber construido un
 *       objeto `Uf`, lo cual es útil durante las propias rutas de
 *       construcción y también facilita las pruebas unitarias de la
 *       validación de forma aislada.
 */
void Uf::validar_valor(const double valor) {
    // NaN e infinito son double válidos para el compilador, pero no son una
    // UF. isfinite cubre las tres variantes (NaN, +inf, -inf) en una sola
    // comprobación. Se comprueba antes que la positividad porque NaN no
    // satisface ninguna comparación numérica, incluida '<= 0.0'.
    if (!std::isfinite(valor)) {
        throw std::invalid_argument("Valor de UF inválido: debe ser un número finito");
    }
    if (valor <= 0.0) {
        throw std::invalid_argument(
            "Valor de UF inválido: debe ser mayor que cero, se recibió: " + std::to_string(valor));
    }
}



/**
 * @brief Adaptador de `Uf` a la convención de serialización de nlohmann.
 *
 * @details
 * Función requerida por nlohmann para que `j.get<Uf>()` compile. En este
 * proyecto se implementa como una simple delegación a
 * `crear_uf_desde_json`, que es la función con la lógica real de parsing.
 *
 * @par Por qué existe si no se usa directamente
 * nlohmann descubre `from_json` por ADL cuando se invoca `j.get<Uf>()` y
 * requiere que exista, aunque el proyecto no utilice esa ruta. La delegación
 * evita duplicar la lógica de parsing en dos sitios con riesgo de
 * desincronización.
 *
 * @param j Objeto JSON de origen.
 * @param uf Referencia al `Uf` destino. Se **reasigna** con el resultado de
 *           `crear_uf_desde_json`, no se modifica in-place.
 *
 * @throws std::invalid_argument Si el JSON no satisface el contrato de
 *               `crear_uf_desde_json`.
 * @throws nlohmann::json::exception Si el JSON no tiene la estructura
 *               esperada.
 *
 * @see crear_uf_desde_json para el contrato completo.
 */
void from_json(const nlohmann::json &j, Uf &uf) {
    // Delegación completa: la lógica de parsing vive en crear_uf_desde_json
    // y aquí solo se adapta a la firma que exige nlohmann.
    uf = crear_uf_desde_json(j);
}

/**
 * @brief Construye un `Uf` validado desde JSON, aceptando dos representaciones del valor.
 *
 * @details
 * La función aplica validación en dos fases bien diferenciadas:
 *
 *   1. **Validación estructural del JSON**: comprueba que las claves
 *      esperadas existen y que sus tipos son los admitidos. Una clave ausente
 *      o un tipo incompatible se rechazan con `std::invalid_argument` antes
 *      de intentar cualquier conversión, evitando que nlohmann lance sus
 *      propias excepciones con mensajes menos contextualizados.
 *   2. **Validación de dominio**: delega en el constructor de `Uf`, que a su
 *      vez invoca los validadores de fecha y valor. El constructor es el
 *      punto único donde se comprueban las invariantes de la clase.
 *
 * @par Doble representación del campo "Valor"
 * El campo `"Valor"` puede llegar en dos formas distintas, y la función
 * acepta ambas:
 *
 *   - **Como número JSON** (`{"Valor": 5435.28}`): se extrae directamente con
 *     `get<double>()`. Es la forma natural cuando el JSON lo genera un
 *     programa.
 *   - **Como cadena en formato chileno** (`{"Valor": "5.435,28"}`): se pasa
 *     por `convertir_valor_chileno`. Es la forma habitual cuando el JSON se
 *     construye a partir de fuentes que preservan el formato original —una
 *     API que devuelve los valores tal como aparecen en el sitio web del
 *     Banco Central, una exportación desde Excel con formato regional, etc.
 *
 * Cualquier otro tipo de JSON (booleano, array, objeto, null) se rechaza con
 * `std::invalid_argument`. La distinción entre "número válido" y "cadena
 * válida" se hace con `is_number()` y `is_string()`, no con intentos de
 * conversión seguidos de captura de excepción: el chequeo explícito produce
 * mensajes de error más precisos y evita depender del comportamiento exacto
 * de las conversiones implícitas de nlohmann.
 *
 * @param j Objeto JSON del que extraer los campos. Se espera que contenga al
 *          menos las claves `"Fecha"` (cadena) y `"Valor"` (número o cadena).
 *
 * @return Instancia de `Uf` con los valores extraídos y validados.
 *
 * @throws std::invalid_argument Si el JSON no contiene las claves esperadas,
 *               si sus tipos no son los admitidos, si el valor en formato
 *               chileno no encaja con la gramática aceptada, o si los valores
 *               violan las invariantes de `Uf` (fecha mal formada, valor no
 *               finito o no positivo). La distinción entre estos casos queda
 *               reflejada en el mensaje de la excepción.
 *
 * @note La función no captura las excepciones del constructor: si el
 *       constructor lanza porque los datos violan las invariantes de `Uf`, la
 *       excepción se propaga sin transformación. Envolverla añadiría ruido
 *       sin información adicional, porque el mensaje del validador ya indica
 *       qué campo falló y por qué.
 *
 * @warning La función **no** ignora claves adicionales del JSON. Si el objeto
 *          contiene campos que `Uf` no reconoce, simplemente se descartan sin
 *          aviso. Si en el futuro se necesitara detectar JSON con claves
 *          inesperadas —por ejemplo, para detectar cambios de esquema
 *          silenciosos— habría que añadir una comprobación explícita de que
 *          `j.size() == 2` o de que todas las claves pertenecen a un conjunto
 *          conocido.
 */
Uf crear_uf_desde_json(const nlohmann::json &j) {
    // Validación fail-fast de existencia y tipo de los campos. Se comprueba
    // primero la presencia, después el tipo, para que el mensaje de error
    // distinga entre "falta el campo" y "el campo está pero es del tipo
    // equivocado", que son problemas operativamente distintos.
    if (!j.contains("Fecha") || !j.at("Fecha").is_string()) {
        throw std::invalid_argument("JSON inválido: falta o no es cadena el campo 'Fecha' en UF");
    }
    if (!j.contains("Valor")) {
        throw std::invalid_argument("JSON inválido: falta el campo 'Valor' en UF");
    }

    const std::string fecha = j.at("Fecha").get<std::string>();
    const nlohmann::json &valor_json = j.at("Valor");
    double valor = 0.0;

    // 'Valor' puede venir como número o como cadena en formato chileno. La
    // elección entre los dos caminos se hace con chequeos explícitos de tipo,
    // no con intentos de conversión y captura de excepción.
    if (valor_json.is_number()) {
        valor = valor_json.get<double>();
    } else if (valor_json.is_string()) {
        valor = convertir_valor_chileno(valor_json.get<std::string>());
    } else {
        throw std::invalid_argument("JSON inválido: el campo 'Valor' debe ser número o cadena");
    }

    // El constructor dispara las validaciones de dominio definidas en Uf.
    // Delegar en él —en lugar de asignar los campos directamente— garantiza
    // que la ruta de creación desde JSON aplica exactamente las mismas
    // invariantes que cualquier otra ruta de creación.
    return Uf(fecha, valor);
}
