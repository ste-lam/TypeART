// TypeART library
//
// Copyright (c) 2017-2022 TypeART Authors
// Distributed under the BSD 3-Clause license.
// (See accompanying file LICENSE.txt or copy at
// https://opensource.org/licenses/BSD-3-Clause)
//
// Project home: https://github.com/tudasc/TypeART
//
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef TYPEART_LIB_PASSES_ANALYSIS_CALLFILTER_H
#define TYPEART_LIB_PASSES_ANALYSIS_CALLFILTER_H

#include "Filter.h"
#include "FilterPlugin.h"
#include "MemInstFinder.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <memory>

namespace typeart::analysis {
class FilterBuilder {
  using CallT = std::function<std::unique_ptr<typeart::filter::Filter>(const MemInstFinderConfig::Filter &)>;

  llvm::SmallVector<CallT, 2> builderCallbacks;
  const MemInstFinderConfig::Filter &config;

 public:
  explicit FilterBuilder(const MemInstFinderConfig::Filter &);

  void registerBuilderCallback(const CallT &);

  std::unique_ptr<typeart::filter::Filter> operator()();
};

class CallFilter {
  std::unique_ptr<typeart::filter::Filter> fImpl;

 public:
  explicit CallFilter(std::unique_ptr<typeart::filter::Filter> filter);
  CallFilter(const CallFilter&) = delete;
  CallFilter(CallFilter&&)      = default;
  CallFilter& operator=(const CallFilter&) = delete;
  CallFilter& operator=(CallFilter&&) noexcept = default;
  virtual ~CallFilter() = default;

  bool operator()(llvm::AllocaInst*);
  bool operator()(llvm::GlobalValue*);
};

}  // namespace typeart::analysis

#endif  // TYPEART_LIB_PASSES_ANALYSIS_CALLFILTER_H
