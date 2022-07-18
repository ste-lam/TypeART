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

#include "MemInstFinder.h"
#include "CallFilter.h"
#include "MemOpVisitor.h"

#include "analysis/MemOpData.h"
#include "filter/CGInterface.h"
#include "filter/StdForwardFilter.h"
#include "support/Logger.h"
#include "support/Table.h"
#include "support/TypeUtil.h"
#include "support/Util.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "MemInstFinder"
ALWAYS_ENABLED_STATISTIC(NumDetectedHeap, "Number of detected heap allocs");
ALWAYS_ENABLED_STATISTIC(NumFilteredDetectedHeap, "Number of filtered heap allocs");

ALWAYS_ENABLED_STATISTIC(NumDetectedAllocs, "Number of detected allocs");
ALWAYS_ENABLED_STATISTIC(NumFilteredPointerAllocs, "Number of filtered pointer allocs");
ALWAYS_ENABLED_STATISTIC(NumCallFilteredAllocs, "Number of call filtered allocs");
ALWAYS_ENABLED_STATISTIC(NumFilteredMallocAllocs, "Number of  filtered  malloc-related allocs");
ALWAYS_ENABLED_STATISTIC(NumFilteredNonArrayAllocs, "Number of filtered non-array allocs");

ALWAYS_ENABLED_STATISTIC(NumDetectedGlobals, "Number of detected globals");
ALWAYS_ENABLED_STATISTIC(NumFilteredGlobals, "Number of filtered globals");
ALWAYS_ENABLED_STATISTIC(NumCallFilteredGlobals, "Number of filtered globals");

namespace typeart::analysis {


class MemInstFinderPass : public MemInstFinder {
 private:
  MemOpVisitor mOpsCollector;
  CallFilter filter;
  llvm::DenseMap<const llvm::Function*, FunctionData> functionMap;
  MemInstFinderConfig config;

 public:
  explicit MemInstFinderPass(const MemInstFinderConfig&);
  bool runOnModule(llvm::Module&) override;
  bool hasFunctionData(const llvm::Function&) const override;
  const FunctionData& getFunctionData(const llvm::Function&) const override;
  const GlobalDataList& getModuleGlobals() const override;
  void printStats(llvm::raw_ostream&) const override;
  // void configure(MemInstFinderConfig&) override;
  ~MemInstFinderPass() = default;

