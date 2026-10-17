#ifndef APP_ERROR_H
#define APP_ERROR_H

#include <stdexcept>
#include <string>

class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

#endif // APP_ERROR_H