// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

//
// Created by seba on 12-10-26.
//

#include "utem.h"


namespace utem {
    /**
     * @brief Implementación de utem::eliminarEspacios.
     *
     * @details
     * Estrategia basada en índices, no en iteradores. Se localizan los límites
     * del segmento útil con `find_first_not_of` y `find_last_not_of`, y se
     * extrae el subsegmento con `substr` en una única operación.
     *
     * @par Por qué índices y no iteradores
     * La versión equivalente con `std::find_if` sobre iteradores directos e
     * inversos obliga a reconstruir la posición derecha mediante
     * `reverse_iterator::base()`, que apunta al carácter **siguiente** al
     * último no blanco. Esa asimetría —extremo izquierdo inclusivo, extremo
     * derecho exclusivo tras la conversión— es una fuente clásica de errores
     * de *off-by-one* y de comentarios que repiten la misma explicación en
     * cada revisión. Trabajar con índices elimina esa carga cognitiva: ambos
     * extremos son inclusivos y la longitud es `fin - inicio + 1`, sin
     * conversiones ni semánticas semiabiertas ocultas.
     *
     * @par Por qué find_*_not_of y no std::isspace
     * `find_first_not_of(" \t\r\n")` fija el conjunto de caracteres en el
     * propio código, sin depender de la locale activa del proceso. El
     * contrato documentado en utem.h es exactamente el de esos cuatro
     * caracteres; si en el futuro se decidiera ampliar el conjunto, basta
     * con modificar la cadena literal en ambos `find_*_not_of` y actualizar
     * la cabecera en el mismo cambio.
     *
     * @par Control de flujo
     * El retorno temprano cuando `find_first_not_of` devuelve `npos` cubre
     * simultáneamente dos escenarios indistinguibles a efectos prácticos:
     * cadena vacía y cadena compuesta íntegramente por caracteres del
     * conjunto. En ambos casos, invocar `find_last_not_of` sería inútil —no
     * existe ningún carácter no blanco— y el resultado correcto es cadena
     * vacía.
     *
     * Cuando `inicio` es válido, `find_last_not_of` también lo será, porque
     * la propia existencia de un carácter no blanco garantiza que la búsqueda
     * inversa encontrará al menos ese mismo carácter. La relación
     * `fin >= inicio` se cumple por construcción, de modo que la longitud
     * `fin - inicio + 1` es siempre positiva y el `substr` resultante está
     * bien formado.
     *
     * @par Complejidad
     * Dos recorridos lineales, uno por cada `find_*_not_of`, más una copia
     * del segmento útil en `substr`. En el peor caso (cadena completamente
     * blanca) el segundo recorrido no llega a ejecutarse gracias al retorno
     * temprano, y en el caso típico solo se inspeccionan los k caracteres
     * blancos de los extremos. El coste dominante suele ser la copia del
     * resultado, no las búsquedas.
     *
     * @pre Ninguna. La función acepta cualquier std::string, incluida la
     *      cadena vacía.
     *
     * @post El valor retornado no comienza ni termina con ninguno de los
     *       cuatro caracteres del conjunto, o es la cadena vacía.
     *
     * @note La función no captura excepciones. La única fuente plausible de
     *       excepción es `std::bad_alloc` durante la construcción del
     *       `std::string` resultado, cuya propagación al llamador es el
     *       comportamiento correcto: no hay nada que esta función pueda
     *       hacer al respecto.
     */
    std::string eliminarEspacios(const std::string &texto) {
        // Conjunto literal de caracteres considerados espacio en blanco. La
        // fuente de verdad del contrato está en utem.h; mantener ambos
        // sincronizados si se amplía el conjunto.
        constexpr const char *BLANCOS = " \t\r\n";

        const std::size_t inicio = texto.find_first_not_of(BLANCOS);

        // Caso base: la cadena está vacía o compuesta íntegramente por
        // caracteres del conjunto. No hay nada que recortar.
        if (inicio == std::string::npos) {
            return {};
        }

        const std::size_t fin = texto.find_last_not_of(BLANCOS);

        // Ambos extremos son inclusivos: la longitud es fin - inicio + 1.
        // substr espera una longitud, no una posición final, de ahí el +1.
        return texto.substr(inicio, fin - inicio + 1);
    }

