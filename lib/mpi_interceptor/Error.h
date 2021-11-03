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

#ifndef TYPEART_MPI_INTERCEPTOR_ERROR_H
#define TYPEART_MPI_INTERCEPTOR_ERROR_H

#include <memory>
#include <mpi.h>
#include <result.hpp>
#include <variant>
#include <vector>

namespace typeart {
template <class... Ts>
struct [[nodiscard]] VariantError {
  std::variant<Ts...> data;

  template <class... Param>
  VariantError(Param&&... param) : data(std::forward<Param>(param)...) {
  }

  template <class T>
  [[nodiscard]] bool is() const {
    return std::holds_alternative<T>(data);
  }

  template <class Visitor>
  auto visit(Visitor&& visitor) const -> decltype(auto) {
    return std::visit(std::forward<Visitor>(visitor), data);
  }
};

struct MPIError {
  std::string function_name;
  std::string message;
};
struct TypeARTError {
  std::string message;
};
struct InvalidArgument {
  std::string message;
};
struct SourceLocationError {
  std::string message;
};

struct [[nodiscard]] CreateError : public VariantError<MPIError, TypeARTError, InvalidArgument, SourceLocationError> {};
template <class T>
using CreateResult = cpp::result<T, CreateError>;

struct TypeCheckError;
struct NullCount {};
struct NullBuffer {};
struct UnsupportedCombiner {
  std::string combiner_name;
};
struct InsufficientBufferSize {
  size_t actual;
  size_t required;
};
struct BuiltinTypeMismatch {
  std::string buffer_type_name;
  std::string mpi_type_name;
};
struct UnsupportedCombinerArgs {
  std::string message;
};
struct BufferNotOfStructType {
  std::string buffer_type_name;
};
struct MemberCountMismatch {
  std::string buffer_type_name;
  size_t buffer_count;
  int mpi_count;
};
struct StructContentsMismatch {
  std::vector<TypeCheckError> errors;
};
struct MemberOffsetMismatch {
  std::string type_name;
  size_t member;
  ptrdiff_t struct_offset;
  MPI_Aint mpi_offset;
};
struct MemberTypeMismatch {
  size_t member;
  std::unique_ptr<TypeCheckError> error;
};
struct MemberElementCountMismatch {
  std::string type_name;
  size_t member;
  size_t count;
  size_t mpi_count;
};

struct [[nodiscard]] TypeCheckError
    : public VariantError<NullCount, NullBuffer, UnsupportedCombiner, InsufficientBufferSize, BuiltinTypeMismatch,
                          UnsupportedCombinerArgs, BufferNotOfStructType, MemberCountMismatch, StructContentsMismatch,
                          MemberOffsetMismatch, MemberTypeMismatch, MemberElementCountMismatch> {};

template <class T>
using TypeCheckResult = cpp::result<T, TypeCheckError>;

}  // namespace typeart

#endif  // TYPEART_MPI_INTERCEPTOR_ERROR_H
