// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

//
// Created by seba on 12-10-26.
//

#ifndef APP_UTEM_H
#define APP_UTEM_H

#include <string>

/**
 * @namespace utem
 * @brief Espacio de nombres que agrupa utilidades transversales del proyecto.
 *
 * @details
 * Reúne funciones auxiliares sin estado, de propósito general y sin dependencias
 * del dominio de la aplicación (procesamiento de cadenas, normalización de
 * entradas, pequeñas operaciones sobre contenedores). El objetivo es triple:
 *
 *   1. **Evitar duplicación** de micro-algoritmos que de otro modo reaparecen
 *      copiados en múltiples módulos con ligeras variaciones.
 *   2. **Aislar dependencias** de bajo nivel en un único punto de inclusión,
 *      de modo que el resto del código no tenga que arrastrarlas.
 *   3. **Servir de banco de pruebas** de convenciones: lo que aquí se decide
 *      sobre `[[nodiscard]]`, paso por `const&`, criterios de *trim*, etc.,
 *      sirve de referencia para el resto del proyecto.
 *
 * @par Criterios de admisión
 * Para que una función pertenezca a este espacio de nombres debe cumplir:
 *   - Ser **sin estado**: ninguna dependencia de variables globales mutables.
 *   - Ser **reentrante**: segura para invocación concurrente desde múltiples
 *     hilos sin sincronización (en la práctica, esto se traduce en no tocar
 *     `static` locales ni memoria compartida).
 *   - Ser **agnóstica al dominio**: si la función conoce conceptos del negocio
 *     (endpoints, esquemas, nombres de tablas), su sitio es otro módulo.
 *
 * @par Organización cabecera / implementación
 * Este archivo declara las funciones y documenta su contrato; las definiciones
 * viven en `utem.cpp`. Esta separación es deliberada:
 *
 *   - **Reduce el acoplamiento**: los módulos que solo necesitan *invocar*
 *     estas utilidades no cargan el cuerpo de las funciones ni obligan al
 *     compilador a arrastrar `<algorithm>` y `<cctype>` a su unidad de
 *     traducción.
 *   - **Centraliza la implementación**: existe un único punto donde auditar
 *     el comportamiento real, evitando la clase de divergencia entre lo que
 *     documenta la cabecera y lo que hace el `.cpp` que ya se produjo en la
 *     primera iteración de este módulo.
 *   - **Evita violaciones de ODR**: la alternativa —definir las funciones
 *     como `inline` aquí *y* volver a definirlas en el `.cpp`— da lugar a
 *     múltiples definiciones en la misma unidad de traducción cuando el
 *     `.cpp` incluye su propia cabecera. No hay escenario donde ese patrón
 *     sea correcto.
 *
 * @note El coste de esta separación es que estas funciones **no** pueden
 *       integrarse en línea en el punto de llamada, a diferencia de lo que
 *       ocurría con la versión `inline` de la cabecera. Para las longitudes
 *       de cadena típicas en este proyecto, la diferencia es despreciable
 *       frente al coste de las operaciones de E/S que suelen rodearlas. Si en
 *       el futuro se midiera un impacto real, la vía correcta sería
 *       `constexpr`/`inline` en la cabecera **con la implementación
 *       eliminada del `.cpp`**, nunca mantener ambas.
 */
namespace utem {