    /**
     * @brief Implementación de utem::eliminarComillas.
     *
     * @details
     * La función inspecciona únicamente los dos extremos de la cadena y, si
     * cumplen el contrato de comillas balanceadas —mismo símbolo, uno de los
     * dos ASCII admitidos—, devuelve el segmento interior mediante `substr`.
     * En cualquier otro caso, devuelve la cadena original sin tocarla.
     *
     * @par Por qué se comprueba primero la longitud
     * La comprobación `texto.size() >= 2` no es una optimización prematura,
     * es una precondición semántica. Sin ella, una cadena de longitud 1 con
     * una comilla —`"` o `'`— haría que `front()` y `back()` devolvieran el
     * mismo carácter, la comparación `primer_caracter == '"' && ultimo_caracter == '"'`
     * se cumpliría, y `substr(1, size() - 2)` se invocaría con `size() - 2`
     * bajo cero. Al ser `size()` un tipo sin signo, ese valor se interpretaría
     * como un `std::size_t` enorme; `substr` acotaría la longitud y devolvería una cadena vacía en lugar de dejar la entrada intacta.
     * La comprobación de longitud elimina esa ruta por completo antes de que
     * pueda activarse.
     *
     * @par Por qué se comparan los dos pares por separado
     * La condición `(a == '"' && b == '"') || (a == '\'' && b == '\'')` es
     * deliberadamente más verbosa que la alternativa `a == b && (a == '"' || a == '\'')`.
     * Ambas son equivalentes en resultado, pero la primera hace explícito en
     * el código el contrato documentado —"mismo símbolo, y ese símbolo es
     * uno de los dos admitidos"— sin requerir que el lector reconstruya la
     * equivalencia. Dado que esta función procesa entradas no confiables en
     * el contexto del parseo de configuración, prima la legibilidad sobre
     * cualquier micro-optimización de comparaciones que el compilador
     * aplicará igualmente.
     *
     * @par Por qué se retorna la cadena original en lugar de una copia vacía
     * El contrato de la función es "despojar si procede, no tocar si no
     * procede". Retornar una cadena vacía en el caso negativo rompería ese
     * contrato y obligaría al llamador a distinguir entre "no había comillas
     * que despojar" y "el valor era legítimamente vacío". Mantener el paso
     * inalterado preserva la composición con `eliminarEspacios`: cualquier
     * orden de aplicación de ambas funciones sobre la misma cadena produce
     * el mismo resultado, salvo por los espacios que el *trim* retire.
     *
     * @par Complejidad
     * Cuando no procede despojar, el coste es O(1): dos accesos a los
     * extremos y una comparación de longitud. Cuando sí procede, se añade la
     * copia del segmento interior en `substr`, que es O(n) en la longitud
     * del resultado. La decisión de actuar nunca es más costosa que la
     * longitud del resultado, lo cual es la cota natural de esta operación.
     *
     * @pre Ninguna. La función acepta cualquier std::string, incluida la
     *      cadena vacía y cadenas con bytes no ASCII en el interior.
     *
     * @post Si el valor retornado es distinto del valor de entrada, entonces
     *       el valor de entrada comenzaba y terminaba con la misma comilla
     *       ASCII y el retorno es exactamente el segmento interior.
     *
     * @note Al igual que `eliminarEspacios`, esta función no captura
     *       excepciones. `std::bad_alloc` durante la construcción del
     *       resultado se propaga, lo cual es correcto: no hay recuperación
     *       local posible.
     */
    std::string eliminarComillas(const std::string &texto) {
        // Una cadena de longitud 0 o 1 no puede estar envuelta por una pareja
        // de comillas. Esta comprobación también protege contra el cálculo
        // size() - 2 bajo cero en el caso de longitud 1 (ver documentación).
        if (texto.size() >= 2) {
            const char primer_caracter = texto.front();
            const char ultimo_caracter = texto.back();

            // Comprobación estricta: misma comilla en ambos extremos, y esa
            // comilla pertenece al repertorio ASCII admitido. La combinación
            // cruzada —doble con simple, o viceversa— se descarta
            // explícitamente porque en el caso de uso real (parsing de .env)
            // señala un valor con comillas legítimas en su interior.
            if ((primer_caracter == '"' && ultimo_caracter == '"') ||
                (primer_caracter == '\'' && ultimo_caracter == '\'')) {
                // Despoja las dos comillas: se extrae desde la posición 1 una
                // longitud de size() - 2, dejando fuera el primer y el último
                // carácter. El caso límite "\"\"" (dos comillas) produce una
                // cadena vacía, que es el resultado correcto.
                return texto.substr(1, texto.size() - 2);
            }
        }

        // Retorno de la cadena original cuando no se cumple el contrato de
        // comillas balanceadas. Este camino cubre el caso de longitud
        // insuficiente, la ausencia de comillas en los extremos y la mezcla
        // de comillas de tipos distintos.
        return texto;
    }
} // namespace utem
