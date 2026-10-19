// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

#ifndef APP_UF_H
#define APP_UF_H

#include <string>
#include "json.hpp"

/**
 * @brief Representa un valor puntual de la Unidad de Fomento (UF) en una fecha concreta.
 *
 * @details
 * Esta clase modela la entidad de dominio más pequeña del proyecto: un par
 * (fecha, valor) que registra el valor oficial de la UF chilena en un día
 * determinado. La UF es una unidad de cuenta indexada a la inflación, usada
 * en Chile para préstamos hipotecarios, contratos y otras operaciones
 * financieras; su valor es publicado diariamente por el Banco Central y
 * varía de un día a otro.
 *
 * @par Rol en el proyecto
 * `Uf` es un **objeto de valor** (value object): su identidad está definida
 * por el contenido de sus dos campos, no por una referencia. Dos instancias
 * con la misma fecha y el mismo valor son intercambiables a efectos prácticos.
 * Esta propiedad es la que justifica las decisiones de diseño que siguen.
 *
 * @par Inmutabilidad efectiva frente a setters expuestos
 * La clase expone dos setters públicos —establecer_fecha() y
 * establecer_valor()— lo que a primera vista sugiere mutabilidad irrestricta.
 * La intención de diseño, sin embargo, es más matizada:
 *
 *   - La **mutabilidad existe** porque hay casos de uso legítimos donde un
 *     objeto `Uf` se construye vacío o con valores preliminares y se rellena
 *     después (por ejemplo, durante un proceso de carga por etapas).
 *   - La **validación es obligatoria** en cada mutación, no solo en el
 *     constructor. Los setters no permiten asignar valores que violarían las
 *     invariantes de la clase.
 *   - Como consecuencia, no existe un estado "inválido" alcanzable desde
 *     código cliente: cualquier instancia de `Uf` que exista en un momento
 *     dado satisface las invariantes, hayan sido establecidos sus campos en
 *     el constructor o mediante los setters.
 *
 * Si en el futuro se decidiera convertir `Uf` en un tipo estrictamente
 * inmutable —eliminando los setters y obligando a construir una instancia
 * nueva para cada cambio— esta es la sección de la documentación que debería
 * reescribirse, junto con los métodos afectados.
 *
 * @par Jerarquía y herencia
 * `Uf` **no está diseñada para ser heredada**. El destructor se declara
 * `= default` y no es virtual, y la clase no tiene otros métodos virtuales.
 * Esta combinación tiene una consecuencia que conviene tener presente:
 *
 *   - Heredar de `Uf` y usar la clase derivada a través de un puntero a la
 *     base es un error de programación. Si el puntero es de tipo `Uf*` y
 *     apunta a una instancia de una clase derivada, el `delete` invocará el
 *     destructor no virtual de `Uf` y **no** el destructor de la derivada,
 *     produciendo comportamiento indefinido (fuga de recursos de la parte
 *     derivada, corrupción del heap en casos extremos).
 *   - Si en el futuro se necesitara herencia, el cambio correcto es marcar
 *     el destructor como `virtual`, no simplemente añadir clases derivadas.
 *     Ese cambio añadiría un puntero a la tabla virtual a cada instancia
 *     —típicamente 8 bytes en plataformas de 64 bits— y rompería la
 *     compatibilidad binaria con código ya compilado contra la versión sin
 *     `virtual`. Es una decisión de coste real, no un detalle cosmético.
 *
 * @par Copia y movimiento
 * La clase no declara ninguno de los seis operadores especiales (constructor
 * de copia, constructor de movimiento, operadores de asignación, destructor)
 * más allá del destructor `= default`. Las consecuencias son las habituales
 * de la *regla del cero*:
 *
 *   - **Copiable**: se genera el constructor de copia por miembro. Copiar un
 *     `Uf` copia el `std::string` de la fecha y el `double` del valor, sin
 *     sorpresas ni estado compartido.
 *   - **Movible**: se genera el constructor de movimiento por miembro. Mover
 *     un `Uf` mueve el `std::string` (barato, no copia el contenido) y copia
 *     el `double` (indistinguible de moverlo). El objeto origen queda en un estado válido, pero no especificado.
 *   - **Excepciones**: el constructor de copia puede lanzar `std::bad_alloc`
 *     si el `std::string` de la fecha no puede duplicarse. El constructor de
 *     movimiento es `noexcept` en la práctica, aunque no se declara
 *     explícitamente aquí.
 *
 * @par Concurrencia
 * La clase **no es thread-safe para escrituras concurrentes**. Dos hilos que
 * invoquen simultáneamente `establecer_fecha` o `establecer_valor` sobre la
 * misma instancia entran en condición de carrera, y el resultado observable
 * —qué valor gana— es indeterminado. Tampoco es seguro leer mientras otro
 * hilo escribe: una lectura concurrente con una escritura puede observar un
 * `std::string` a medio modificar.
 *
 * Las lecturas concurrentes sin escrituras sí son seguras: los getters no
 * modifican estado y pueden invocarse desde múltiples hilos simultáneamente
 * sobre la misma instancia, siempre que ningún hilo esté mutándola.
 *
 * El patrón recomendado en aplicaciones con OpenMP es, por tanto, construir
 * todos los `Uf` en una fase monohilo y consultarlos desde la región
 * paralela sin modificarlos, o bien usar una instancia independiente por
 * hilo si se necesita mutación.
 *
 * @par Rutas de deserialización JSON
 * La clase ofrece **dos** formas de construirse desde JSON, y conviene
 * entender cuándo usar cada una:
 *
 *   - `from_json(const nlohmann::json&, Uf&)` — función libre que sigue la
 *     convención de nlohmann para integración con `get<Uf>()`, `j.get<Uf>()`
 *     y `nlohmann::json::get_to()`. Requiere que `Uf` sea construible por
 *     defecto, porque nlohmann construye primero la instancia y luego la
 *     rellena mediante `from_json`. Como `Uf` no tiene constructor por
 *     defecto (el único constructor toma fecha y valor), la ruta
 *     `get<Uf>()` de nlohmann no está disponible en este proyecto; la
 *     función existe únicamente para satisfacer la convención de la
 *     biblioteca y delega toda su lógica en `crear_uf_desde_json`.
 *   - `crear_uf_desde_json(const nlohmann::json&)` — función libre que
 *     construye un `Uf` validado a partir de un JSON, sin pasar por la
 *     ruta genérica de nlohmann. Es la forma recomendada en este proyecto.
 *
 * Ambas funciones aplican las mismas validaciones, porque `from_json`
 * delega en `crear_uf_desde_json`. Ver la documentación individual de cada
 * función para el contrato detallado.
 *
 * @see Uf::Uf para el contrato del constructor.
 * @see crear_uf_desde_json para la ruta de deserialización recomendada.
 */