 private:
  bool runOnFunction(llvm::Function&);
};

MemInstFinderPass::MemInstFinderPass(const MemInstFinderConfig& config)
    : mOpsCollector(config.collect_alloca, config.collect_heap, config.collect_global), filter(FilterBuilder(config.filter)()), config(config) {
}

bool MemInstFinderPass::runOnModule(Module& module) {
  mOpsCollector.collectGlobals(module);
  auto& globals = mOpsCollector.globals;
  NumDetectedGlobals += globals.size();
  if (config.filter.ClFilterGlobal) {
    globals.erase(llvm::remove_if(
                      globals,
                      [&](const auto gdata) {  // NOLINT
                        GlobalVariable* global = gdata.global;
                        const auto name        = global->getName();

                        LOG_DEBUG("Analyzing global: " << name);

                        if (name.empty()) {
                          return true;
                        }

                        if (name.startswith("llvm.") || name.startswith("__llvm_gcov") ||
                            name.startswith("__llvm_gcda") || name.startswith("__profn")) {
                          // 2nd and 3rd check: Check if the global is private gcov data (--coverage).
                          LOG_DEBUG("LLVM startswith \"llvm\"")
                          return true;
                        }

                        if (name.startswith("___asan") || name.startswith("__msan") || name.startswith("__tsan")) {
                          LOG_DEBUG("LLVM startswith \"sanitizer\"")
                          return true;
                        }

                        if (global->hasInitializer()) {
                          auto* ini            = global->getInitializer();
                          std::string ini_name = util::dump(*ini);

                          if (llvm::StringRef(ini_name).contains("std::ios_base::Init")) {
                            LOG_DEBUG("std::ios");
                            return true;
                          }
                        }

                        if (global->hasSection()) {
                          // for instance, filters:
                          //   a) (Coverage) -fprofile-instr-generate -fcoverage-mapping
                          //   b) (PGO) -fprofile-instr-generate
                          StringRef Section = global->getSection();
                          // Globals from llvm.metadata aren't emitted, do not instrument them.
                          if (Section == "llvm.metadata") {
                            LOG_DEBUG("metadata");
                            return true;
                          }
                          // Do not instrument globals from special LLVM sections.
                          if (Section.find("__llvm") != StringRef::npos || Section.find("__LLVM") != StringRef::npos) {
                            LOG_DEBUG("llvm section");
                            return true;
                          }
                        }

                        if ((global->getLinkage() == GlobalValue::ExternalLinkage && global->isDeclaration())) {
                          LOG_DEBUG("Linkage: External");
                          return true;
                        }

                        Type* global_type = global->getValueType();
                        if (!global_type->isSized()) {
                          LOG_DEBUG("not sized");
                          return true;
                        }

                        if (global_type->isArrayTy()) {
                          global_type = global_type->getArrayElementType();
                        }
                        if (auto structType = dyn_cast<StructType>(global_type)) {
                          if (structType->isOpaque()) {
                            LOG_DEBUG("Encountered opaque struct " << global_type->getStructName() << " - skipping...");
                            return true;
                          }
                        }
                        return false;
                      }),
                  globals.end());

    const auto beforeCallFilter = globals.size();
    NumFilteredGlobals          = NumDetectedGlobals - beforeCallFilter;

    globals.erase(llvm::remove_if(globals, [&](const auto global) { return filter(global.global); }), globals.end());

    NumCallFilteredGlobals = beforeCallFilter - globals.size();
    NumFilteredGlobals += NumCallFilteredGlobals;
  }

  return llvm::count_if(module.functions(), [&](auto& function) { return runOnFunction(function); }) > 0;
}  // namespace typeart

bool MemInstFinderPass::runOnFunction(llvm::Function& function) {
  if (function.isDeclaration() || function.getName().startswith("__typeart")) {
    return false;
  }

  LOG_DEBUG("Running on function: " << function.getName())

  mOpsCollector.collect(function);

  const auto checkAmbigiousMalloc = [&function](const MallocData& mallocData) {
    using namespace typeart::util::type;
    auto primaryBitcast = mallocData.primary;
    if (primaryBitcast != nullptr) {
      const auto& bitcasts = mallocData.bitcasts;
      std::for_each(bitcasts.begin(), bitcasts.end(), [&](auto bitcastInst) {
        auto dest = bitcastInst->getDestTy();
        if (bitcastInst != primaryBitcast &&
            (!isVoidPtr(dest) && !isi64Ptr(dest) &&
             primaryBitcast->getDestTy() != dest)) {  // void* and i64* are used by LLVM
          // Second non-void* bitcast detected - semantics unclear
          LOG_WARNING("Encountered ambiguous pointer type in function: " << util::try_demangle(function));
          LOG_WARNING("  Allocation" << util::dump(*(mallocData.call)));
          LOG_WARNING("  Primary cast: " << util::dump(*primaryBitcast));
          LOG_WARNING("  Secondary cast: " << util::dump(*bitcastInst));
        }
      });
    }
  };

  NumDetectedAllocs += mOpsCollector.allocas.size();

  if (config.filter.ClFilterNonArrayAlloca) {
    auto& allocs = mOpsCollector.allocas;
    allocs.erase(llvm::remove_if(allocs,
                                 [&](const auto& data) {
                                   if (!data.alloca->getAllocatedType()->isArrayTy() && data.array_size == 1) {
                                     ++NumFilteredNonArrayAllocs;
                                     return true;
                                   }
                                   return false;
                                 }),
                 allocs.end());
  }

  if (config.filter.ClFilterMallocAllocPair) {
    auto& allocs  = mOpsCollector.allocas;
    auto& mallocs = mOpsCollector.mallocs;

    const auto filterMallocAllocPairing = [&mallocs](const auto alloc) {
      // Only look for the direct users of the alloc:
      // TODO is a deeper analysis required?
      for (auto inst : alloc->users()) {
        if (StoreInst* store = dyn_cast<StoreInst>(inst)) {
          const auto source = store->getValueOperand();
          if (isa<BitCastInst>(source)) {
            for (auto& mdata : mallocs) {
              // is it a bitcast we already collected? if yes, we can filter the alloc
              return std::any_of(mdata.bitcasts.begin(), mdata.bitcasts.end(),
                                 [&source](const auto bcast) { return bcast == source; });
            }
          } else if (isa<CallInst>(source)) {
            return std::any_of(mallocs.begin(), mallocs.end(),
                               [&source](const auto& mdata) { return mdata.call == source; });
          }
        }
      }
      return false;
    };

    allocs.erase(llvm::remove_if(allocs,
                                 [&](const auto& data) {
                                   if (filterMallocAllocPairing(data.alloca)) {
                                     ++NumFilteredMallocAllocs;
                                     return true;
                                   }
                                   return false;
                                 }),
                 allocs.end());
  }

  if (config.filter.ClFilterPointerAlloca) {
    auto& allocs = mOpsCollector.allocas;
    allocs.erase(llvm::remove_if(allocs,
                                 [&](const auto& data) {
                                   auto alloca = data.alloca;
                                   if (!data.is_vla && isa<llvm::PointerType>(alloca->getAllocatedType())) {
                                     ++NumFilteredPointerAllocs;
                                     return true;
                                   }
                                   return false;
                                 }),
                 allocs.end());
  }

  if (config.filter.ClUseCallFilter) {
    auto& allocs = mOpsCollector.allocas;
    allocs.erase(llvm::remove_if(allocs,
                                 [&](const auto& data) {
                                   if (filter(data.alloca)) {
                                     ++NumCallFilteredAllocs;
                                     return true;
                                   }
                                   return false;
                                 }),
                 allocs.end());
    //    LOG_DEBUG(allocs.size() << " allocas to instrument : " << util::dump(allocs));
  }

  auto& mallocs = mOpsCollector.mallocs;
  NumDetectedHeap += mallocs.size();

  for (const auto& mallocData : mallocs) {
    checkAmbigiousMalloc(mallocData);
  }

  FunctionData data{mOpsCollector.mallocs, mOpsCollector.frees, mOpsCollector.allocas};
  functionMap[&function] = data;

  mOpsCollector.clear();

  return true;
}  // namespace typeart

void MemInstFinderPass::printStats(llvm::raw_ostream& out) const {
  auto all_stack                       = double(NumDetectedAllocs);
  auto nonarray_stack                  = double(NumFilteredNonArrayAllocs);
  auto malloc_alloc_stack              = double(NumFilteredMallocAllocs);
  auto filter_pointer_stack            = double(NumFilteredPointerAllocs);
  auto call_filter_stack               = double(NumCallFilteredAllocs);
  auto call_filter_global              = double(NumCallFilteredGlobals);
  auto call_filter_global_nocallfilter = double(NumFilteredGlobals);
  auto call_filter_heap                = double(NumFilteredDetectedHeap);

  const auto call_filter_stack_p =
      (call_filter_stack /
       std::max<double>(1.0, all_stack - nonarray_stack - malloc_alloc_stack - filter_pointer_stack)) *
      100.0;

  const auto call_filter_heap_p =
      (call_filter_heap / std::max<double>(1.0, double(NumDetectedHeap))) * 100.0;

  const auto call_filter_global_p =
      (call_filter_global / std::max(1.0, double(NumDetectedGlobals))) * 100.0;

  const auto call_filter_global_nocallfilter_p =
      (call_filter_global_nocallfilter / std::max(1.0, double(NumDetectedGlobals))) * 100.0;

  Table stats("MemInstFinderPass");
  stats.wrap_header = true;
  stats.wrap_length = true;
  stats.put(Row::make("Filter string", config.filter.ClCallFilterGlob));
  stats.put(Row::make_row("> Heap Memory"));
  stats.put(Row::make("Heap alloc", NumDetectedHeap.getValue()));
  stats.put(Row::make("Heap call filtered", NumFilteredDetectedHeap.getValue()));
  stats.put(Row::make("Heap call filtered %", call_filter_heap_p));
  stats.put(Row::make_row("> Stack Memory"));
  stats.put(Row::make("Alloca", NumDetectedAllocs.getValue()));
  stats.put(Row::make("Alloca of pointer discarded", NumFilteredPointerAllocs.getValue()));
  stats.put(Row::make("Alloca of malloc-related discarded", NumFilteredMallocAllocs.getValue()));
  stats.put(Row::make("Alloca of non-array discarded", NumFilteredNonArrayAllocs.getValue()));
  stats.put(Row::make("Stack call filtered", NumCallFilteredAllocs.getValue()));
  stats.put(Row::make("Stack call filtered %", call_filter_stack_p));
  stats.put(Row::make_row("> Global Memory"));
  stats.put(Row::make("Global", NumDetectedGlobals.getValue()));
  stats.put(Row::make("Global discarded", NumFilteredGlobals.getValue() - NumCallFilteredGlobals.getValue()));
  stats.put(Row::make("Global call filtered", NumCallFilteredGlobals.getValue()));
  stats.put(Row::make("Global call filtered %", call_filter_global_p));
  stats.put(Row::make("Global filtered", NumFilteredGlobals.getValue()));
  stats.put(Row::make("Global filtered %", call_filter_global_nocallfilter_p));

  std::ostringstream stream;
  stats.print(stream);
  out << stream.str();
}

bool MemInstFinderPass::hasFunctionData(const Function& function) const {
  auto iter = functionMap.find(&function);
  return iter != functionMap.end();
}

const FunctionData& MemInstFinderPass::getFunctionData(const Function& function) const {
  auto iter = functionMap.find(&function);
  return iter->second;
}

const GlobalDataList& MemInstFinderPass::getModuleGlobals() const {
  return mOpsCollector.globals;
}

std::unique_ptr<MemInstFinder> create_finder(const MemInstFinderConfig& config) {
  return std::make_unique<MemInstFinderPass>(config);
}

}  // namespace typeart::analysis
