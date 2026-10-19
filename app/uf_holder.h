// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// uf_holder.h
#ifndef APP_UF_HOLDER_H
#define APP_UF_HOLDER_H

#include <vector>
#include "uf.h"
#include "json.hpp"

/**
 * @brief Contenedor de registros de UF con encapsulamiento estricto de la colección interna.
 *
 * @details
 * Esta clase agrupa un conjunto de registros `Uf` —presumiblemente el
 * historial completo de valores publicados por el Banco Central en un
 * intervalo dado— y expone una interfaz deliberadamente minimalista para
 * consultarlos y mutarlos. La colección subyacente es un `std::vector<Uf>`,
 * pero ese detalle de implementación no forma parte del contrato: la clase
 * se reserva el derecho de cambiar el contenedor interno sin afectar al
 * código cliente, siempre que se preserve la semántica observable de los
 * métodos públicos.
 *
 * @par Por qué una clase contenedora y no un `std::vector<Uf>` directo
 * Envolver el vector en una clase dedicada —en lugar de pasar el vector
 * crudo entre funciones— aporta tres propiedades que el uso directo no
 * ofrece:
 *
 *   1. **Punto único de validación estructural.** Cualquier futura
 *      invariante sobre el conjunto —"las fechas no pueden repetirse",
 *      "los elementos deben estar ordenados ascendentemente"— tiene un
 *      lugar natural donde aplicarse: los métodos de mutación de esta
 *      clase. Sin envoltorio, cada consumidor tendría que verificar la
 *      invariante por su cuenta.
 *   2. **Punto de extensión de la interfaz.** Añadir operaciones derivadas
 *      —`obtener_uf_mas_reciente()`, `calcular_promedio()`,
 *      `filtrar_por_rango()`— tiene un sitio obvio donde vivir. Con un
 *      vector crudo, esas operaciones acabarían como funciones libres
 *      dispersas.
 *   3. **Frontera de serialización.** La función `from_json` que
 *      deserializa un `UfHolder` desde la estructura JSON del proyecto se
 *      declara aquí, no en un módulo separado, porque el formato de
 *      serialización del contenedor es parte de su contrato, no un
 *      detalle externo.
 *
 * @par Modelo de propiedad
 * `UfHolder` posee su colección por valor (`std::vector<Uf>`), sin punteros
 * ni referencias a datos externos. Las consecuencias son las habituales de
 * la propiedad directa:
 *
 *   - **Copiable**: el constructor de copia por miembro copia el vector
 *     completo, incluyendo cada `Uf` individual con su `std::string` de
 *     fecha. Para colecciones grandes, la operación es costosa pero
 *     correcta.
 *   - **Movible**: el constructor de movimiento por miembro mueve el búfer
 *     interno del vector a coste constante, independientemente del número
 *     de elementos. Un `UfHolder` movido queda con un vector vacío, estado
 *     válido aunque no útil.
 *   - **Sin recursos externos**: la clase no abre archivos, no establece
 *     conexiones, no reserva descriptores. Todo lo que posee son objetos
 *     `Uf` en memoria.
 *
 * @par Concurrencia
 * `UfHolder` **no es thread-safe para escrituras concurrentes**. Dos hilos
 * que invoquen simultáneamente `agregar_uf`, `limpiar_ufs` o
 * `establecer_ufs` sobre la misma instancia entran en condición de carrera,
 * y el estado observable al final es indeterminado. Tampoco es seguro leer
 * mientras otro hilo escribe: una llamada a `obtener_ufs` concurrente con
 * una mutación puede devolver una referencia a un búfer que está siendo
 * reasignado, produciendo comportamiento indefinido.
 *
 * Las **lecturas concurrentes sin escrituras** sí son seguras: múltiples
 * hilos pueden invocar `obtener_ufs` sobre la misma instancia sin
 * sincronización, siempre que ningún hilo esté mutándola. Esta propiedad es
 * relevante para el patrón habitual en el proyecto: un `UfHolder` poblado
 * en una fase monohilo, consultado desde una región OpenMP sin
 * modificaciones.
 *
 * @warning Las referencias devueltas por `obtener_ufs` quedan invalidadas
 *          por cualquier mutación posterior del contenedor, **incluida una
 *          mutación desde el mismo hilo**. El patrón de uso incorrecto más
 *          común es almacenar la referencia, agregar un elemento, y seguir
 *          leyendo el vector a través de la referencia antigua. Este error
 *          no produce una excepción ni un valor incorrecto detectable;
 *          produce comportamiento indefinido.
 *
 * @par Jerarquía y herencia
 * Al igual que `Uf`, esta clase **no está diseñada para ser heredada**. El
 * destructor se declara `= default` y no es virtual, y no hay ningún otro
 * método virtual. Heredar de `UfHolder` y usar la clase derivada a través
 * de un puntero a la base produce comportamiento indefinido en la
 * destrucción; ver la sección "Jerarquía y herencia" en la documentación de
 * `Uf` para el análisis completo, que aplica aquí sin cambios.
 *
 * @par Constructores
 * La clase declara dos constructores especiales y ninguno parametrizado:
 *
 *   - **Constructor por defecto** (`= default`): crea un `UfHolder` vacío.
 *     Es el único medio de construir una instancia, y es coherente con la
 *     semántica de "contenedor vacío que se puebla después" mediante
 *     `agregar_uf`, `establecer_ufs` o `from_json`.
 *   - **Constructor de copia y de movimiento**: generados implícitamente
 *     por el compilador, según la regla del cero. Copiar un `UfHolder`
 *     duplica su colección; moverlo transfiere el búfer interno a coste
 *     constante.
 *
 * La existencia del constructor por defecto es también la razón por la que
 * `from_json` puede escribirse con la firma convencional de nlohmann: la
 * biblioteca construye primero el `UfHolder` por defecto y luego invoca
 * `from_json` para poblarlo. Es un caso distinto del de `Uf`, que carece de
 * constructor por defecto y por eso no es utilizable con la ruta genérica
 * `get<Uf>()`.
 *
 * @see Uf para la entidad que compone la colección.
 * @see from_json para la ruta de deserialización desde JSON.
 */