class Uf {
public:
    /**
     * @brief Construye un registro de UF validado desde sus dos componentes.
     *
     * @details
     * El constructor aplica la misma validación que los setters, de modo que
     * ninguna instancia de `Uf` puede existir en un estado que viole las
     * invariantes de la clase. Este comportamiento *fail-fast* es preferible
     * a la alternativa —construir sin validar y comprobar más tarde— porque
     * localiza el error en el punto donde se introdujo el dato inválido, en
     * lugar de propagarlo hasta un punto lejano donde la causa original ya
     * no es evidente.
     *
     * @param fecha Fecha del registro en el formato que la clase acepta como
     *              válido (ver `validar_formato_fecha` en el `.cpp` para la
     *              gramática exacta). Se pasa por valor; el constructor la copia a `fecha_` mediante `establecer_fecha`.
     * @param valor Valor de la UF en la fecha indicada. Debe satisfacer las
     *              restricciones documentadas por `validar_valor`.
     *
     * @throws std::invalid_argument Si `fecha` no cumple el formato esperado.
     * @throws std::invalid_argument Si `valor` no satisface las restricciones de dominio.
     *
     * @note El constructor está marcado `explicit` para prevenir
     *       conversiones implícitas. Sin este marcador, un `double` o un
     *       `std::string` podrían convertirse accidentalmente a `Uf` en
     *       contextos donde el compilador busca una conversión —paso de
     *       argumentos, retornos de función, inicialización de contenedores—
     *       produciendo instancias con un valor por defecto para el otro
     *       campo, algo casi siempre indeseado.
     *
     * @warning Una instancia construida con éxito es siempre válida. Si el
     *          constructor lanza, no se obtiene ninguna instancia: el objeto
     *          no llega a existir y no hay estado parcial que limpiar.
     */
    explicit Uf(std::string fecha, double valor);

