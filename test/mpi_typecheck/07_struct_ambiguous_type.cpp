// REQUIRES: mpi
// UNSUPPORTED: asan
// UNSUPPORTED: tsan
// clang-format off
// RUN: %run %s --mpi_intercept --compile_flags "-g" --executable %s.exe --command "%mpi-exec -n 2 --output-filename %s.log %s.exe"
// RUN: cat "%s.log/1/rank.0/stderr" | FileCheck --check-prefixes CHECK,RANK0 %s
// RUN: cat "%s.log/1/rank.1/stderr" | FileCheck --check-prefixes CHECK,RANK1 %s
// clang-format on

#include "Util.hpp"

#include <mpi.h>
#include <stdlib.h>

constexpr auto n = 16;

struct A {
  double arr[16];
};

struct B {
  A a;
};

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  // CHECK: [Trace] TypeART Runtime Trace

  A a;
  B b;

  // clang-format off
  // RANK0: R[0][Info]ID[0] run_test(void*, int, {{.*}}[0x{{.*}}] at {{(/.*)*/.*\..*}}:{{[0-9]+}}: MPI_Send: checking send-buffer 0x{{.*}} of type "struct.A" against MPI type "MPI_DOUBLE"
  // RANK0-NOT: R[0][Info]ID[0] {{.*}}
  // RANK1: R[1][Info]ID[0] run_test(void*, int, {{.*}}[0x{{.*}}] at {{(/.*)*/.*\..*}}:{{[0-9]+}}: MPI_Recv: checking recv-buffer 0x{{.*}} of type "struct.A" against MPI type "MPI_DOUBLE"
  // RANK1-NOT: R[1][Info]ID[0] {{.*}}
  // clang-format on
  run_test(a.arr, n, MPI_DOUBLE);

  // clang-format off
  // RANK0: R[0][Info]ID[1] run_test(void*, int, {{.*}}[0x{{.*}}] at {{(/.*)*/.*\..*}}:{{[0-9]+}}: MPI_Send: checking send-buffer 0x{{.*}} of type "struct.B" against MPI type "MPI_DOUBLE"
  // RANK0-NOT: R[0][Info]ID[1] {{.*}}
  // RANK1: R[1][Info]ID[1] run_test(void*, int, {{.*}}[0x{{.*}}] at {{(/.*)*/.*\..*}}:{{[0-9]+}}: MPI_Recv: checking recv-buffer 0x{{.*}} of type "struct.B" against MPI type "MPI_DOUBLE"
  // RANK1-NOT: R[1][Info]ID[1] {{.*}}
  // clang-format on
  run_test(b.a.arr, n, MPI_DOUBLE);

  MPI_Finalize();
  return 0;
}
