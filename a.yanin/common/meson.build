project('common', 'c',
  default_options: ['warning_level=3', 'c_std=c2x', 'werror=true', 'default_library=static'])

modules = {}

# A primitive logging library.
subdir('log')

# The definition of common_error_code_t.
subdir('error-codes')

# Provides textual descrption of common_error_code_t error codes.
subdir('error-codes.display')

# Provides a macro for token concatenation.
subdir('util.concat-token')

# The base definitions for the collections.
subdir('collections')

# Atomic reference-counted pointers.
subdir('memory.arc')

# A double-linked list implementation.
subdir('collections.dlist')

# A typed hashmap with double hashing collision resolution.
subdir('collections.hash')

# A typed dynamic array.
subdir('collections.vec')

# A collection that provides constant-time index-based element lookup,
# with the indices preserved across element insertion/removals.
# In a way, an amalgamation of a double linked list and a vector.
subdir('collections.slab')

# A dynamically-sized byte array that permits NUL bytes.
subdir('collections.string')

# Utilities complementing the standard buffered I/O library.
subdir('io')

# POSIX function wrappers that capture the `errno` value and return it.
subdir('posix')

# A generic error library, supporing error chaining and backtraces.
subdir('error')

# Implements the `error` interface for `common_error_code_t`.
subdir('error-codes.adapter')

# Implements the `error` interface for `posix_err_t`.
subdir('posix.adapter')

# The base definitions for the executors.
subdir('executor')

# An asynchronous event loop.
subdir('loop')
