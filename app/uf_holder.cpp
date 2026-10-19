// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

// uf_holder.cpp
#include "uf_holder.h"
#include <stdexcept>


/**
 * @brief Retorna el vector interno de UF por referencia constante.
 *
 * @details
 * Devolver por referencia constante —en lugar de por copia— es la decisión
 * clave de este método. El vector puede contener cientos o miles de `Uf`, y
 * una copia por llamada implicaría duplicar cada `std::string` de fecha
 * almacenado, con la consiguiente reserva de memoria y copia de bytes. Para
 * el uso previsto —recorrer el vector para exportarlo, calcular agregados,
 * serializarlo de nuevo a JSON— la referencia evita ese coste por completo.
 *
 * La contrapartida es idéntica a la de `Uf::obtener_fecha`: la referencia
 * deja de ser válida en cuanto la instancia se modifica. Cualquier operación
 * que pueda alterar el tamaño o el contenido del vector —`agregar_uf`,
 * `limpiar_ufs`, `establecer_ufs`— invalida las referencias obtenidas con
 * anterioridad, igual que cualquier operación de `std::vector` que cause
 * reasignación.
 *
 * @return Referencia constante al vector interno. Los elementos son `const
 *         Uf` y no pueden modificarse a través de esta referencia, lo cual
 *         preserva la encapsulación del contenedor.
 *
 * @par Por qué esta función es `noexcept`
 * Retornar una referencia no construye objetos, no asigna memoria y no puede
 * fallar. Declararla `noexcept` permite al compilador aplicar las mismas
 * optimizaciones que en los getters de `Uf`.
 *
 * @warning La referencia retornada queda invalidada por cualquier mutación
 *          posterior del contenedor. Un patrón de uso incorrecto típico es
 *          almacenar la referencia, agregar un elemento, y seguir usando la
 *          referencia antigua. El comportamiento en ese caso es
 *          comportamiento indefinido, no una excepción ni un valor
 *          incorrecto detectable.
 *
 * @note El atributo `[[nodiscard]]` de la declaración previene el error de invocar la función sin usar el
 *       resultado, que en un getter puro sería siempre un error de
 *       programación.
 */
const std::vector<Uf> &UfHolder::obtener_ufs() const noexcept {
    return ufs_;
}

/**
 * @brief Añade una UF al final del contenedor, copiándola.
 *
 * @details
 * La operación delega en `std::vector::push_back`, que añade el elemento al
 * final y ajusta la capacidad si es necesario. Cuando el vector ya tiene
 * capacidad suficiente, la operación es O(1); cuando debe crecer, la
 * reasignación copia todos los elementos existentes y el coste agregado es
 * amortizado O(1) por inserción.
 *
 * @par Por qué toma el argumento por referencia constante
 * La firma `agregar_uf(const Uf&)` acepta tanto lvalues como rvalues, y en
 * ambos casos copia el argumento al vector. Para un lvalue, esto es lo
 * esperado; para un rvalue, se desaprovecha la oportunidad de mover.
 *
 * La alternativa —sobrecargas separadas para `const Uf&` y `Uf&&`, o una
 * única firma con paso por valor y `std::move` interno— sería más eficiente
 * para el caso de rvalues, pero no se ha implementado en la versión actual.
 * Si en el futuro se detecta un caso de uso donde se construyan Uf
 * temporales y se agreguen en bucle, esta es la optimización a considerar:
 *
 * @code
 *   // Alternativa con paso por valor + move: una sola firma, óptima para
 *   // lvalues (copia) y rvalues (mueve).
 *   void agregar_uf(Uf uf) {
 *       ufs_.push_back(std::move(uf));
 *   }
 * @endcode
 *
 * La decisión de no aplicarla ahora responde a que el caso de uso actual
 * —parsear JSON y poblar el contenedor por lotes— pasa por
 * `establecer_ufs`, no por `agregar_uf`, y no se ha medido impacto real en
 * un caso con rvalues.
 *
 * @throws std::bad_alloc Si la inserción requiere una reasignación del
 *               vector y no hay memoria disponible. En ese caso, el vector
 *               queda en su estado previo: `std::vector::push_back` ofrece
 *               garantía de excepción fuerte cuando el tipo de elemento
 *               tiene constructor de copia `noexcept` o cuando la operación
 *               no requiere reasignación.
 *
 * @note La declaración de la cabecera debería reflejar si el método se
 *       considera `noexcept` o no. En la implementación actual puede lanzar
 *       por `std::bad_alloc`, así que **no** debería marcarse `noexcept`.
 *
 * @warning No es seguro invocar este método concurrentemente con otras
 *          operaciones de mutación sobre la misma instancia, ni con
 *          invocaciones concurrentes de `obtener_ufs` que accedan a la
 *          referencia devuelta. Ver la sección de concurrencia en la
 *          documentación de la clase.
 */