    /**
     * @brief Destructor por defecto, no virtual.
     *
     * @details
     * La clase no gestiona recursos propios: sus dos miembros (`std::string`
     * y `double`) tienen destructores triviales o gestionados por RAII, y no
     * hay punteros crudos, descriptores ni handles que liberar. El destructor
     * generado por el compilador es, por tanto, exactamente lo que se
     * necesita, y declararlo `= default` deja constancia explícita de esa
     * intención.
     *
     * @par Por qué no es virtual
     * Marcar el destructor como `virtual` tendría dos efectos:
     *
     *   1. **Coste de memoria**: cada instancia de `Uf` incorporaría un
     *      puntero a la tabla virtual, típicamente 8 bytes en plataformas de
     *      64 bits. Con dos miembros que ocupan del orden de 40 bytes, eso
     *      supone un 20 % de sobrecarga por objeto.
     *   2. **Coste de indirección**: la llamada al destructor pasaría por la
     *      tabla virtual incluso cuando el tipo se conoce en tiempo de
     *      compilación, impidiendo la inlineación en el caso común.
     *
     * Dado que la clase **no está diseñada para ser heredada**, ambos costes
     * se pagarían sin beneficio. La decisión de no marcar `virtual` es
     * deliberada y coherente con la ausencia de cualquier otro método
     * virtual en la clase.
     *
     * @warning Heredar de `Uf` es un error de programación. Si una clase
     *          derivada se destruye a través de un puntero a `Uf`, el
     *          comportamiento es indefinido. Ver la sección "Jerarquía y
     *          herencia" en la documentación de la clase para el análisis
     *          completo.
     */
    ~Uf() = default;

    /**
     * @brief Devuelve la fecha del registro, por referencia constante.
     *
     * @details
     * La fecha es un `std::string` y devolverla por valor implicaría copiar
     * su contenido en cada invocación. Para una cadena de fecha típica
     * (alrededor de 10 caracteres en formato ISO), la copia es barata pero
     * no gratuita: una reserva de memoria dinámica y una copia de bytes. Con
     * esta función invocada en bucles de proceso —por ejemplo, al exportar
     * miles de registros a CSV— el coste agregado es tangible.
     *
     * Devolver por referencia constante elimina la copia a coste cero. La
     * contrapartida es que el llamador debe ser consciente de la validez de
     * la referencia: si la instancia de `Uf` se modifica o se destruye, la
     * referencia deja de ser válida.
     *
     * @return Referencia constante a la fecha almacenada. La referencia es
     *         válida mientras la instancia `Uf` sobre la que se invocó la
     *         función siga existiendo y no sea modificada por
     *         `establecer_fecha`.
     *
     * @par Por qué esta función es `noexcept`
     * No construye objetos, no asigna memoria y no puede fallar. Declararla
     * `noexcept` permite al compilador omitir el código de manejo de
     * excepciones en los puntos de llamada y habilita optimizaciones
     * adicionales en contextos donde la función se usa dentro de expresiones
     * `noexcept` más amplias.
     *
     * @note El atributo `[[nodiscard]]` previene el error de invocar la
     *       función sin usar el resultado, que en una consulta pura sería
     *       siempre un error de programación.
     *
     * @warning La referencia retornada deja de ser válida si se invoca
     *          `establecer_fecha` sobre la misma instancia. Este es el
     *          precio de evitar la copia y la razón por la que el llamador
     *          debe copiar el valor si necesita conservarlo más allá del
     *          siguiente punto de mutación.
     */
    [[nodiscard]] const std::string &obtener_fecha() const noexcept;

