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

#ifndef TYPEART_FILTERUTIL_H
#define TYPEART_FILTERUTIL_H

#include "IRPath.h"
#include "OmpUtil.h"
#include "compat/CallSite.h"
#include "support/DefUseChain.h"
#include "support/Logger.h"

#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>
#include <iostream>

namespace llvm {
class Value;
class raw_ostream;
}  // namespace llvm

using namespace llvm;

namespace typeart::filter {

struct FunctionAnalysis {
  using FunctionCounts = struct { int decl, def, intrinsic, indirect; };
  using FunctionCalls  = struct { llvm::SmallVector<CallSite, 8> decl, def, intrinsic, indirect; };

  FunctionCalls calls;

  void clear();

  bool empty() const;

  FunctionCounts analyze(Function* f);
};

raw_ostream& operator<<(raw_ostream& os, const FunctionAnalysis::FunctionCounts& counts);

enum class ArgCorrelation {
  NoMatch,
  Exact,
  ExactMismatch,
  Global,
  GlobalMismatch,
};

inline std::vector<llvm::Argument*> findArgs(const llvm::CallBase& Site, const llvm::Function &Callee, const Path& p) {
  assert(Site.getCalledOperand() == &Callee || Site.isIndirectCall());

  auto arg = p.getEndPrev();
  if (!arg) {
    return {};
  }

  const auto OmpMicrotask = omp::OmpContext::isOmpExecutor(Callee) ? omp::OmpContext::getMicrotask(Site, Callee) : None;

  Value* ArgValue = arg.getValue();

  std::vector<llvm::Argument*> Ret{};
  for (const auto &ArgUse: Site.args()) {
    if (ArgUse.get() != ArgValue) {
      continue;
    }

    const auto ArgNo = ArgUse.getOperandNo();

    if (OmpMicrotask) {
      // Calc the offset of arg ArgValue executor to actual arg of the outline function:
      auto offset = omp::OmpContext::getArgOffsetToMicrotask(Callee, ArgNo);

      Ret.push_back(OmpMicrotask.getValue()->getArg(offset));
      continue;
    }

    Ret.push_back(Callee.getArg(ArgNo));
  }
  
  return Ret;
}

inline std::vector<llvm::Argument*> args(const llvm::CallBase& Site, const llvm::Function &Callee, const Path& P) {
  assert(Site.getCalledOperand() == &Callee || Site.isIndirectCall());

  if (auto args = findArgs(Site, Callee, P); !args.empty()) {
    return args;
  }

  return {const_cast<llvm::Argument*>(Callee.arg_begin()), const_cast<llvm::Argument*>(Callee.arg_end())};
}


namespace detail {
template <typename TypeID>
ArgCorrelation correlate(const llvm::CallBase& Site, const llvm::Function &Callee, const Path& p, TypeID&& isType) {
  assert(Site.getCalledOperand() == &Callee || Site.isIndirectCall());

  if (auto args = findArgs(Site, Callee, p); !args.empty()) {
    for (auto *arg : args) {
      auto *type = arg->getType();
      if (isType(type)) {
        return ArgCorrelation::Exact;
      }
    }
    return ArgCorrelation::ExactMismatch;
  }

  const auto count_type_ptr = llvm::count_if(Site.args(), [&](const auto& csite_arg) {
    const auto type = csite_arg->getType();
    return isType(type);
  });

  if (count_type_ptr > 0) {
    return ArgCorrelation::Global;
  }
  return ArgCorrelation::GlobalMismatch;
}
}  // namespace detail

inline ArgCorrelation correlate2void(const llvm::CallBase& Site, const llvm::Function &Callee, const Path& p) {
  return detail::correlate(Site, Callee, p, [](llvm::Type* type) {
    return type->isPointerTy() && type->getPointerElementType()->isIntegerTy(8);
  });
}

inline ArgCorrelation correlate2pointer(const llvm::CallBase& Site, const llvm::Function &Callee, const Path& p) {
  // weaker predicate than void pointer, but more generally applicable
  return detail::correlate(Site, Callee, p, [](llvm::Type* type) { return type->isPointerTy(); });
}

inline bool isTempAlloc(llvm::Value* in) {
  const auto farg_stored_to = [](llvm::AllocaInst* inst) -> bool {
    bool match{false};
    Function* f = inst->getFunction();

    util::DefUseChain chain;
    chain.traverse(inst, [&f, &match](auto val) {
      if (auto* store = llvm::dyn_cast<StoreInst>(val)) {
        for (auto& args : f->args()) {
          if (&args == store->getValueOperand()) {
            match = true;
            return util::DefUseChain::cancel;
          }
        }
      }
      return util::DefUseChain::no_match;
    });

    return match;
  };
  if (auto *inst = llvm::dyn_cast<llvm::AllocaInst>(in)) {
    if (inst->getAllocatedType()->isPointerTy()) {
      return farg_stored_to(inst);
    }
  }
  return false;
}

}  // namespace typeart::filter

#endif  // TYPEART_FILTERUTIL_H
