// clang-format off
// RUN: %c-to-llvm  %s | %apply-typeart -typeart-stack -typeart-call-filter -mem2reg -S 2>&1 | %filecheck %s
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

// CHECK: MemInstFinderPass
// CHECK: > Stack Memory
// CHECK-NEXT: Alloca                 :  1.00
// CHECK-NEXT: Stack call filtered %  :  0.00