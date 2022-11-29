#include "common/error/display.h"

#include <assert.h>
#include <stdlib.h>

char const *common_error_code_as_str(common_error_code_t error) {
    switch (error) {
    case COMMON_ERROR_CODE_OK:
        return "no error";

    case COMMON_ERROR_CODE_NOT_FOUND:
        return "an element was not found";

    case COMMON_ERROR_CODE_MEMORY_ALLOCATION_FAILURE:
        return "could not allocate memory";

    case COMMON_ERROR_CODE_OVERFLOW:
        return "a data storage has reached its maximum capacity";

    case COMMON_ERROR_CODE_FILE_ERROR:
        return "an error has occured while processing a file";

    case COMMON_ERROR_CODE_UNEXPECTED_EOF:
        return "a file ended abruptly";

    case COMMON_ERROR_CODE_UNEXPECTED_CHARACTER:
        return "encountered an unexpected character";

    case COMMON_ERROR_CODE_MALFORMED_NUMBER:
        return "a number was malformed";

    case COMMON_ERROR_CODE_NUMBER_TOO_LARGE:
        return "a number was too large";
    }

    // This is unreachable
    abort();
}
