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

#include <atomic>

namespace typeart {

struct Logger {
  virtual ~Logger();

  virtual void log(const char* function_name, const void* called_from, const CreateError&) = 0;
  virtual size_t logTypeCheckHeader(const MPICall& call)                                   = 0;
  virtual void log(size_t trace_id, const MPICall&, const TypeCheckError&)                 = 0;
};

struct StderrLogger : public Logger {
  ~StderrLogger() override;

  void log(const char* function_name, const void* called_from, const CreateError&) override;
  size_t logTypeCheckHeader(const MPICall& call) override;
  void log(size_t trace_id, const MPICall& call, const TypeCheckError&) override;

 private:
  static std::atomic_size_t next_trace_id;
};

}  // namespace typeart
