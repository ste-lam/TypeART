#ifndef LLVM_MUST_SUPPORT_RUNTIMEINTERFACE_H
#define LLVM_MUST_SUPPORT_RUNTIMEINTERFACE_H

#include "../typelib/TypeInterface.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lookup_result_t { SUCCESS, UNKNOWN_ADDRESS, BAD_ALIGNMENT, WRONG_KIND } lookup_result;

lookup_result typeart_get_builtin_type(const void* addr, typeart_builtin_type* type);
lookup_result typeart_get_type(const void* addr, typeart_type_info* type, size_t* count);
lookup_result typeart_resolve_type(int id, size_t* len, const typeart_type_info** types, const size_t** count,
                                   const size_t** offsets, size_t* extent);
// lookup_result typeart_support_resolve_type_alloc_buffer(int id, int* len, typeart_type_info* types[], int* count[],
// int* offsets[],
//                                                     int* extent);

const char* typeart_get_type_name(int id);

#ifdef __cplusplus
}
#endif

#endif  // LLVM_MUST_SUPPORT_RUNTIMEINTERFACE_H
