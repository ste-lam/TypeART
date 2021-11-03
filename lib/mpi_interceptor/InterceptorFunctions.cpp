// TypeART library
//
// Copyright (c) 2017-2021 TypeART Authors
// Distributed under the BSD 3-Clause license.
// (See accompanying file LICENSE.txt or copy at
// https://opensource.org/licenses/BSD-3-Clause)
//
// Project home: https://github.com/tudasc/TypeART
//
// SPDX-License-Identifier: BSD-3-Clause
//

#include "Logger.h"
#include "TypeCheck.h"
#include "runtime/RuntimeInterface.h"

#include <atomic>
#include <fmt/printf.h>
#include <mpi.h>
#include <sys/resource.h>

void typeart_check_buffer(const typeart::MPICall& call);

struct CallCounter {
  std::atomic_size_t send        = {0};
  std::atomic_size_t recv        = {0};
  std::atomic_size_t send_recv   = {0};
  std::atomic_size_t unsupported = {0};
};

static CallCounter counter;

struct MPICounter {
  std::atomic_size_t null_count = {0};
  std::atomic_size_t null_buff  = {0};
  std::atomic_size_t type_error = {0};
  std::atomic_size_t error      = {0};
};

static MPICounter mcounter;

static typeart::StderrLogger logger;

extern "C" {

void typeart_check_send(const char* name, const void* called_from, const void* sendbuf, int count, MPI_Datatype dtype) {
  ++counter.send;
  auto call = typeart::MPICall::create(name, called_from, sendbuf, 1, count, dtype);
  if (!call) {
    ++mcounter.error;
    return;
  }
  typeart_check_buffer(*call);
}

void typeart_check_recv(const char* name, const void* called_from, void* recvbuf, int count, MPI_Datatype dtype) {
  ++counter.recv;
  auto call = typeart::MPICall::create(name, called_from, recvbuf, 0, count, dtype);
  if (!call) {
    ++mcounter.error;
    return;
  }
  typeart_check_buffer(*call);
}

void typeart_check_send_and_recv(const char* name, const void* called_from, const void* sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype) {
  ++counter.send_recv;
  typeart_check_send(name, called_from, sendbuf, sendcount, sendtype);
  typeart_check_recv(name, called_from, recvbuf, recvcount, recvtype);
}

void typeart_unsupported_mpi_call(const char* name, const void* called_from) {
  ++counter.unsupported;
  fmt::print(stderr, "[Error] The MPI function {} is currently not checked by TypeArt\n", name);
}

void typeart_exit() {
  // Called at MPI_Finalize time
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  struct rusage end;
  getrusage(RUSAGE_SELF, &end);
  fmt::print(stderr, "R[{}][Info] CCounter {{ Send: {} Recv: {} Send_Recv: {} Unsupported: {} MAX RSS[KBytes]: {} }}\n",
             rank, counter.send.load(), counter.recv.load(), counter.send_recv.load(), counter.unsupported.load(),
             end.ru_maxrss);
  fmt::print(stderr, "R[{}][Info] MCounter {{ Error: {} Null_Buf: {} Null_Count: {} Type_Error: {} }}\n", rank,
             mcounter.error.load(), mcounter.null_buff.load(), mcounter.null_count.load(), mcounter.type_error.load());
}
}

void typeart_check_buffer(const typeart::MPICall& call) {
  auto check_result = call.check_buffer();
  auto trace_id     = logger.logTypeCheckHeader(call);
  if (check_result.has_error()) {
    auto err = std::move(check_result).error();
    if (err.is<typeart::NullCount>()) {
      ++mcounter.null_count;
    } else if (err.is<typeart::NullBuffer>()) {
      ++mcounter.null_buff;
    } else {
      ++mcounter.type_error;
    }
    logger.log(trace_id, call, err);
  }
}
