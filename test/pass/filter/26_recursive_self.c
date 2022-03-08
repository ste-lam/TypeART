// clang-format off
// RUN: %c-to-llvm %s | %opt -mem2reg -S | %apply-typeart -typeart-stack -typeart-call-filter -S 2>&1 | %filecheck %s
// clang-format on

#include <stdlib.h>

extern void MPI_sink(void* a);
extern int rand_value;

void rec(int* x, int* y, int* z, int counter) {
  int q = 54654 * rand_value;

  if (counter == 0) {
    MPI_sink(x);
    return;
  }

  rec(y, z, &q, counter - 1);
}


// CHECK: > Stack Memory
// CHECK-NEXT: Alloca                 :  1.00
// CHECK-NEXT: Stack call filtered %  :  0.00