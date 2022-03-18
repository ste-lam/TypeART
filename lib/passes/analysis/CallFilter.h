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

#include "../filter/Filter.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <memory>

namespace typeart::analysis {

enum class FilterImplementation { none, standard, cg, external };

struct FilterConfig {
  bool ClFilterNonArrayAlloca{false};
  bool ClFilterMallocAllocPair{false};
  bool ClFilterGlobal{true};
  bool ClUseCallFilter{false};
  bool ClFilterPointerAlloca{false};

  // std::string ClCallFilterImpl{"default"};
  FilterImplementation implementation{FilterImplementation::standard};
  std::string ClCallFilterPlugin{};
  std::string ClCallFilterGlob{"*MPI_*"};
  std::string ClCallFilterDeepGlob{"MPI_*"};
  std::string ClCallFilterCGFile{};
};

class FilterBuilder {
  using CallT = std::function<std::unique_ptr<typeart::filter::Filter>(const FilterConfig &)>;

  llvm::SmallVector<CallT, 2> builderCallbacks;
  const FilterConfig &config;

 public:
  explicit FilterBuilder(const FilterConfig &);

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

  /// workaround(?) for module reset, warmup, initialisation
  void reset(llvm::Module &);
};

}  // namespace typeart::analysis

#endif  // TYPEART_LIB_PASSES_ANALYSIS_CALLFILTER_H