    /**
     * @brief Elimina los caracteres de espacio en blanco de los extremos de una cadena (*trim*).
     *
     * @details
     * La función devuelve una **nueva** cadena con los espacios en blanco
     * eliminados del principio y del final. Los espacios internos se preservan
     * intactos: `"  hola  mundo  "` produce `"hola  mundo"`, conservando los
     * dos espacios internos.
     *
     * @par Conjunto exacto de caracteres considerados
     * La implementación **no** delega en `std::isspace` ni en la localidad del
     * programa. Utiliza un conjunto literal y explícito de **cuatro**
     * caracteres:
     *
     *   - espacio `' '` (`0x20`)
     *   - tabulación horizontal `'\t'` (`0x09`)
     *   - retorno de carro `'\r'` (`0x0D`)
     *   - salto de línea `'\n'` (`0x0A`)
     *
     * Quedan **fuera** del conjunto, aunque `std::isspace` los reconocería:
     * la tabulación vertical `'\v'` (`0x0B`) y el avance de página `'\f'`
     * (`0x0C`). La exclusión es deliberada y no un descuido:
     *
     *   - **Inmunidad a la localidad.** `std::isspace` depende de la locale
     *     activa del proceso, que cualquier componente ajeno puede modificar
     *     con `std::locale::global()`. Un conjunto literal no tiene ese
     *     acoplamiento oculto: el comportamiento de esta función es el mismo
     *     sin importar qué locale esté activa en tiempo de ejecución.
     *   - **Verificabilidad.** El conjunto de caracteres se lee directamente
     *     del cuerpo de la función; no requiere consultar la documentación de
     *     `<cctype>` ni conocer qué locale está instalada.
     *   - **Adecuación al uso previsto.** `\v` y `\f` no aparecen en archivos
     *     de configuración ni en texto convencional. Tratarlos como espacio
     *     en blanco sería una sorpresa, no una ayuda.
     *
     * @param texto Cadena de entrada a procesar. Se pasa por referencia
     *              constante: la función no la modifica, y cuando procede
     *              retornar contenido recortado materializa un objeto nuevo.
     *
     * @return std::string Nueva cadena sin espacios en blanco en los extremos.
     *         Si la entrada estaba compuesta únicamente por caracteres del
     *         conjunto o estaba vacía, retorna una cadena vacía.
     *
     * @par Complejidad
     * Lineal en la longitud de la cadena. O(n) en el peor caso (cadena
     * completamente blanca) y O(k) en el caso típico, donde solo hay k
     * caracteres blancos en los extremos.
     *
     * @note El atributo `[[nodiscard]]` es deliberado y no cosmético: previene
     *       el error lógico de asumir que la función modifica la cadena
     *       original *in-place*. Al tratarse de una función pura que retorna
     *       un valor nuevo, descartar el retorno implica no haber obtenido
     *       absolutamente nada.
     *
     * @warning Esta función **no** realiza normalización Unicode. Un espacio
     *          "duro" (`U+00A0`), un espacio fino (`U+2009`), un espacio de
     *          ancho cero (`U+200B`) o cualquier otro blanco fuera del
     *          repertorio ASCII **no** se eliminará. Si el texto proviene de
     *          fuentes que tienden a introducir este tipo de caracteres
     *          —páginas web, PDF, correos en HTML, exportaciones de suites
     *          ofimáticas— se requiere un paso previo de normalización con
     *          una biblioteca consciente de Unicode (ICU, `utf8proc`).
     *
     * @see eliminarComillas para la operación complementaria de despojado de
     *      comillas, con la que esta función se combina habitualmente durante
     *      el parseo de archivos de configuración.
     */
    [[nodiscard]] std::string eliminarEspacios(const std::string &texto);

