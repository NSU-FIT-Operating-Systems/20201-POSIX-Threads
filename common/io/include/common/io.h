#pragma once

#include <stdio.h>
#include <stdint.h>

#include "common/error/error-codes.h"
#include "common/collections/string.h"

common_error_code_t read_byte(FILE *file, uint8_t *result);
common_error_code_t read_expecting(FILE *file, unsigned char const *str);
common_error_code_t write_byte(FILE *file, uint8_t byte);
common_error_code_t write_cstr(FILE *file, unsigned char const *str);

common_error_code_t read_expecting_newline(FILE *file);
common_error_code_t read_expecting_space(FILE *file);

common_error_code_t skip_while(FILE *file, bool (*predicate)(uint8_t));

common_error_code_t read_while(FILE *file, bool (*predicate)(uint8_t), string_t *result);
common_error_code_t read_word(FILE *file, string_t *result);
common_error_code_t read_line(FILE *file, string_t *result);
common_error_code_t read_integer(FILE *file, int64_t *result);
common_error_code_t read_float(FILE *file, double *result);

common_error_code_t eat_till_lf(FILE *file);

common_error_code_t parse_integer(unsigned char const *buf, size_t size, int64_t *result);
common_error_code_t parse_float(unsigned char const *buf, size_t size, double *result);
