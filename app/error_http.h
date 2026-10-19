// Autor: Sebastián Salazar Molina
// Académico, Universidad Tecnológica Metropolitana del Estado de Chile (UTEM)

//
// Created by seba on 11-10-26.
//

#ifndef APP_ERROR_HTTP_H
#define APP_ERROR_HTTP_H

#include <stdexcept>
#include <string>

/**
 * @brief Excepción específica para fallos en operaciones HTTP realizadas por ClienteHttp.
 *
 * @details
 * Esta clase hereda de std::runtime_error para integrarse en la jerarquía estándar de C++.
 * Se lanza en dos escenarios distintos, diferenciados por el valor del código HTTP:
 *
 * 1. Errores de Transporte (curl): Fallos de DNS, timeouts o rechazo de conexión.
 *    En este caso, el código HTTP será 0, ya que el servidor nunca respondió.
 * 2. Errores de Protocolo (HTTP): El servidor respondió, pero con un código fuera del
 *    rango de éxito (ej. 404 Not Found, 500 Internal Server Error).
 *
 * El uso del tipo 'long' para el código HTTP es deliberado y estricto, para mantener
 * la compatibilidad directa con la API de libcurl (CURLINFO_RESPONSE_CODE) sin
 * conversiones implícitas.
 */
class ErrorHttp : public std::runtime_error {
public:
    /**
     * @brief Construye una excepción HTTP con un mensaje descriptivo y, opcionalmente, un código de estado.
     *
     * @param mensaje Descripción detallada del error (ej. "Timeout de conexión" o "Respuesta inesperada").
     * @param codigo_http El código de estado HTTP devuelto por el servidor.
     *                    Debe ser 0 (valor por defecto) para errores de transporte donde no hubo respuesta.
     */
    explicit ErrorHttp(const std::string &mensaje, long codigo_http = 0)
        : std::runtime_error(mensaje), codigo_http_(codigo_http) {}

    /**
     * @brief Obtiene el código de estado HTTP asociado a este error.
     *
     * @return El código HTTP (ej. 404, 500). Retorna 0 si el error fue a nivel de transporte.
     * @note El atributo [[nodiscard]] obliga al llamador a considerar este código,
     *       ya que es crucial para la lógica de reintentos o manejo de errores.
     */
    [[nodiscard]] long obtenerCodigoHttp() const noexcept {
        return codigo_http_;
    }

private:
    long codigo_http_;
};

#endif // APP_ERROR_HTTP_H