class UfHolder {
public:
    /**
     * @brief Constructor por defecto: crea un contenedor vacío.
     *
     * @details
     * Declarado `= default` porque no hay nada que inicializar más allá del
     * miembro `ufs_`, que se construye por defecto como un vector vacío. La
     * declaración explícita —en lugar de omitir el constructor por
     * completo— tiene un propósito: dejar constancia de que el constructor
     * por defecto es parte del contrato público, no un residuo. Si en el
     * futuro se añadieran miembros que requirieran inicialización no
     * trivial, este es el sitio donde se añadiría.
     *
     * @note El constructor por defecto es también una precondición de la
     *       integración con nlohmann: la ruta `j.get<UfHolder>()` construye
     *       primero una instancia por defecto antes de invocar `from_json`.
     *       Si este constructor se eliminara, esa ruta dejaría de compilar.
     */
    UfHolder() = default;

    /**
     * @brief Destructor por defecto, no virtual.
     *
     * @details
     * La clase no gestiona recursos propios más allá del `std::vector<Uf>`,
     * cuyo destructor libera la memoria y destruye los elementos. El
     * destructor generado por el compilador es exactamente lo que se
     * necesita, y declararlo `= default` deja constancia explícita de esa
     * intención.
     *
     * @par Por qué no es virtual
     * El razonamiento es idéntico al de `Uf`: la clase no está diseñada
     * para ser heredada, y marcar el destructor como `virtual` añadiría 8
     * bytes por instancia más el coste de indirección en la llamada, sin
     * beneficio alguno. Si en el futuro se necesitara herencia, este es el
     * punto donde debe cambiarse el diseño, no en el sitio donde se
     * introduzca la primera clase derivada.
     */
    ~UfHolder() = default;

