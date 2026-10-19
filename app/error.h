// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

//
// Created by seba on 11-10-26.
//

#ifndef APP_ERROR_H
#define APP_ERROR_H

#include <stdexcept>

/**
 * @brief Excepción base personalizada para todos los errores de dominio de la aplicación.
 *
 * @details
 * Esta clase hereda de std::runtime_error para garantizar la compatibilidad con
 * la jerarquía de excepciones estándar de C++. Esto permite que los bloques `catch`
 * capturen tanto `Error` como `std::exception` de manera polimórfica y segura.
 *
 * Se utiliza como la excepción unificada para fallos recuperables o de lógica de negocio
 * (ej. configuración inválida, fallos de red controlados, errores de base de datos).
 *
 * NOTA SOBRE LA HERENCIA DE CONSTRUCTORES:
 * La declaración `using std::runtime_error::runtime_error;` es una característica de C++11
 * que hereda automáticamente todos los constructores de la clase base. Esto evita la
 * necesidad de escribir constructores redundantes (boilerplate) que simplemente reenvían
 * el mensaje de error a `std::runtime_error`, manteniendo el código DRY (Don't Repeat Yourself)
 * sin sacrificar la claridad del contrato de la clase.
 *
 * @par Ejemplo
 * @code
 *   throw Error("No se pudo conectar a la base de datos: " + mensaje_detalle);
 * @endcode
 */
class Error : public std::runtime_error {
public:
    /**
     * @brief Hereda los constructores de std::runtime_error.
     *
     * @param mensaje La cadena de caracteres que describe el error.
     *                Se convierte implícitamente a std::string según la firma de la clase base.
     */
    using std::runtime_error::runtime_error;
};

#endif // APP_ERROR_H
