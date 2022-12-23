#include "common/error/error-codes.h"

#ifdef COMMON_ERROR_EXPORT_ASSERT
#undef COMMON_ERROR_EXPORT_ASSERT

#ifndef ASSERT_OK
#define ASSERT_OK(FALLIBLE) do { \
        common_error_code_t error_ = (FALLIBLE); \
        assert(error_ == COMMON_ERROR_CODE_OK); \
    } while (0)
#endif

#endif

#ifndef GOTO_ON_ERROR
#define GOTO_ON_ERROR(FALLIBLE, LABEL) do { \
        if ((FALLIBLE) != COMMON_ERROR_CODE_OK) { \
            goto LABEL; \
        } \
    } while(0)
#endif