    /**
     * @brief Devuelve el valor de la UF.
     *
     * @details
     * A diferencia de la fecha, el valor se retorna **por copia**. La razón
     * es que un `double` ocupa 8 bytes y su copia tiene el mismo coste que
     * copiar una referencia —también 8 bytes en plataformas de 64 bits— sin
     * los problemas de validez que introduce devolver una referencia a un
     * miembro mutable. Retornar por copia es, en este caso, simultáneamente
     * más simple y no más costoso.
     *
     * @return Valor de la UF. Es una copia independiente del miembro interno:
     *         modificaciones posteriores a la instancia no afectan al valor
     *         retornado, y viceversa.
     *
     * @par Por qué esta función es `noexcept`
     * Copiar un `double` es una operación trivial que no puede fallar. La
     * declaración `noexcept` habilita las mismas optimizaciones que en
     * `obtener_fecha`, sin ninguna de sus restricciones de validez.
     *
     * @note El atributo `[[nodiscard]]` sigue la misma lógica que en
     *       `obtener_fecha`: la función es pura y descartar su resultado no
     *       tiene sentido.
     *
     * @see obtener_fecha para el caso análogo con la fecha, donde la
     *      decisión de retorno por referencia sí tiene consecuencias.
     */
    [[nodiscard]] double obtener_valor() const noexcept;

    /**
     * @brief Actualiza la fecha del registro, aplicando validación.
     *
     * @details
     * La mutación no es libre: el nuevo valor pasa por la misma validación
     * que el constructor aplica, de modo que la invariante de la clase —"la
     * fecha tiene siempre un formato válido"— se preserva a lo largo de toda
     * la vida del objeto.
     *
     * @param nueva_fecha Nueva fecha a asignar. Debe cumplir el mismo formato
     *                    que el constructor exige; se pasa por referencia
     *                    constante porque el setter va a copiarla al miembro
     *                    interno y el llamador conserva la propiedad del
     *                    argumento.
     *
     * @throws std::invalid_argument Si `nueva_fecha` no cumple el formato esperado. En ese
     *               caso, la instancia **no se modifica**: la validación
     *               ocurre antes de la asignación, de modo que un intento
     *               fallido de mutación deja el objeto en su estado previo.
     *
     * @note Esta función **no** es `noexcept`: la validación puede lanzar y
     *       la asignación del `std::string` puede agotar memoria. Declararla
     *       `noexcept` sería incorrecto.
     *
     * @warning El uso de este setter sobre una instancia compartida entre
     *          hilos no es seguro sin sincronización externa. Ver la sección
     *          "Concurrencia" en la documentación de la clase.
     *
     * @see establecer_valor para el setter análogo sobre el otro miembro.
     */
    void establecer_fecha(const std::string &nueva_fecha);

    /**
     * @brief Actualiza el valor de la UF, aplicando validación.
     *
     * @details
     * Análoga a `establecer_fecha` en su política: el nuevo valor pasa por
     * la misma validación que el constructor, la asignación se realiza solo
     * si la validación es exitosa, y un intento fallido deja la instancia en
     * su estado previo.
     *
     * @param nuevo_valor Nuevo valor a asignar. Debe satisfacer las
     *                    restricciones documentadas por `validar_valor`.
     *
     * @throws std::invalid_argument Si `nuevo_valor` no satisface las restricciones de
     *               dominio. En ese caso, la instancia **no se modifica**.
     *
     * @note La asignación de un `double` es una operación trivial que no
     *       puede fallar por sí misma; la única fuente de excepción en esta
     *       función es la validación previa.
     *
     * @warning El uso de este setter sobre una instancia compartida entre
     *          hilos no es seguro sin sincronización externa. Ver la sección
     *          "Concurrencia" en la documentación de la clase.
     */
    void establecer_valor(double nuevo_valor);

private:
    /**
     * @brief Valida el formato de una fecha candidata.
     *
     * @details
     * Función auxiliar compartida entre el constructor y `establecer_fecha`,
     * de modo que ambos caminos de entrada a la clase aplican exactamente la
     * misma regla. Se declara `static` porque no depende de ningún estado de
     * instancia: su única entrada es la cadena a validar.
     *
     * @par Por qué es privada y no un método público de validación
     * Exponer la validación como método público tendría dos consecuencias
     * indeseadas:
     *
     *   1. **Duplicaría la superficie de la API**: el llamador podría
     *      invocarla para pre-validar datos antes de construir un `Uf`, lo
     *      cual es legítimo pero redundante, porque el constructor valida de
     *      todas formas.
     *   2. **Invitaría a comprobar la validez de una fecha sin construir un
     *      `Uf`**, sembrando la duda de si existe un estado intermedio
     *      "validado pero no construido" que la clase tendría que respetar.
     *
     * Mantenerla privada deja claro que la validación es un detalle de
     * implementación, no parte del contrato público.
     *
     * @param fecha Cadena a validar.
     *
     * @throws std::invalid_argument Si la cadena no cumple el formato esperado. El mensaje
     *               debe describir la discrepancia con suficiente detalle
     *               para que el operador que depure un archivo de datos
     *               pueda corregirlo.
     *
     * @note El formato concreto que esta función acepta está documentado en
     *       su definición en el `.cpp`, no aquí. Esta cabecera describe el
     *       **contrato** —"la fecha debe tener un formato válido"—; los
     *       detalles gramaticales del formato pertenecen a la implementación.
     */
    static void validar_formato_fecha(const std::string &fecha);

