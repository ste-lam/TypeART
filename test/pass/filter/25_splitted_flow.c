// clang-format off
// RUN: %c-to-llvm  %s | %apply-typeart -typeart-stack -mem2reg -S 2>&1 | %filecheck %s
// clang-format on

#include <stdlib.h>

extern void MPI_sink(void* a);

void splitted_flow(int* x, int* y, int* z) {
    MPI_sink(z);
}


void foo() {
  int a = 1;

  splitted_flow(&a, &a, &a);
}

// CHECK: > Stack Memory
// CHECK-NEXT: Alloca                      :
// CHECK-NEXT: Stack call filtered %       :   0.00