void UfHolder::agregar_uf(const Uf &uf) {
    ufs_.push_back(uf);
}

/**
 * @brief Vacía el contenedor, eliminando todos sus elementos.
 *
 * @details
 * La operación delega en `std::vector::clear`, que destruye todos los
 * elementos en orden inverso al de inserción y deja el vector con tamaño
 * cero. La **capacidad** del vector no se reduce: los bytes ya reservados se
 * conservan para futuras inserciones, evitando una nueva reserva si se
 * vuelve a poblar el contenedor inmediatamente después.
 *
 * @par Por qué esta función sí es `noexcept` mientras `agregar_uf` no lo es
 * `std::vector::clear` es `noexcept` cuando el destructor del tipo de
 * elemento es `noexcept`. `Uf` declara su destructor como `= default` y
 * todos sus miembros (`std::string` y `double`) tienen destructores
 * `noexcept`; por transitividad, el destructor de `Uf` es implícitamente
 * `noexcept`, y `clear` también lo es. La declaración `noexcept` aquí es,
 * por tanto, correcta y no una promesa vacía.
 *
 * En cambio, `agregar_uf` puede lanzar `std::bad_alloc` desde la
 * reasignación, y no puede marcarse `noexcept`.
 *
 * @par Liberación de memoria
 * Esta función **no** libera la memoria reservada por el vector. Si el
 * llamador necesita recuperar esa memoria —por ejemplo, tras vaciar un
 * contenedor con millones de elementos que no se van a volver a poblar— debe
 * invocar `std::vector::shrink_to_fit` explícitamente o reconstruir el
 * contenedor. La clase no expone ninguna operación equivalente a
 * `shrink_to_fit`; si el caso de uso lo requiere, esta es la ampliación a
 * considerar.
 *
 * @note El calificador `noexcept` en la definición **debe** coincidir con el
 *       de la declaración en la cabecera. Una divergencia entre ambos es un
 *       error de compilación, pero se menciona aquí como recordatorio de
 *       que ambos sitios deben mantenerse sincronizados.
 */
void UfHolder::limpiar_ufs() noexcept {
    ufs_.clear();
}

/**
 * @brief Reemplaza el contenido completo del contenedor con el de otro vector.
 *
 * @details
 * La operación delega en el operador de asignación de `std::vector`, que
 * reemplaza el contenido actual por el del argumento. La implementación
 * interna del operador puede reutilizar la capacidad existente si es
 * suficiente, o reasignar si no lo es; el efecto observable para el llamador
 * es el mismo en ambos casos.
 *
 * @par Semántica de reemplazo total, no de fusión
 * A diferencia de `agregar_uf` —que **añade** un elemento al final—, esta
 * función **reemplaza** todo el contenido. Si el contenedor tenía N
 * elementos y se le pasa un vector con M, el resultado tiene exactamente M
 * elementos: los N anteriores se destruyen y el nuevo contenido sustituye al
 * antiguo en su totalidad. Esta distinción es la que justifica la existencia
 * de dos métodos con propósitos aparentemente solapados.
 *
 * @par Por qué se toma por referencia constante y no por valor
 * La firma `establecer_ufs(const std::vector<Uf>&)` acepta tanto lvalues
 * como rvalues. Para un lvalue, copia el contenido al vector interno; para
 * un rvalue, también copia, desaprovechando la oportunidad de mover.
 *
 * La alternativa `establecer_ufs(std::vector<Uf> nuevas_ufs)` con paso por
 * valor y `ufs_ = std::move(nuevas_ufs)` sería óptima en ambos casos:
 * mueve el vector cuando el argumento es un rvalue (por ejemplo, el
 * resultado de una función que devuelve un vector), y copia una sola vez
 * cuando es un lvalue. La decisión de no aplicarla ahora responde al mismo
 * criterio que en `agregar_uf`: no se ha medido un caso donde importe.
 *
 * @par Uso interno desde from_json
 * La función `from_json` de este mismo archivo invoca `establecer_ufs` con
 * un vector temporal construido localmente. Ese vector es un rvalue, así
 * que la versión actual **copia** el contenido completo en lugar de moverlo.
 * Cambiar la firma al paso por valor + `std::move` eliminaría esa copia y
 * sería una mejora tangible en el rendimiento del parsing de JSON con
 * muchos elementos.
 *
 * @throws std::bad_alloc Si la asignación requiere una reasignación del
 *               vector y no hay memoria disponible. En ese caso, el estado
 *               del contenedor depende de la garantía de excepción del
 *               operador de asignación de `std::vector`, que en C++11 en
 *               adelante ofrece garantía fuerte cuando el tipo de elemento
 *               tiene constructor de copia `noexcept` o movimiento `noexcept`.
 *               Para `Uf`, el constructor de copia puede lanzar
 *               `std::bad_alloc` por la copia del `std::string`, así que la
 *               garantía depende de la implementación concreta de la
 *               biblioteca estándar; en la práctica, la mayoría ofrece
 *               garantía fuerte mediante el uso de almacenamiento temporal.
 *
 * @warning No es seguro invocar este método concurrentemente con otras
 *          operaciones de mutación o lectura sobre la misma instancia. Ver
 *          la sección de concurrencia en la documentación de la clase.
 */
