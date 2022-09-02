#include "common/io.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "common/error/macros.h"

#define MAX_INT64_CHAR_LEN 20

static common_error_code_t convert_ferr(FILE *file, int status, int *output) {
    assert(file != NULL);

    if (status == EOF) {
        if (feof(file)) {
            return COMMON_ERROR_CODE_NOT_FOUND;
        }

        return COMMON_ERROR_CODE_FILE_ERROR;
    }

    if (output != NULL) {
        *output = status;
    }

    return COMMON_ERROR_CODE_OK;
}

static bool is_not_lf(uint8_t ch) {
    return ch != '\n';
}

common_error_code_t read_byte(FILE *file, uint8_t *result) {
    assert(file != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    int ch;
    GOTO_ON_ERROR(status = convert_ferr(file, fgetc(file), &ch), fail);

    if (result != NULL) {
        *result = (uint8_t) ch;
    }

fail:
    return status;
}

common_error_code_t read_expecting(FILE *file, unsigned char const *str) {
    assert(file != NULL);
    assert(str != NULL);

    unsigned char const *p = str;

    while (*p != '\0') {
        uint8_t byte = 0;
        common_error_code_t error = read_byte(file, &byte);

        if (error == COMMON_ERROR_CODE_NOT_FOUND) {
            return COMMON_ERROR_CODE_UNEXPECTED_EOF;
        } else if (error != COMMON_ERROR_CODE_OK) {
            return error;
        }

        if (byte != *p) {
            return COMMON_ERROR_CODE_UNEXPECTED_CHARACTER;
        }

        ++p;
    }

    return COMMON_ERROR_CODE_OK;
}

common_error_code_t write_byte(FILE *file, uint8_t byte) {
    assert(file != NULL);

    if (fputc(byte, file) == EOF) {
        return COMMON_ERROR_CODE_FILE_ERROR;
    }

    return COMMON_ERROR_CODE_OK;
}

common_error_code_t write_cstr(FILE *file, unsigned char const *str) {
    assert(file != NULL);
    assert(str != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    unsigned char const *p = str;

    while (*p != '\0') {
        GOTO_ON_ERROR(status = write_byte(file, *(p++)), fail);
    }

fail:
    return COMMON_ERROR_CODE_OK;
}

common_error_code_t read_expecting_newline(FILE *file) {
    assert(file != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    uint8_t ch = '\0';
    if ((status = read_byte(file, &ch)) != COMMON_ERROR_CODE_OK) {
        return status;
    }

    if (ch != '\n' && ch != '\r') {
        return COMMON_ERROR_CODE_UNEXPECTED_CHARACTER;
    }

    uint8_t second_ch = '\0';
    status = read_byte(file, &second_ch);

    if (status == COMMON_ERROR_CODE_NOT_FOUND) {
        return COMMON_ERROR_CODE_OK;
    } else if (status != COMMON_ERROR_CODE_OK) {
        return status;
    }

    if (second_ch != ch && (second_ch == '\n' || second_ch == '\r')) {
        return COMMON_ERROR_CODE_OK;
    }

    status = convert_ferr(file, ungetc(second_ch, file), NULL);

    return status;
}

common_error_code_t read_expecting_space(FILE *file) {
    assert(file != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    uint8_t ch;
    bool found = false;

    while (true) {
        common_error_code_t error = read_byte(file, &ch);

        if (error == COMMON_ERROR_CODE_NOT_FOUND && found) {
            break;
        } else if (error != COMMON_ERROR_CODE_OK) {
            return error;
        }

        if (ch != ' ' && ch != '\t') {
            if ((status = convert_ferr(file, ungetc(ch, file), NULL)) != COMMON_ERROR_CODE_OK) {
                return status;
            }

            break;
        }

        found = true;
    }

    if (!found) {
        return COMMON_ERROR_CODE_UNEXPECTED_CHARACTER;
    }

    return status;
}

common_error_code_t skip_while(FILE *file, bool (*predicate)(uint8_t)) {
    assert(file != NULL);
    assert(predicate != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    uint8_t ch = '\0';

    do {
        status = read_byte(file, &ch);

        if (status == COMMON_ERROR_CODE_NOT_FOUND) {
            return COMMON_ERROR_CODE_OK;
        } else if (status != COMMON_ERROR_CODE_OK) {
            return status;
        }
    } while (predicate(ch));

    status = convert_ferr(file, ungetc(ch, file), NULL);

    return status;
}

common_error_code_t read_while(FILE *file, bool (*predicate)(uint8_t), string_t *result) {
    assert(file != NULL);
    assert(predicate != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    uint8_t ch = '\0';

    while (true) {
        status = read_byte(file, &ch);

        if (status == COMMON_ERROR_CODE_NOT_FOUND) {
            return COMMON_ERROR_CODE_OK;
        }

        GOTO_ON_ERROR(status, fail);

        if (!predicate(ch)) {
            status = convert_ferr(file, ungetc(ch, file), NULL);

            break;
        }

        GOTO_ON_ERROR(status = string_push(result, (unsigned char) ch), fail);
    }

fail:
    return status;
}

common_error_code_t read_word(FILE *file, string_t *str) {
    assert(file != NULL);
    assert(str != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    int ch = '\0';
    bool found = false;

    while (true) {
        status = convert_ferr(file, fgetc(file), &ch);

        if (status == COMMON_ERROR_CODE_NOT_FOUND && found) {
            return COMMON_ERROR_CODE_OK;
        }

        GOTO_ON_ERROR(status, fail);

        if (isspace(ch)) {
            GOTO_ON_ERROR(status = convert_ferr(file, ungetc(ch, file), NULL), fail);

            break;
        }

        GOTO_ON_ERROR(status = string_push(str, (unsigned char) ch), fail);
        found = true;
    }

fail:
    return status;
}

common_error_code_t read_line(FILE *file, string_t *result) {
    assert(file != NULL);
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    string_clear(result);
    GOTO_ON_ERROR(status = read_while(file, is_not_lf, result), fail);

    uint8_t lf;
    status = read_byte(file, &lf);

    if (string_len(result) > 0 && status == COMMON_ERROR_CODE_NOT_FOUND) {
        return COMMON_ERROR_CODE_OK;
    }

    GOTO_ON_ERROR(status, fail);
    assert(lf == '\n');

fail:
    return status;
}

common_error_code_t read_integer(FILE *file, int64_t *result) {
    assert(file != NULL);
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    // 3 bytes for NUL and safety
    char digits[MAX_INT64_CHAR_LEN + 3] = {'\0'};

    size_t len = 0;
    bool negative = false;
    bool minus_allowed = true;
    bool leading_zeroes = true;
    bool met_digit = false;
    bool first_char = true;

    while (true) {
        unsigned char ch = '\0';
        common_error_code_t error = read_byte(file, &ch);

        if (error == COMMON_ERROR_CODE_NOT_FOUND) {
            if (first_char) {
                return COMMON_ERROR_CODE_UNEXPECTED_EOF;
            }

            break;
        } else if (error != COMMON_ERROR_CODE_OK) {
            return error;
        }

        if (ch == '0' && leading_zeroes) {
            met_digit = true;
            minus_allowed = false;
        } else if (ch == '-' && minus_allowed) {
            negative = true;
            minus_allowed = false;
        } else if (isdigit(ch)) {
            minus_allowed = false;
            leading_zeroes = false;
            met_digit = true;

            if (len < MAX_INT64_CHAR_LEN) {
                digits[len++] = ch;
            } else {
                return COMMON_ERROR_CODE_NUMBER_TOO_LARGE;
            }
        } else {
            if ((status = convert_ferr(file, ungetc(ch, file), NULL)) != COMMON_ERROR_CODE_OK) {
                return status;
            }

            break;
        }

        first_char = false;
    }

    if (len == 0 && met_digit) {
        digits[len++] = '0';
    }

    if (len == 0 && negative) {
        // we only found the minus sign — restore it before exiting
        if ((status = convert_ferr(file, ungetc('-', file), NULL)) != COMMON_ERROR_CODE_OK) {
            return status;
        }
    }

    if (len == 0) {
        return COMMON_ERROR_CODE_MALFORMED_NUMBER;
    }

    if (negative) {
        memmove(digits + 1, digits, len);
        digits[0] = '-';
        ++len;
    }

    return parse_integer((unsigned char *) digits, len, result);
}

common_error_code_t read_float(FILE *file, double *result) {
    assert(file != NULL);
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    string_t word;
    GOTO_ON_ERROR(status = string_new(&word), string_new_fail);
    GOTO_ON_ERROR(read_word(file, &word), read_word_fail);
    size_t len = string_len(&word);

    char const *word_ptr = (char const *) string_as_ptr(&word);
    GOTO_ON_ERROR(status = parse_float((unsigned char *) word_ptr, len, result), conversion_fail);

conversion_fail:
read_word_fail:
    string_free(&word);

string_new_fail:
    return status;
}

common_error_code_t eat_till_lf(FILE *file) {
    assert(file != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    GOTO_ON_ERROR(status = skip_while(file, is_not_lf), fail);
    status = read_expecting(file, (unsigned char const *) "\n");

    if (status == COMMON_ERROR_CODE_UNEXPECTED_EOF) {
        return COMMON_ERROR_CODE_OK;
    }

fail:
    return status;
}

common_error_code_t parse_integer(unsigned char const *buf, size_t size, int64_t *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[size] == '\0');

    if (size == 0) {
        return COMMON_ERROR_CODE_MALFORMED_NUMBER;
    }

    errno = 0;
    char *end = NULL;
    long long conversion_result = strtoll((char const *) buf, &end, 10);

    if (end != (char const *) buf + size) {
        if (errno == ERANGE) {
            return COMMON_ERROR_CODE_NUMBER_TOO_LARGE;
        }

        return COMMON_ERROR_CODE_MALFORMED_NUMBER;
    }

    *result = (int64_t) conversion_result;

    return COMMON_ERROR_CODE_OK;
}

common_error_code_t parse_float(unsigned char const *buf, size_t size, double *result) {
    assert(buf != NULL);
    assert(buf[size] == '\0');
    assert(result != NULL);

    if (size == 0) {
        return COMMON_ERROR_CODE_MALFORMED_NUMBER;
    }

    char *end;
    double conversion_result = strtod((char const *) buf, &end);

    if (end != (char const *) buf + size) {
        if (errno == ERANGE) {
            return COMMON_ERROR_CODE_NUMBER_TOO_LARGE;
        }

        return COMMON_ERROR_CODE_MALFORMED_NUMBER;
    }

    *result = conversion_result;

    return COMMON_ERROR_CODE_OK;
}