    /**
     * @brief Valida el valor numérico de una UF candidata.
     *
     * @details
     * Función auxiliar compartida entre el constructor y `establecer_valor`,
     * análoga a `validar_formato_fecha` en su estructura y en las razones
     * para mantenerla privada y `static`.
     *
     * @param valor Valor a validar.
     *
     * @throws std::invalid_argument Si el valor no satisface las restricciones de dominio.
     *               El mensaje debe indicar qué restricción se violó y con
     *               qué valor, para facilitar la corrección.
     *
     * @note Las restricciones de dominio concretas —rango admisible,
     *       tratamiento de `NaN`, de infinitos, de valores negativos— están
     *       documentadas en la definición de esta función en el `.cpp`.
     */
    static void validar_valor(double valor);

    /**
     * @brief Fecha del registro de UF.
     *
     * @details
     * Almacenada como `std::string` en lugar de un tipo de fecha dedicado
     * (`std::chrono::year_month_day`, `boost::gregorian::date`, etc.) por
     * simplicidad de serialización. El formato se valida en el constructor
     * y en el setter, de modo que el contenido de este campo es siempre una
     * fecha válida en el formato documentado por `validar_formato_fecha`.
     *
     * @note El sufijo `_` sigue la convención del proyecto para miembros
     *       privados, permitiendo distinguirlos de variables locales y
     *       parámetros sin prefijos más verbosos.
     */
    std::string fecha_;

    /**
     * @brief Valor de la UF en la fecha indicada.
     *
     * @details
     * Se usa `double` en lugar de un tipo de precisión decimal
     * (`long double`, `boost::multiprecision::cpp_dec_float`, etc.) porque
     * el valor de la UF publicado oficialmente tiene precisión limitada
     * —típicamente cuatro decimales— y los cálculos posteriores del proyecto
     * no requieren precisión arbitraria. Si en el futuro se necesitara
     * aritmética decimal exacta —por ejemplo, para cálculos financieros con
     * redondeo normativo— este miembro y todos los puntos que lo leen serían
     * los candidatos al cambio.
     */
    double valor_;
};

/**
 * @brief Adaptador de `Uf` a la convención de serialización de nlohmann.
 *
 * @details
 * Función libre que nlohmann descubre por ADL (Argument-Dependent Lookup)
 * cuando se invoca `j.get<Uf>()` o cualquiera de las variantes equivalentes.
 * La convención requiere que exista, aunque el proyecto no utilice esa ruta
 * —nlohmann necesita construir un `Uf` por defecto antes de invocar
 * `from_json`, y `Uf` no tiene constructor por defecto—. La implementación
 * delega íntegramente en `crear_uf_desde_json`, que es donde vive la lógica
 * real de parsing y validación.
 *
 * @par Validación
 * Esta función **sí** aplica todas las validaciones de la clase, porque su
 * única acción es delegar en `crear_uf_desde_json`, que a su vez construye
 * el `Uf` mediante el constructor público. No existe divergencia entre el
 * contrato de esta función y el de la ruta recomendada.
 *
 * @param j Objeto JSON del que extraer los campos. Se espera que contenga al
 *          menos las claves que `Uf` necesita para construirse.
 * @param uf Referencia al `Uf` a poblar. Se **reasigna** con el resultado de
 *           `crear_uf_desde_json`, no se modifica in-place.
 *
 * @throws std::invalid_argument Si el JSON no satisface el contrato de
 *               `crear_uf_desde_json` o si los valores violan las
 *               invariantes de `Uf`.
 * @throws nlohmann::json::exception Si el JSON no tiene la estructura
 *               esperada.
 *
 * @note La función existe exclusivamente para satisfacer la convención de
 *       nlohmann. Su uso directo en el proyecto está desaconsejado: la ruta
 *       preferida es `crear_uf_desde_json`, que no depende del contrato de
 *       la biblioteca de JSON.
 *
 * @see crear_uf_desde_json para la ruta de deserialización recomendada.
 */
