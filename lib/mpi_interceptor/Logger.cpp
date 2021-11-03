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

#include <cstdio>
#include <fmt/printf.h>

namespace typeart {

Logger::~Logger() {
}

struct StderrLoggerCreateErrorVisitor {
  int rank;
  const char* function_name;
  const void* called_from;

  template <class... Ts>
  void print_error(const std::string& fmt, Ts... fmt_args) {
    fmt::print(stderr, "R[{}][Error] internal error while typechecking a call to {} from {}: " + fmt + "\n", rank,
               function_name, called_from, std::forward<Ts>(fmt_args)...);
  }

  void operator()(const MPIError& err) {
    print_error("{} failed: {}", err.function_name, err.message);
  }
  void operator()(const TypeARTError& err) {
    print_error("internal runtime error ({})", err.message);
  }
  void operator()(const InvalidArgument& err) {
    print_error("{}", err.message);
  }
  void operator()(const SourceLocationError& err) {
    print_error("{}", err.message);
  }
};

struct StderrLoggerTypeCheckErrorVisitor {
  size_t trace_id;
  const MPICall& call;

  template <class... Ts>
  void print_error(const std::string& fmt, Ts... fmt_args) {
    fmt::print(stderr, "R[{}][Error]ID[{}] " + fmt + "\n", call.rank, trace_id, std::forward<Ts>(fmt_args)...);
  }

  void operator()(const NullCount&) {
  }
  void operator()(const NullBuffer&) {
    print_error("buffer {} is NULL", call.args.buffer.ptr);
  }
  void operator()(const UnsupportedCombiner& err) {
    print_error("the MPI type combiner {} is currently not supported", err.combiner_name);
  }
  void operator()(const InsufficientBufferSize& err) {
    print_error("buffer too small ({} elements, {} required)", err.actual, err.required);
  }
  void operator()(const BuiltinTypeMismatch& err) {
    print_error("expected a type matching MPI type \"{}\", but found type \"{}\"", err.mpi_type_name,
                err.buffer_type_name);
  }
  void operator()(const UnsupportedCombinerArgs& err) {
    print_error("{}", err.message);
  }
  void operator()(const BufferNotOfStructType& err) {
    print_error("expected a struct type, but found type \"{}\"", err.buffer_type_name);
  }
  void operator()(const MemberCountMismatch& err) {
    print_error("expected {} members, but the type \"{}\" has {} members", err.mpi_count, err.buffer_type_name,
                err.buffer_count);
  }
  void operator()(const StructContentsMismatch& err) {
    for (const auto& e : err.errors) {
      e.visit(*this);
    }
  }
  void operator()(const MemberOffsetMismatch& err) {
    print_error("expected a byte offset of {} for member {}, but the type \"{}\" has an offset of {}", err.mpi_offset,
                err.member, err.type_name, err.struct_offset);
  }
  void operator()(const MemberTypeMismatch& err) {
    (*err.error).visit(*this);
    print_error("the typechek for member {} failed", err.member);
  }
  void operator()(const MemberElementCountMismatch& err) {
    print_error("expected element count of {} for member {}, but the type \"{}\" has a count of {}", err.mpi_count,
                err.member, err.type_name, err.count);
  }
};

StderrLogger::~StderrLogger() {
}

void StderrLogger::log(const char* function_name, const void* called_from, const CreateError& err) {
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  err.visit(StderrLoggerCreateErrorVisitor{rank, function_name, called_from});
}

size_t StderrLogger::logTypeCheckHeader(const MPICall& call) {
  auto trace_id = next_trace_id++;
  fmt::print(stderr,
             "R[{}][Info]ID[{}] {}: checked {}-buffer {} of type \"{}\" against MPI type \"{}\"\n"
             "R[{}][Info]ID[{}] \tin {}[{}] at {}:{}\n",
             call.rank, trace_id, call.function_name, call.is_send ? "send" : "recv", call.args.buffer.ptr,
             call.args.buffer.type.name, call.args.type.name, call.rank, trace_id, call.caller.location.function,
             call.caller.addr, call.caller.location.file, call.caller.location.line);
  return trace_id;
}

void StderrLogger::log(size_t trace_id, const MPICall& call, const TypeCheckError& error) {
  error.visit(StderrLoggerTypeCheckErrorVisitor{trace_id, call});
}

std::atomic_size_t StderrLogger::next_trace_id = {0};

}  // namespace typeart
