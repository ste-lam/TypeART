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

#include "TypeCheck.h"

#include <algorithm>
#include <cxxabi.h>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/format.h>
#include <memory>
#include <numeric>
#include <sstream>

namespace fs = std::filesystem;

namespace typeart {

void printMPIError(const MPICall& call, const char* fnname, int mpierr) {
  int len;
  std::string mpierrstr;
  mpierrstr.resize(MPI_MAX_ERROR_STRING);
  MPI_Error_string(mpierr, &mpierrstr[0], &len);
  mpierrstr.resize(strlen(mpierrstr.c_str()));
  PRINT_ERRORV(call, "%s failed: %s", fnname, mpierrstr.c_str());
}

CreateResult<Type> Type::create(const MPICall& call, int type_id) {
  const auto type_name = typeart_get_type_name(type_id);
  const auto type_size = typeart_get_type_size(type_id);
  return Type{type_id, type_name, type_size};
}

CreateResult<Buffer> Buffer::create(const MPICall& call, const void* buffer) {
  if (buffer == nullptr) {
    return {Buffer::create(call, 0, nullptr, 0, TYPEART_INVALID_ID)};
  }
  int type_id;
  size_t count          = 0;
  auto typeart_status_v = typeart_get_type(buffer, &type_id, &count);
  if (typeart_status_v != TYPEART_OK) {
    return cpp::fail(CreateError{TypeARTError{error_message_for(typeart_status_v)}});
  }
  return Buffer::create(call, 0, buffer, count, type_id);
}

CreateResult<Buffer> Buffer::create(const MPICall& call, ptrdiff_t offset, const void* ptr, size_t count, int type_id) {
  if (ptr == nullptr) {
    return Buffer{0, nullptr, 0, TYPEART_INVALID_ID, "", {}};
  }
  const auto type = Type::create(call, type_id);
  if (!type) {
    return cpp::fail(std::move(type).error());
  }
  typeart_struct_layout struct_layout;
  typeart_status status = typeart_resolve_type_id(type_id, &struct_layout);
  if (status == TYPEART_INVALID_ID) {
    auto message = fmt::format("Buffer::create received an invalid type_id {}", type_id);
    return cpp::fail(CreateError{InvalidArgument{message}});
  }
  if (status == TYPEART_OK) {
    std::vector<Buffer> type_layout = {};
    type_layout.reserve(struct_layout.num_members);
    for (size_t i = 0; i < struct_layout.num_members; ++i) {
      auto buffer =
          Buffer::create(call, static_cast<ptrdiff_t>(struct_layout.offsets[i]), (char*)ptr + struct_layout.offsets[i],
                         struct_layout.count[i], struct_layout.member_types[i]);
      if (!buffer) {
        return buffer;
      }
      type_layout.push_back(*buffer);
    }
    return Buffer{offset, ptr, count, *type, {type_layout}};
  }
  return Buffer{offset, ptr, count, *type, {}};
}

bool Buffer::hasStructType() const {
  return type_layout.has_value();
}

CreateResult<MPICombiner> MPICombiner::create(const MPICall& call, MPI_Datatype type) {
  MPICombiner result;
  int num_integers;
  int num_addresses;
  int num_datatypes;
  int combiner;
  auto mpierr = MPI_Type_get_envelope(type, &num_integers, &num_addresses, &num_datatypes, &combiner);
  if (mpierr != MPI_SUCCESS) {
    return cpp::fail(CreateError{MPIError{"MPI_Type_get_envelope", error_message_for(mpierr)}});
  }
  result.id = combiner;
  if (combiner != MPI_COMBINER_NAMED) {
    result.integer_args.resize(num_integers);
    result.address_args.resize(num_addresses);
    std::vector<MPI_Datatype> type_args(num_datatypes);
    mpierr = MPI_Type_get_contents(type, num_integers, num_addresses, num_datatypes, result.integer_args.data(),
                                   result.address_args.data(), type_args.data());
    if (mpierr != MPI_SUCCESS) {
      return cpp::fail(CreateError{MPIError{"MPI_Type_get_contents", error_message_for(mpierr)}});
    }
    result.type_args.reserve(num_datatypes);
    for (auto i = size_t{0}; i < num_datatypes; ++i) {
      auto type_arg = MPIType::create(call, type_args[i]);
      if (!type_arg) {
        return cpp::fail(std::move(type_arg).error());
      }
      result.type_args.push_back(std::move(type_arg).value());
    }
  }
  return {result};
}

CreateResult<MPIType> MPIType::create(const MPICall& call, MPI_Datatype type) {
  auto combiner = MPICombiner::create(call, type);
  if (!combiner) {
    return cpp::fail(std::move(combiner).error());
  }
  const auto type_id = type_id_for(type);
  auto result        = MPIType{type, type_id, "", *combiner};
  int len;
  result.name.resize(MPI_MAX_OBJECT_NAME);
  int mpierr = MPI_Type_get_name(type, &result.name[0], &len);
  result.name.resize(strlen(result.name.c_str()));
  if (mpierr != MPI_SUCCESS) {
    return cpp::fail(CreateError{MPIError{"MPI_Type_get_name", error_message_for(mpierr)}});
  }
  return {result};
}

CreateResult<Caller> Caller::create(const void* addr) {
  Caller result;
  result.addr   = addr;
  auto location = SourceLocation::create(addr);
  if (!location) {
    return cpp::fail(
        CreateError{SourceLocationError{fmt::format("Couldn't aquire source location for address {}", addr)}});
  }
  result.location = *location;
  return result;
}

std::atomic_size_t MPICall::next_trace_id = {0};

CreateResult<MPICall> MPICall::create(const char* function_name, const void* called_from, const void* buffer_ptr,
                                      int is_const, int count, MPI_Datatype type) {
  fmt::print(stderr, "======\n");
  const auto stacktrace = typeart::Stacktrace::current();
  for (auto entry : stacktrace) {
    fmt::print(stderr, "\tin {} ({}+{}) [{}] at {}:{}\n", entry.binary->file, *(entry.binary->function),
               (char*)entry.addr - (char*)entry.binary->function_addr, entry.addr, entry.source->file,
               entry.source->line);
    fmt::print(stderr, "\tin {}\n", entry);
  }
  fmt::print(stderr, "======\n");
  auto rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  auto result = MPICall{next_trace_id++, Caller{}, function_name, is_const, rank, {Buffer{}, count, MPIType{}}};
  auto caller = Caller::create(called_from);
  if (!caller) {
    return cpp::fail(std::move(caller).error());
  }
  auto buffer = Buffer::create(result, buffer_ptr);
  if (!buffer) {
    return cpp::fail(std::move(buffer).error());
  }
  auto mpi_type = MPIType::create(result, type);
  if (!mpi_type) {
    return cpp::fail(std::move(mpi_type).error());
  }
  result.caller      = *caller;
  result.args.buffer = *buffer;
  result.args.type   = *mpi_type;
  return {result};
}

TypeCheckResult<void> MPICall::check_buffer() const {
  if (args.count <= 0) {
    return cpp::fail(TypeCheckError{NullCount{}});
  }
  if (args.buffer.ptr == nullptr) {
    return cpp::fail(TypeCheckError{NullBuffer{}});
  }
  return check_type_and_count_against(args.buffer);
}

// For a given Buffer checks that the type of the buffer fits the MPI type
// `args.type` of this MPICall instance and that the buffer is large enough to
// hold `args.count` elements of the MPI type.
TypeCheckResult<void> MPICall::check_type_and_count_against(const Buffer& buffer) const {
  auto result = check_type(buffer, args.type);
  if (result.has_error()) {
    // If the type is a struct type and has a member with offset 0,
    // recursively check against the type of the first member.
    const auto type_layout = buffer.type_layout;
    if (!type_layout) {
      return cpp::fail(std::move(result).error());
    }
    const auto first_member = (*type_layout)[0];
    if (first_member.offset == 0) {
      if (result.has_value()) {
        return check_type_and_count_against(first_member);
      }
    }
    return cpp::fail(std::move(result).error());
  }
  auto multipliers  = std::move(result).value();
  auto type_count   = static_cast<size_t>(args.count * multipliers.type);
  auto buffer_count = buffer.count * multipliers.buffer;
  if (type_count > buffer_count) {
    return cpp::fail(TypeCheckError{InsufficientBufferSize{buffer_count, type_count}});
  }
  return {};
}

// For a given Buffer and MPIType, checks that the buffer's type matches the
// MPI type.
// The resulting integer `type_count_multiplier` is the number of elements of
// the buffer's type required to represent one element of the MPI type
// (e.g. an MPI_Type_contiguous with a `count` of 4 and an `oldtype` of
// MPI_DOUBLE would require 4 double elements for each element of that type.)
// Similarly, `buffer_count_multiplier` is the number of elements of the MPI
// type needed to represent one element of the buffer's type. This is used to
// correctly handle MPI_BYTE, where for each given type T, sizeof(T) elements
// of MPI_BYTE are needed to represent one instance of T.
TypeCheckResult<MPICall::Multipliers> MPICall::check_type(const Buffer& buffer, const MPIType& type) const {
  switch (type.combiner.id) {
    case MPI_COMBINER_NAMED:
      return check_combiner_named(buffer, type);
    case MPI_COMBINER_DUP:
      // MPI_Type_dup creates an exact duplicate of the type argument to the type
      // combiner, so we can delegate to a check against that type.
      return check_type(buffer, type.combiner.type_args[0]);
    case MPI_COMBINER_CONTIGUOUS:
      return check_combiner_contiguous(buffer, type);
    case MPI_COMBINER_VECTOR:
      return check_combiner_vector(buffer, type);
    case MPI_COMBINER_INDEXED_BLOCK:
      return check_combiner_indexed_block(buffer, type);
    case MPI_COMBINER_STRUCT:
      return check_combiner_struct(buffer, type);
    case MPI_COMBINER_SUBARRAY:
      return check_combiner_subarray(buffer, type);
    default:
      return cpp::fail(TypeCheckError{UnsupportedCombiner{combiner_name_for(type.combiner.id)}});
  }
}

// See MPICall::check_type(const Buffer&, const MPIType&)
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_named(const Buffer& buffer, const MPIType& type) const {
  // We assume MPI_BYTE to be the MPI equivalent of void*.
  if (type.mpi_type == MPI_BYTE) {
    return Multipliers{1, buffer.type.size};
  }
  // For named types (like e.g. MPI_DOUBLE) we compare the type id of the
  // buffer with the type id deduced for the MPI type using the type_id_for
  // function from Util.h.
  // As a special case, if the types do not match, but both represent a 128bit
  // floating point type, they are also considered to match.
  if (buffer.type.id != type.type_id && !(buffer.type.id == TYPEART_PPC_FP128 && type.type_id == TYPEART_FP128)) {
    return cpp::fail(TypeCheckError{BuiltinTypeMismatch{buffer.type.name, type.name}});
  }
  return Multipliers{1, 1};
}

// Type check for the type combiner:
// int MPI_Type_contiguous(int count, MPI_Datatype oldtype,
//     MPI_Datatype *newtype)
//
// See MPICall::check_type(const Buffer&, const MPIType&) for an explanation of
// the arguments and the return type.
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_contiguous(const Buffer& buffer,
                                                                         const MPIType& type) const {
  // MPI_Type_contiguous has one type argument and a count which denotes the
  // number of consecutive elements of the old type forming one element of the
  // conntiguous type. Therefore, we check that the old type matches the
  // buffer's type and multiply the count required for on element by the first
  // the first integer argument of the type combiner.
  auto count = type.combiner.integer_args[0];
  return check_type(buffer, type.combiner.type_args[0]).map([&](auto multipliers) {
    return Multipliers{multipliers.type * count, multipliers.buffer};
  });
}

// Type check for the type combiner:
// int MPI_Type_vector(int count, int blocklength, int stride,
//     MPI_Datatype oldtype, MPI_Datatype *newtype)
//
// See MPICall::check_type(const Buffer&, const MPIType&) for an explanation of
// the arguments and the return type.
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_vector(const Buffer& buffer, const MPIType& type) const {
  const auto count       = type.combiner.integer_args[0];
  const auto blocklength = type.combiner.integer_args[1];
  const auto stride      = type.combiner.integer_args[2];
  if (stride < 0) {
    return cpp::fail(
        TypeCheckError{UnsupportedCombinerArgs{"negative strides for MPI_Type_vector are currently not supported\n"}});
  }
  // MPI_Type_vector forms a number of `count` blocks of `oldtype` where the
  // start of each consecutive block is `stride` elements of `oldtype` apart
  // and each block consists of `blocklength` elements of oldtype.
  // We therefore check the buffer's type against `oldtype` and multiply the
  // resulting count by `(count - 1) * stride + blocklength`.
  return check_type(buffer, type.combiner.type_args[0]).map([&](auto multipliers) {
    return Multipliers{multipliers.type * ((count - 1) * stride + blocklength), multipliers.buffer};
  });
}

// Type check for the type combiner:
// int MPI_Type_create_indexed_block(int count, int blocklength, const int
//     array_of_displacements[], MPI_Datatype oldtype, MPI_Datatype *newtype)
//
// See MPICall::check_type(const Buffer&, const MPIType&) for an explanation of
// the arguments and the return type.
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_indexed_block(const Buffer& buffer,
                                                                            const MPIType& type) const {
  const auto count                  = type.combiner.integer_args[0];
  const auto blocklength            = type.combiner.integer_args[1];
  const auto array_of_displacements = type.combiner.integer_args.begin() + 2;
  const auto [min_displacement, max_displacement] =
      std::minmax_element(array_of_displacements, array_of_displacements + count);
  if (*min_displacement < 0) {
    return cpp::fail(TypeCheckError{UnsupportedCombinerArgs{
        "negative displacements for MPI_Type_create_indexed_block are currently not supported\n"}});
  }
  // Similer to MPI_Type_vector but with a separate displacement specified for
  // each block.
  // We therefore check the buffer's type against `oldtype` and multiply the
  // resulting count by `max(array_of_displacements) + blocklength`.
  return check_type(buffer, type.combiner.type_args[0])
      .map([&, max_displacement = *max_displacement](auto multipliers) {
        return Multipliers{multipliers.type * (max_displacement + blocklength), multipliers.buffer};
      });
}

// Type check for the type combiner:
// int MPI_Type_create_struct(int count, int array_of_blocklengths[],
//     const MPI_Aint array_of_displacements[], const MPI_Datatype array_of_types[],
//     MPI_Datatype *newtype)
//
// See MPICall::check_type(const Buffer&, const MPIType&) for an explanation of
// the arguments and the return type.
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_struct(const Buffer& buffer, const MPIType& type) const {
  const auto count                 = type.combiner.integer_args[0];
  const auto array_of_blocklenghts = type.combiner.integer_args.begin() + 1;
  // First, check that the buffer's type is a struct type...
  if (!buffer.hasStructType()) {
    return cpp::fail(TypeCheckError{BufferNotOfStructType{buffer.type.name}});
  }
  // ... and that the number of members of the struct matches the argument
  // `count` of the type combiner.
  const auto& type_layout = *(buffer.type_layout);
  if (type_layout.size() != count) {
    return cpp::fail(TypeCheckError{MemberCountMismatch{buffer.type.name, type_layout.size(), count}});
  }
  // Then, for each member check that...
  auto errors = std::vector<TypeCheckError>{};
  for (size_t i = 0; i < type_layout.size(); ++i) {
    // ... the byte offset of the member matches the respective element in
    // the `array_of_displacements` type combiner argument.
    if (type_layout[i].offset != type.combiner.address_args[i]) {
      errors.push_back(
          {MemberOffsetMismatch{buffer.type.name, i + 1, type_layout[i].offset, type.combiner.address_args[i]}});
    }
  }
  for (size_t i = 0; i < type_layout.size(); ++i) {
    // ... the type of the member matches the respective MPI type in the
    // `array_of_types` type combiner argument.
    auto result = check_type(type_layout[i], type.combiner.type_args[i]);
    if (result.has_error()) {
      errors.push_back({MemberTypeMismatch{i + 1, std::make_unique<TypeCheckError>(std::move(result).error())}});
      continue;
    }
    // ... the count of elements in the buffer of the member matches the count
    // required to represent `blocklength` elements of the MPI type.
    const auto multipliers  = std::move(result).value();
    const auto type_count   = static_cast<size_t>(array_of_blocklenghts[i]) * multipliers.type;
    const auto buffer_count = type_layout[i].count * multipliers.buffer;
    if (type_count != buffer_count) {
      errors.push_back({MemberElementCountMismatch{buffer.type.name, i + 1, type_count, buffer_count}});
    }
  }
  if (errors.size() != 0) {
    return cpp::fail(TypeCheckError{StructContentsMismatch{std::move(errors)}});
  }
  return Multipliers{1, 1};
}

// Type check for the type combiner:
// int MPI_Type_create_subarray(int ndims, const int array_of_sizes[], const
//     int array_of_subsizes[], const int array_of_starts[], int order, MPI_Datatype
//     oldtype, MPI_Datatype *newtype)
//
// See MPICall::check_type(const Buffer&, const MPIType&) for an explanation of
// the arguments and the return type.
TypeCheckResult<MPICall::Multipliers> MPICall::check_combiner_subarray(const Buffer& buffer,
                                                                       const MPIType& type) const {
  const auto ndims               = type.combiner.integer_args[0];
  const auto array_of_sizes      = type.combiner.integer_args.begin() + 1;
  const auto array_element_count = std::accumulate(array_of_sizes, array_of_sizes + ndims, 1, std::multiplies{});
  // As this type combiner specifies a subarray of a larger array, the buffer
  // must be large enough to hold that larger array. We therefore check the
  // buffer's type against `oldtype` and multiply the resulting count with
  // the product of all elements of the `array_of_sizes` (i.e. the element
  // count of the large n-dimensional array).
  return check_type(buffer, type.combiner.type_args[0]).map([&](auto multipliers) {
    return Multipliers{multipliers.type * array_element_count, multipliers.buffer};
  });
}

}  // namespace typeart

template <>
struct fmt::formatter<typeart::StacktraceEntry> {
  constexpr auto parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin()) {
    auto it = ctx.begin(), end = ctx.end();
    if (it != end && *(ctx.begin()) != '}') {
      throw fmt::format_error("invalid format");
    }
    return ctx.begin();
  }

  template <typename FormatCtx>
  auto format(const typeart::StacktraceEntry& entry, FormatCtx& ctx) -> decltype(ctx.out()) {
    auto binary_file = entry.binary.has_value() ? entry.binary.value().file : "??";
    auto function =
        entry.binary.has_value() && entry.binary->function.has_value()
            ? fmt::format("{}+{}", *(entry.binary->function), (char*)entry.addr - (char*)entry.binary->function_addr)
            : (entry.source.has_value() ? entry.source->function : "");
    auto location = entry.source.has_value() ? fmt::format("{}:{}", entry.source->file, entry.source->line) : "??:0";
    return fmt::format_to(ctx.out(), "{} ({}) [{}] at {}", binary_file, function, entry.addr, location);
  }
};
