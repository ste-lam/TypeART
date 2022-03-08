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

#ifndef TYPEART_FILTER_OMPUTIL_H
#define TYPEART_FILTER_OMPUTIL_H

#include "compat/CallSite.h"
#include "support/DefUseChain.h"
#include "support/OmpUtil.h"

#include "llvm/ADT/Optional.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Casting.h"

namespace typeart::filter::omp {
struct EmptyContext {
  constexpr static bool WithOmp = false;
};

struct OmpContext {
  constexpr static bool WithOmp = true;

  static bool isOmpExecutor(const llvm::Function& called) {
    // TODO probably not complete (openmp task?, see isOmpTask*())
    return called.getName().startswith("__kmpc_fork_call");
  }

  static bool isOmpTaskAlloc(const llvm::Function& called) {
    return called.getName().startswith("__kmpc_omp_task_alloc");
  }

  static bool isOmpTaskCall(const llvm::Function& called) {
    return called.getName().endswith("__kmpc_omp_task");
  }

  static bool isOmpTaskRelated(const llvm::Function& called) {
    return called.getName().startswith("__kmpc_omp_task");
  }

  static bool isOmpHelper(const llvm::Function& called) {
    if (isOmpExecutor(called)) {
      return false;
    }
    const auto name = called.getName();
    // TODO extend this if required
    return name.startswith("__kmpc") || name.startswith("omp_");
  }

  static bool isOmpTaskAlloc(const llvm::CallSite& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpTaskAlloc(*c.getCalledFunction());
  }

  static bool isOmpTaskCall(const llvm::CallSite& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpTaskCall(*c.getCalledFunction());
  }

  static bool isOmpTaskRelated(const llvm::CallSite& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpTaskRelated(*c.getCalledFunction());
  }

  static bool isOmpHelper(const llvm::CallSite& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpHelper(*c.getCalledFunction());
  }

  static bool isOmpExecutor(const llvm::CallSite& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpExecutor(*c.getCalledFunction());
  }

  static bool isOmpExecutor(const llvm::CallBase& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpExecutor(*c.getCalledFunction());
  }

  static bool isOmpTaskAlloc(const llvm::CallBase& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpTaskAlloc(*c.getCalledFunction());
  }

  static bool isOmpHelper(const llvm::CallBase& c) {
    if (c.isIndirectCall()) {
      return false;
    }
    return isOmpHelper(*c.getCalledFunction());
  }

  static llvm::Optional<llvm::Function*> getMicrotask(const llvm::CallBase& Site, const llvm::Function &Callee) {
    assert(Site.getCalledOperand() == &Callee || Site.isIndirectCall());

    if (isOmpExecutor(Callee)) {
      auto *Fun = llvm::dyn_cast<llvm::Function>(Site.getArgOperand(2)->stripPointerCasts());
      return llvm::Optional<llvm::Function*>::create(&Fun);
    }
    if (isOmpTaskAlloc(Callee)) {
      auto *Fun = llvm::dyn_cast<llvm::Function>(Site.getArgOperand(5)->stripPointerCasts());
      return llvm::Optional<llvm::Function*>::create(&Fun);
    }
    return llvm::None;
  }

  static bool canDiscardMicrotaskArg(const llvm::CallBase &c, const llvm::Function &Callee, const Path& path) {
    using namespace llvm;
    auto arg = path.getEndPrev();
    if (!arg) {
      return false;
    }

    Value* in          = arg.getValue();
    auto *const arg_pos = llvm::find_if(c.args(), [&in](const Use& arg_use) -> bool { return arg_use.get() == in; });

    if (arg_pos == c.arg_end()) {
      return false;
    }

    auto arg_num = c.getArgOperandNo(arg_pos);

    if (isOmpExecutor(Callee)) {
      return arg_num <= 2;
    }

    if (isOmpTaskAlloc(Callee)) {
      return arg_num <= 5;  // task alloc inits the task, discard all 5
    }

    if (isOmpTaskCall(Callee)) {
      return arg_num <= 2;  // task call executes, in theory, discard only first 2
    }

    return false;
  }

  // TODO: implement v/register-call handling
  static bool allocaReachesTask(llvm::AllocaInst* alloc) {
    if (!util::omp::isOmpContext(alloc->getFunction())) {
      return false;
    }

    bool found{false};
    util::DefUseChain finder;
    finder.traverse_custom(
        alloc,
        [](auto val) -> llvm::Optional<decltype(val->users())> {
          if (auto cinst = llvm::dyn_cast<llvm::StoreInst>(val)) {
            return cinst->getValueOperand()->users();
          }
          return val->users();
        },
        [&found](auto value) {
          if (llvm::isa<llvm::CallInst>(value) || llvm::isa<llvm::InvokeInst>(value)) {
            auto *const site = llvm::cast<llvm::CallBase>(value);
            auto *const called = site->getCalledFunction();
            if (called != nullptr && called->getName().startswith("__kmpc_omp_task(")) {
              found = true;
              return util::DefUseChain::cancel;
            }
          }
          return util::DefUseChain::no_match;
        });
    return found;
  }

  static bool isTaskRelatedStore(llvm::Value* v) {
    if (auto* store = llvm::dyn_cast<llvm::StoreInst>(v)) {
      if (util::omp::isOmpContext(store)) {
        auto *operand = store->getPointerOperand();
        if (auto* gep = llvm::dyn_cast<llvm::GEPOperator>(operand)) {
          auto *type = gep->getSourceElementType();
          // Second condition filters out many struct.ident_t in lulesh omp:
          if (llvm::isa<llvm::StructType>(type) && !type->getStructName().contains("struct.ident_t")) {
            //            LOG_FATAL(*(gep->getSourceElementType()))
            return true;
          }
        }

        // else find task_alloc, and correlate with store (arg "v") to result of task_alloc
        llvm::Function* f = store->getFunction();
        auto calls = util::find_all(f, [&](auto& inst) {
          if (llvm::isa<llvm::CallInst>(inst) || llvm::isa<llvm::InvokeInst>(inst)) {
            return isOmpTaskAlloc(llvm::cast<llvm::CallBase>(inst));
          }
          return false;
        });

        bool found{false};
        util::DefUseChain chain;
        for (auto *i : calls) {
          chain.traverse(i, [&v, &found](auto val) {
            if (v == val) {
              found = true;
              return util::DefUseChain::cancel;
            }
            return util::DefUseChain::no_match;
          });
          if (found) {
            return true;
          }
        }
      }
    }

    return false;
  }

  template <typename Distance>
  static Distance getArgOffsetToMicrotask(const llvm::Function &Callee, Distance d) {
    if (d < 1) {
      LOG_WARNING("OMP offset should be > 2 for non-omp-internal args to outlined region")
      return d;
    }
    if (isOmpExecutor(Callee)) {
      return d - Distance{1};
    }
    LOG_WARNING("Unsupported OMP call.")
    return d;
  }
};

}  // namespace typeart::filter::omp

#endif  // TYPEART_OMPUTIL_H