void from_json(const nlohmann::json &j, Uf &uf);

/**
 * @brief Construye un `Uf` validado a partir de un objeto JSON.
 *
 * @details
 * Función alternativa a la ruta genérica de nlohmann (`get<Uf>()`), que no
 * es utilizable porque `Uf` carece de constructor por defecto. Esta función
 * extrae los campos del JSON, los pasa al constructor de `Uf` y retorna la
 * instancia resultante. Como el constructor valida, el `Uf` retornado
 * satisface todas las invariantes de la clase sin necesidad de comprobaciones
 * adicionales por parte del llamador.
 *
 * @par Doble representación del campo `"Valor"`
 * El campo `"Valor"` puede llegar como número JSON o como cadena en formato
 * chileno (`"5.435,28"`). Ambos casos se aceptan; cualquier otro tipo se
 * rechaza con `std::invalid_argument`. Los detalles de la conversión desde
 * formato chileno están documentados en el `.cpp`.
 *
 * @par Por qué una función libre y no un método estático de `Uf`
 * La función podría haberse declarado como `static Uf Uf::desde_json(...)`,
 * pero se ha optado por una función libre por dos razones:
 *
 *   1. **Simetría con `from_json`**: la función complementaria ya es libre
 *      por exigencia de la convención de nlohmann. Mantener ambas como
 *      funciones libres evita la impresión de que una es "más oficial" que
 *      la otra.
 *   2. **Desacoplamiento de la cabecera**: la firma de `crear_uf_desde_json`
 *      menciona `nlohmann::json` en sus parámetros. Si se declarara dentro
 *      de la clase, arrastraría la inclusión de `<json.hpp>` a cualquier
 *      unidad de traducción que incluya `uf.h`, incluso cuando no la use.
 *      Como función libre en la cabecera, el efecto es el mismo —la
 *      inclusión sigue siendo necesaria— pero la separación visual deja más
 *      claro qué partes de la interfaz dependen de JSON y cuáles no.
 *
 * @param j Objeto JSON con los campos necesarios. Se pasa por referencia
 *          constante: la función no lo modifica.
 *
 * @return Instancia de `Uf` con los valores extraídos del JSON, validada
 *         por el constructor.
 *
 * @throws std::invalid_argument Si el JSON no contiene las claves esperadas,
 *               si sus tipos no son los admitidos, si el valor en formato
 *               chileno no encaja con la gramática aceptada, o si los
 *               valores violan las invariantes de `Uf`.
 * @throws nlohmann::json::exception Si el JSON no tiene la estructura
 *               esperada.
 *
 * @note La distinción entre las dos categorías de excepción es relevante
 *       para el llamador: `nlohmann::json::exception` indica un problema
 *       estructural del JSON (clave ausente, tipo incompatible),
 *       mientras que `std::invalid_argument` indica que el JSON es
 *       estructuralmente correcto pero contiene datos inválidos. Un proceso
 *       de carga masiva puede querer tratar estos casos de forma distinta.
 *
 * @see from_json para la función complementaria de la convención de nlohmann.
 * @see Uf::Uf para el contrato del constructor, que es donde se aplican las
 *      validaciones que esta función hereda.
 */
Uf crear_uf_desde_json(const nlohmann::json &j);

#endif // APP_UF_H