    /**
     * @brief Devuelve una vista de solo lectura de la colección de UF.
     *
     * @details
     * La función expone el vector interno mediante una referencia constante,
     * evitando tanto la copia del contenedor como la posibilidad de que el
     * llamador modifique su contenido. Este es el único canal de lectura
     * que la clase ofrece; no hay iteradores, no hay acceso indexado, no hay
     * métodos de consulta derivados. El llamador que necesite recorrer los
     * elementos lo hace directamente sobre la referencia devuelta:
     *
     * @code
     *   const auto& ufs = holder.obtener_ufs();
     *   for (const auto& uf : ufs) {
     *       procesar(uf.obtener_fecha(), uf.obtener_valor());
     *   }
     * @endcode
     *
     * @par Por qué se expone el vector completo y no métodos individuales
     * La alternativa —exponer `size()`, `operator[]`, `begin()`, `end()` y
     * compañía como métodos del `UfHolder`— duplicaría la interfaz de
     * `std::vector` sin aportar valor. La referencia constante al vector
     * pone a disposición del llamador toda la API de solo lectura del
     * contenedor subyacente, y si en el futuro se cambiara el tipo de
     * contenedor interno —por ejemplo, a `std::deque<Uf>` para optimizar
     * inserciones al principio— habría que mantener la misma fachada. La
     * exposición directa de la referencia es más simple y más honesta sobre
     * el tipo real del almacenamiento.
     *
     * @par Encapsulamiento y const-correctness
     * El vector se devuelve como `const std::vector<Uf>&`. El calificador
     * `const` sobre el tipo de retorno impide que el llamador invoque
     * métodos no constantes del vector —`push_back`, `clear`, `operator[]`
     * en su versión mutable—, de modo que la encapsulación se preserva sin
     * necesidad de copias defensivas. Los elementos del vector son también
     * `const Uf` en el contexto del llamador, así que no pueden modificarse
     * a través de esta referencia.
     *
     * @return Referencia constante al vector interno. Válida mientras la
     *         instancia sobre la que se invocó exista y no sea mutada por
     *         `agregar_uf`, `limpiar_ufs` o `establecer_ufs`.
     *
     * @par Complejidad
     * O(1): retornar una referencia no copia elementos ni recorre el
     * contenedor.
     *
     * @note El atributo `[[nodiscard]]` previene el error de invocar la
     *       función sin usar el resultado, que en una consulta pura sería
     *       siempre un error de programación.
     *
     * @warning La referencia retornada queda invalidada por la siguiente
     *          mutación del contenedor, sea desde el mismo hilo o desde
     *          otro. Ver la sección de concurrencia en la documentación de
     *          la clase para el análisis completo de los patrones de uso
     *          incorrecto.
     *
     * @see UfHolder::agregar_uf, UfHolder::limpiar_ufs,
     *      UfHolder::establecer_ufs para las operaciones que invalidan la
     *      referencia devuelta.
     */
    [[nodiscard]] const std::vector<Uf> &obtener_ufs() const noexcept;

    /**
     * @brief Añade una UF al final de la colección.
     *
     * @details
     * La operación añade un elemento preservando el contenido existente. Es
     * el método adecuado para construir la colección incrementalmente,
     * elemento a elemento, cuando los datos llegan de una fuente que no
     * produce un lote completo.
     *
     * Para cargar un lote completo de una sola vez —por ejemplo, tras
     * parsear un JSON con muchas UF— el método preferido es
     * `establecer_ufs`, que reemplaza el contenido en lugar de añadirlo y
     * permite reservar la capacidad necesaria de antemano.
     *
     * @param uf Instancia de `Uf` a añadir. Se copia al vector interno; el
     *           llamador conserva la propiedad del argumento. La copia
     *           incluye el `std::string` de la fecha, con la consiguiente
     *           reserva de memoria y copia de bytes.
     *
     * @throws std::bad_alloc Si la inserción requiere una reasignación del
     *               vector y no hay memoria disponible. El vector queda en
     *               su estado previo si la excepción se propaga.
     *
     * @par Complejidad
     * Amortizado O(1). En el caso peor aislado —cuando la inserción provoca
     * una reasignación—, la operación es O(n) porque copia los n elementos
     * existentes al nuevo búfer. La amortización a O(1) se debe a que las
     * reasignaciones ocurren con frecuencia geométricamente decreciente.
     *
     * @warning Esta operación **invalida** cualquier referencia obtenida
     *          previamente mediante `obtener_ufs`, así como cualquier
     *          iterador o puntero a elementos del vector. El llamador debe
     *          adquirir una nueva referencia si necesita seguir leyendo el
     *          contenedor tras la inserción.
     *
     * @warning No es seguro invocar este método concurrentemente con otras
     *          operaciones de mutación o lectura sobre la misma instancia.
     *          Ver la sección de concurrencia en la documentación de la
     *          clase.
     *
     * @see UfHolder::establecer_ufs para la operación de reemplazo total,
     *      más adecuada cuando se carga un lote completo.
     */
    void agregar_uf(const Uf &uf);