void UfHolder::establecer_ufs(const std::vector<Uf> &nuevas_ufs) {
    ufs_ = nuevas_ufs;
}

/**
 * @brief Deserializa un `UfHolder` completo desde un objeto JSON.
 *
 * @details
 * La función implementa el patrón de construcción con **garantía de
 * excepción fuerte**: construye el nuevo vector en almacenamiento temporal,
 * lo puebla completamente, y solo si todo el proceso termina con éxito
 * reemplaza el contenido del `UfHolder` destino. Si cualquier paso intermedio
 * falla —un `Uf` con fecha mal formada, un `Valor` con formato chileno
 * inválido, un campo faltante— la excepción se propaga y el `UfHolder`
 * destino queda **exactamente como estaba** antes de la llamada.
 *
 * Este patrón se conoce como *copy-and-swap* simplificado y es el que
 * justifica la existencia de la variable local `ufs_temporales`. La
 * alternativa —poblar el contenedor destino directamente durante la
 * iteración— sería más simple pero rompería la garantía: un fallo en el
 * elemento k-ésimo dejaría el contenedor con k-1 elementos del nuevo JSON
 * mezclados con el contenido anterior.
 *
 * @par Validación estructural del JSON
 * Antes de iterar, la función comprueba dos cosas sobre el objeto JSON de
 * entrada:
 *
 *   1. Que contenga la clave `"UFs"`.
 *   2. Que el valor asociado sea un array.
 *
 * Estas comprobaciones se realizan con `contains` y `is_array` en lugar de
 * intentar acceder directamente al campo y dejar que nlohmann lance su
 * propia excepción. El motivo es la calidad del mensaje de error: la
 * excepción de nlohmann típicamente indica "key not found" o "type error"
 * sin contexto sobre qué estructura se esperaba; la comprobación explícita
 * permite un mensaje que nombra el campo y describe el problema en términos
 * del dominio.
 *
 * @par Reserva de memoria
 * La llamada a `reserve(ufs_array.size())` evita las reasignaciones
 * sucesivas que `push_back` haría si el vector creciera sin capacidad
 * suficiente. Para un array de N elementos, sin reserva se producirían del
 * orden de log(N) reasignaciones, cada una copiando los elementos
 * acumulados hasta el momento; con reserva, cero reasignaciones. La mejora
 * es tangible para lotes grandes (miles o decenas de miles de UF) y el
 * coste de la llamada es despreciable para lotes pequeños.
 *
 * @par Iteración explícita sin `auto`
 * El bucle usa `nlohmann::json::const_iterator` de forma explícita en lugar
 * de `auto`. Esta decisión es estilística y responde a una preferencia del
 * proyecto por hacer visibles los tipos en las iteraciones sobre
 * estructuras de bibliotecas externas, donde el tipo de retorno de
 * `begin()` puede no ser obvio para un lector no familiarizado con
 * nlohmann. La forma con `auto` sería más concisa y funcionalmente
 * idéntica; si se adopta como convención en el resto del proyecto, esta
 * línea y las análogas deben actualizarse.
 *
 * @par Uso de crear_uf_desde_json en lugar de get<Uf>()
 * Cada elemento del array se convierte con `crear_uf_desde_json` y no con
 * `it->get<Uf>()`. La razón está documentada en `uf.h`: nlohmann requiere
 * un constructor por defecto de `Uf` para la ruta `get<Uf>()`, y `Uf` no
 * lo tiene. La función libre `crear_uf_desde_json` sortea esa limitación
 * construyendo el objeto por el constructor parametrizado.
 *
 * @param j Objeto JSON con la estructura `{"UFs": [...]}`. Se pasa por
 *          referencia constante: la función no lo modifica.
 * @param holder Referencia al `UfHolder` destino. Se modifica **solo** si
 *               todo el proceso de parsing termina con éxito; en caso de
 *               excepción, el estado previo se preserva íntegro.
 *
 * @throws std::invalid_argument Si el JSON no contiene la clave `"UFs"` o
 *               si el valor asociado no es un array. También se propaga
 *               desde `crear_uf_desde_json` si algún elemento del array no
 *               cumple el contrato de `Uf`.
 * @throws nlohmann::json::exception Si algún elemento del array tiene una
 *               estructura incompatible con lo que `crear_uf_desde_json`
 *               espera, en los casos que esa función no captura
 *               explícitamente.
 * @throws std::bad_alloc Si la reserva del vector temporal o alguna de las
 *               conversiones internas agota la memoria disponible.
 *
 * @note La función se descubre por ADL cuando se invoca `j.get<UfHolder>()`
 *       o cualquiera de sus variantes. Al igual que con `Uf`, esta ruta
 *       requiere que `UfHolder` sea construible por defecto; si la clase
 *       carece de tal constructor, la ruta `get<UfHolder>()` no estará
 *       disponible y la función existirá únicamente por convención.
 *
 * @warning El `UfHolder` destino **no** se vacía antes de la operación. Si
 *          la llamada tiene éxito, su contenido anterior se reemplaza
 *          íntegramente por el nuevo; si falla, su contenido anterior
 *          permanece intacto. Esto es coherente con la semántica de
 *          `establecer_ufs` y con la garantía de excepción fuerte, pero
 *          puede sorprender a quien espere un comportamiento aditivo
 *          (agregar los elementos del JSON a los ya existentes).
 *
 * @see crear_uf_desde_json para el contrato de conversión de cada elemento.
 * @see UfHolder::establecer_ufs para la operación de reemplazo subyacente.
 */