    /**
     * @brief Elimina las comillas simples o dobles que envuelven **completamente** una cadena.
     *
     * @details
     * La función aplica una política de despojado de comillas deliberadamente
     * **estricta**. Solo actúa cuando se cumplen simultáneamente tres
     * condiciones:
     *
     *   1. La cadena tiene longitud ≥ 2. Una cadena de 0 o 1 caracteres no
     *      puede estar "envuelta" por comillas en ningún sentido útil.
     *   2. El primer y el último carácter son **el mismo** símbolo de cita:
     *      o bien ambos son `"`, o bien ambos son `'`. No basta con que
     *      ambos pertenezcan al repertorio de comillas.
     *   3. Ese símbolo es ASCII estándar (`"`, `0x22`; `'`, `0x27`).
     *
     * Si cualquiera de las condiciones falla, la cadena se retorna **sin
     * modificar**. No se retorna cadena vacía, no se lanza excepción, no se
     * produce ningún efecto observable distinto de la copia.
     *
     * @par Por qué esta política tan restrictiva
     * El caso de uso principal es el **parsing seguro de archivos de
     * configuración** (`.env` y dialectos equivalentes), donde un valor puede
     * venir entre comillas para preservar espacios internos. En ese contexto,
     * una política laxa produce bugs silenciosos difíciles de rastrear:
     *
     *   - `"texto'` —comilla inicial doble, final simple— es un valor con
     *     comillas legítimas en su interior, no un valor entrecomillado. Una
     *     política laxa eliminaría ambas y corrompería el dato.
     *   - Las comillas interiores del valor se conservan tal cual, sin reinterpretar escape alguno.
     *     Esta función no desescapa: se limita a retirar las comillas
     *     externas si las hay.
     *   - `no_quotes` —sin comillas— debe pasar inalterado.
     *
     * La función no intenta "adivinar" la intención del llamador ni corregir
     * entradas malformadas: o la cadena está envuelta por comillas pareadas
     * del mismo tipo, o no se toca.
     *
     * @par Ejemplos
     * @code
     *   eliminarComillas("\"hola\"");   // → hola
     *   eliminarComillas("'hola'");     // → hola
     *   eliminarComillas("\"hola'");    // → "hola'   (tipos distintos: sin cambios)
     *   eliminarComillas("\"");         // → "        (longitud 1: sin cambios)
     *   eliminarComillas("\"\"");       // → ""       (cadena vacía resultante)
     *   eliminarComillas("no_quotes");  // → no_quotes
     *   eliminarComillas("");           // → ""       (cadena vacía de entrada)
     * @endcode
     *
     * @param texto Cadena de entrada a procesar. Se pasa por referencia
     *              constante: la función no la modifica, y cuando procede
     *              despojar las comillas, el resultado se materializa en un
     *              objeto nuevo.
     *
     * @return std::string La cadena sin las comillas exteriores si estas
     *         cumplen las condiciones; la cadena original **sin modificar**
     *         en caso contrario. "Sin modificar" aquí significa devuelta tal
     *         cual, no copiada con algún cambio cosmético.
     *
     * @par Complejidad
     * O(1) cuando no procede despojar (solo se inspeccionan `front()` y
     * `back()`). O(n) cuando sí procede, por la copia del contenido
     * intermedio que realiza `std::string::substr`. La decisión de actuar o
     * no se toma siempre en tiempo constante, de modo que la función nunca
     * es más costosa que la longitud del resultado obtenido.
     *
     * @note El atributo `[[nodiscard]]` sigue la misma lógica que en
     *       eliminarEspacios: la función no modifica su argumento, retorna un
     *       valor nuevo. Descartar ese valor implica no haber hecho nada útil.
     *
     * @warning Esta función **no** reconoce comillas tipográficas ("curly")
     *          como `U+2018` `‘`, `U+2019` `’`, `U+201C` `“` o `U+201D` `”`.
     *          Si los datos provienen de fuentes que tienden a embellecer las
     *          comillas —procesadores de texto, ciertos CMS, copiado y pegado
     *          desde páginas web— será necesario normalizarlas a sus
     *          equivalentes ASCII antes de invocar esta función.
     *
     * @warning Tampoco interpreta secuencias de escape: las barras invertidas se tratan
     *          como caracteres normales. Un valor que empieza y termina por comilla
     *          doble pierde esas dos comillas exteriores y conserva cualquier barra
     *          invertida del interior. El desescapado, si se necesita, es
     *          responsabilidad del llamador y debe hacerse en una fase
     *          separada para no mezclar dos transformaciones con semánticas
     *          distintas.
     *
     * @see eliminarEspacios para la operación complementaria de *trim*, con
     *      la que esta función se combina habitualmente durante el parseo de
     *      archivos de configuración.
     */
    [[nodiscard]] std::string eliminarComillas(const std::string &texto);

} // namespace utem

#endif // APP_UTEM_H