    /**
     * @brief Elimina todos los elementos de la colección, dejándola vacía.
     *
     * @details
     * La operación destruye todos los elementos y deja el vector con tamaño
     * cero. La **capacidad** del vector no se reduce: la memoria ya
     * reservada se conserva para futuras inserciones, evitando una nueva
     * reserva si el contenedor se vuelve a poblar en el corto plazo. Si el
     * llamador necesita recuperar esa memoria —por ejemplo, tras vaciar un
     * contenedor con miles de elementos que no se van a volver a
     * insertar—, debe reconstruir el `UfHolder` o esperar a que la clase
     * exponga una operación equivalente a `shrink_to_fit`.
     *
     * @par Por qué esta operación puede ser `noexcept`
     * `std::vector::clear` es `noexcept` cuando el destructor del tipo de
     * elemento es `noexcept`. `Uf` declara su destructor como `= default`,
     * y sus dos miembros (`std::string` y `double`) tienen destructores
     * `noexcept`; por transitividad, el destructor de `Uf` es `noexcept` y
     * `clear` también lo es. La declaración `noexcept` en esta firma es,
     * por tanto, una promesa respaldada por el lenguaje, no una suposición.
     *
     * @par Complejidad
     * O(n) en el tamaño del contenedor, donde n es el número de elementos
     * que se destruyen. El coste es lineal porque cada elemento debe
     * liberar su `std::string` de fecha individualmente.
     *
     * @warning Esta operación **invalida** cualquier referencia obtenida
     *          previamente mediante `obtener_ufs`, así como cualquier
     *          iterador o puntero a elementos del vector.
     *
     * @warning No es seguro invocar este método concurrentemente con otras
     *          operaciones de mutación o lectura sobre la misma instancia.
     *          Ver la sección de concurrencia en la documentación de la
     *          clase.
     */
    void limpiar_ufs() noexcept;

    /**
     * @brief Reemplaza el contenido completo de la colección.
     *
     * @details
     * La operación reemplaza **todos** los elementos actuales por los del
     * vector argumento. No es aditiva: si el contenedor tenía N elementos y
     * se le pasa un vector con M, el resultado tiene exactamente M
     * elementos, con los N anteriores destruidos. Esta semántica de
     * reemplazo total es la que distingue este método de `agregar_uf`, y es
     * la operación natural para cargar un lote completo.
     *
     * @par Uso desde from_json
     * La función `from_json` de este módulo invoca `establecer_ufs` con un
     * vector temporal que construye localmente. Ese vector es un rvalue
     * —no tiene nombre persistente más allá de la expresión—, y la firma
     * actual con `const std::vector<Uf>&` fuerza una copia en lugar de un
     * movimiento. Es un coste evitable que se documenta en el `.cpp` como
     * nota de mantenimiento: si se midiera un impacto real, la alternativa
     * sería cambiar la firma a paso por valor (`std::vector<Uf> nuevas_ufs`)
     * con `ufs_ = std::move(nuevas_ufs)` en el cuerpo, o añadir una
     * sobrecarga `establecer_ufs(std::vector<Uf>&&)`.
     *
     * @param nuevas_ufs Vector con el nuevo contenido. Se copia al miembro
     *                   interno; el llamador conserva la propiedad del
     *                   argumento.
     *
     * @throws std::bad_alloc Si la asignación requiere una reasignación del
     *               vector y no hay memoria disponible. La garantía de
     *               excepción del operador de asignación de `std::vector`
     *               depende de la implementación concreta de la biblioteca
     *               estándar; en la mayoría de implementaciones modernas es
     *               garantía fuerte.
     *
     * @par Complejidad
     * O(n) donde n es el tamaño del vector argumento, por la copia de los
     * elementos. Si el vector destino tenía capacidad suficiente, no hay
     * reasignación adicional; en caso contrario, se añade el coste de la
     * reasignación.
     *
     * @warning Esta operación **invalida** cualquier referencia obtenida
     *          previamente mediante `obtener_ufs`, así como cualquier
     *          iterador o puntero a elementos del vector.
     *
     * @warning No es seguro invocar este método concurrentemente con otras
     *          operaciones de mutación o lectura sobre la misma instancia.
     *          Ver la sección de concurrencia en la documentación de la
     *          clase.
     *
     * @see UfHolder::agregar_uf para la operación aditiva, adecuada cuando
     *      los datos llegan de a un elemento.
     */
    void establecer_ufs(const std::vector<Uf> &nuevas_ufs);

private:
    /**
     * @brief Colección interna de registros de UF.
     *
     * @details
     * Se usa `std::vector<Uf>` como almacenamiento por varias razones:
     *
     *   - **Acceso contiguo**: los elementos están en memoria contigua, lo
     *     que maximiza la localidad de caché durante la iteración —el
     *     patrón de acceso más común sobre esta colección—. Alternativas
     *     como `std::list` o `std::map` fragmentan la memoria y penalizan
     *     los recorridos secuenciales.
     *   - **Acceso indexado O(1)**: si en el futuro se necesita acceder a un
     *     elemento por posición —por ejemplo, para particionar el trabajo
     *     entre hilos en una región OpenMP—, el vector lo soporta sin
     *     coste adicional.
     *   - **Operaciones de cola eficientes**: `agregar_uf` inserta al final,
     *     que es la operación O(1) amortizada del vector. Las inserciones
     *     al principio o en medio serían O(n), pero la interfaz pública no
     *     las expone, así que esa debilidad del vector no es observable
     *     desde el contrato de la clase.
     *
     * @par El sufijo `_`
     * Sigue la convención del proyecto para miembros privados, permitiendo
     * distinguirlos de variables locales y parámetros sin prefijos más
     * verbosos.
     *
     * @note El miembro se declara privado y no se expone ninguna referencia
     *       mutable a él. El único canal de lectura es `obtener_ufs`, que
     *       devuelve una referencia constante; las mutaciones solo pueden
     *       realizarse a través de los tres métodos públicos de mutación.
     *       Esta separación es lo que justifica la afirmación de
     *       "encapsulamiento estricto" en la documentación de la clase.
     */
    std::vector<Uf> ufs_;
};