void from_json(const nlohmann::json &j, UfHolder &holder) {
    // Validación estructural del JSON. Se comprueba explícitamente en lugar
    // de dejar que nlohmann lance su propia excepción, para producir un
    // mensaje que nombre el campo y describa el problema en términos del
    // dominio.
    if (!j.contains("UFs") || !j["UFs"].is_array()) {
        throw std::invalid_argument("JSON inválido: falta o no es un arreglo el campo 'UFs'");
    }

    const nlohmann::json &ufs_array = j.at("UFs");
    std::vector<Uf> ufs_temporales;

    // Reserva para evitar reasignaciones durante el push_back. Sin esta
    // línea, el vector podría reasignar del orden de log(N) veces para N
    // elementos, copiando en cada reasignación lo acumulado hasta entonces.
    ufs_temporales.reserve(ufs_array.size());

    // Iteración explícita sin usar 'auto': el tipo de nlohmann::json::begin()
    // no es evidente para un lector no familiarizado con la biblioteca, y
    // el proyecto prefiere tipos explícitos en iteraciones sobre estructuras
    // externas. Si se adopta 'auto' como convención, esta línea se actualiza.
    for (nlohmann::json::const_iterator it = ufs_array.begin(); it != ufs_array.end(); ++it) {
        // Se usa crear_uf_desde_json en lugar de get<Uf>() porque Uf no tiene
        // constructor por defecto (ver uf.h para el análisis).
        ufs_temporales.push_back(crear_uf_desde_json(*it));
    }

    // Inyección controlada al objeto destino, solo tras poblar el temporal
    // con éxito. Este es el paso que materializa la garantía de excepción
    // fuerte: si cualquier operación anterior lanzó, esta línea nunca se
    // alcanza y el holder conserva su contenido original.
    holder.establecer_ufs(ufs_temporales);
}