/**
 * @brief Deserializa un `UfHolder` completo desde un objeto JSON.
 *
 * @details
 * Función libre que nlohmann descubre por ADL (Argument-Dependent Lookup)
 * cuando se invoca `j.get<UfHolder>()` o cualquiera de sus variantes.
 * Deserializa la estructura `{"UFs": [...]}`, donde el array contiene
 * objetos JSON individuales que se convierten a `Uf` mediante
 * `crear_uf_desde_json`.
 *
 * @par Estructura JSON esperada
 * @code
 *   {
 *     "UFs": [
 *       {"Fecha": "2024-01-15", "Valor": 38542.15},
 *       {"Fecha": "2024-01-16", "Valor": "38.568,22"},
 *       ...
 *     ]
 *   }
 * @endcode
 *
 * El campo `"Valor"` de cada elemento admite tanto número JSON como cadena
 * en formato chileno; ver `crear_uf_desde_json` en `uf.h` para el contrato
 * detallado de cada conversión individual.
 *
 * @par Garantía de excepción fuerte
 * La función construye el nuevo contenido en un vector temporal y solo lo
 * inyecta en el `UfHolder` destino cuando el parsing completo ha tenido
 * éxito. Si cualquier paso intermedio lanza, el `UfHolder` destino queda
 * **exactamente** en su estado previo. Esta propiedad se documenta en
 * detalle en la implementación (`.cpp`); el contrato público la garantiza.
 *
 * @par Diferencia con `get<Uf>()`
 * A diferencia del caso de `Uf`, esta función **sí** es utilizable a través
 * de la ruta genérica de nlohmann, porque `UfHolder` tiene constructor por
 * defecto. Las dos formas —`j.get<UfHolder>()` y la invocación directa de
 * `from_json`— son equivalentes, y nlohmann descubre automáticamente esta
 * función para habilitar la primera.
 *
 * @param j Objeto JSON con la estructura `{"UFs": [...]}`. Se pasa por
 *          referencia constante: la función no lo modifica.
 * @param holder Referencia al `UfHolder` destino. Se modifica **solo** si
 *               todo el proceso de parsing termina con éxito; en caso de
 *               excepción, el estado previo se preserva íntegro.
 *
 * @throws std::invalid_argument Si el JSON no contiene la clave `"UFs"`,
 *               si el valor asociado no es un array, o si algún elemento
 *               del array viola las invariantes de `Uf` (fecha mal formada,
 *               valor no finito o no positivo, campo faltante).
 * @throws nlohmann::json::exception Si algún elemento del array tiene una
 *               estructura incompatible con lo que `crear_uf_desde_json`
 *               espera, en los casos que esa función no captura
 *               explícitamente.
 * @throws std::bad_alloc Si la reserva del vector temporal o alguna de las
 *               conversiones internas agota la memoria disponible.
 *
 * @note La función existe tanto para satisfacer la convención de nlohmann
 *       como para uso directo. A diferencia de `from_json` para `Uf` —que
 *       solo existe por convención—, aquí ambas rutas son legítimas y
 *       equivalentes.
 *
 * @warning El `UfHolder` destino **no** se vacía antes de la operación. Si
 *          la llamada tiene éxito, su contenido anterior se reemplaza
 *          íntegramente por el nuevo; si falla, su contenido anterior
 *          permanece intacto. La operación es de reemplazo, no aditiva; si
 *          se necesita comportamiento aditivo, debe invocarse `agregar_uf`
 *          en bucle o componer el vector a mano.
 *
 * @see Uf para la entidad que compone la colección.
 * @see crear_uf_desde_json en `uf.h` para el contrato de conversión de cada
 *      elemento.
 */
void from_json(const nlohmann::json &j, UfHolder &holder);

#endif // APP_UF_HOLDER_H
