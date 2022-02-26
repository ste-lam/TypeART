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

#ifndef TYPEART_FILTERBASE_H
#define TYPEART_FILTERBASE_H

#include "Filter.h"
#include "FilterUtil.h"
#include "IRPath.h"
#include "IRSearch.h"
#include "OmpUtil.h"
#include "compat/CallSite.h"
#include "support/Logger.h"
#include "support/OmpUtil.h"
#include "support/Util.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

#include <iterator>
#include <type_traits>

namespace typeart::filter {

enum class FilterAnalysis {
  /// Do not follow users of current decl/def etc.
  Skip = 0,
  /// Continue searching users of decl/def etc.
  Continue,
  /// Keep the value (return false)
  Keep,
  /// Filter the value (return true)
  Filter,
  /// Want analysis of the called function def
  FollowDef,
};

template <typename CallSiteHandler, typename Search, typename OmpHelper = omp::EmptyContext>
class BaseFilter : public Filter {
  CallSiteHandler handler;
  Search search_dir{};
  bool malloc_mode{false};
  llvm::Function* start_f{nullptr};

 public:
  explicit BaseFilter(const CallSiteHandler& handler) : handler(handler) {
  }

  template <typename... Args>
  explicit BaseFilter(Args&&... args) : handler(std::forward<Args>(args)...) {
  }

  bool filter(llvm::Value* in) override {
    if (in == nullptr) {
      LOG_WARNING("Called with nullptr");
      return false;
    }

    FPath fpath(start_f);
    const auto filter = DFSFuncFilter(in, fpath);
    if (!filter) {
      LOG_DEBUG(fpath);
    }
    return filter;
  }

  void setStartingFunction(llvm::Function* StartingFunction) override {
    start_f = StartingFunction;
  };

  void setMode(bool MallocMode) override {
    malloc_mode = MallocMode;
  };

 private:

  llvm::iterator_range<llvm::Function*> callees(const llvm::CallBase &Inst) {
    if (Inst.isIndirectCall()) {
      if constexpr (CallSiteHandler::Support::Callees) {
        return handler.callees(Inst);
      }

      return std::initializer_list<llvm::Function*>{};
    }

    return {Inst.getCalledFunction()};
  }

  bool DFSFuncFilter(llvm::Value* current, FPath& fpath) {
    /* do a pre-flow tracking check of value in  */
    if constexpr (CallSiteHandler::Support::PreCheck) {
      // is null in case of global:
      llvm::Function* currentF = fpath.getCurrentFunc();
      if (currentF != nullptr) {
        auto status = handler.precheck(current, currentF, fpath);
        switch (status) {
          case FilterAnalysis::Filter:
            fpath.pop();
            LOG_DEBUG("Pre-check, filter")
            return true;

          case FilterAnalysis::Keep:
            LOG_DEBUG("Pre-check, keep")
            return false;

          case FilterAnalysis::Skip:
          case FilterAnalysis::Continue:
          case FilterAnalysis::FollowDef:
            break;

          default:
            llvm_unreachable("unknown/undefined enum value");
            break;
        }
      }
    }

    PathList defPath;  // paths that reach a definition in currentF
    Path p;

    if (const auto filter = DFSfilter(current, p, defPath); !filter) {
      // for diagnostic output, store the last path
      fpath.pushFinal(p);
      return false;
    }

    for (auto &path2def : defPath) {
      if (followPath(fpath, path2def) == VR_Stop) {
        return false;
      }
    }

    fpath.pop();
    return true;
  }

  enum VisitResult {
    VR_Continue = 0,
    VR_Stop,
  };

  VisitResult followPath(FPath& fpath, IRPath &path2def) {
    auto csite = path2def.getEnd();
    if (!csite) {
      return VR_Continue;
    }

    llvm::CallSite c(csite.getValue());
    if (fpath.contains(c)) {
      // Avoid recursion:
      // TODO a continue may be wrong, if the function itself eventually calls "MPI"?
      return VR_Continue;
    }

    // TODO: here we have a definition OR a omp call, e.g., @__kmpc_fork_call
    LOG_DEBUG("Looking at: " << c.getCalledFunction()->getName());

    if constexpr (OmpHelper::WithOmp) {
      if (OmpHelper::isOmpExecutor(c)) {
        if (OmpHelper::canDiscardMicrotaskArg(c, path2def)) {
          LOG_DEBUG("Passed as internal OMP API arg, skipping " << path2def);
          return VR_Continue;
        }
      }
    }

    auto argv = args(c, path2def);
    if (argv.size() > 1) {
      LOG_DEBUG("All args are looked at.")
    } else if (argv.size() == 1) {
      LOG_DEBUG("Following 1 arg.");
    } else {
      LOG_DEBUG("No argument correlation.")
    }

    // idea: add an arg expand!

    if constexpr (OmpHelper::WithOmp) {
      if (OmpHelper::isOmpExecutor(c)) {
        auto outlined = OmpHelper::getMicrotask(c);
        if (outlined) {
          path2def.push(outlined.getValue());
        }
      }
    }

    fpath.push(path2def);

    for (auto* arg : argv) {
      if (const auto dfs_filter = DFSFuncFilter(arg, fpath); !dfs_filter) {
        return VR_Stop;
      }
    }

    return VR_Continue;
  }

  bool DFSfilter(llvm::Value* current, Path& path, PathList& plist) {
    if (current == nullptr) {
      LOG_FATAL("Called with nullptr: " << path);
      return false;
    }

    path.push(current);

    if (const auto *CallBaseInst = llvm::dyn_cast<llvm::CallBase>(current)) {
      // In-order analysis
      const auto status = callsite(*CallBaseInst, path);
      switch (status) {
        case FilterAnalysis::Skip:
          path.pop();
          return true;

      case FilterAnalysis::Keep:
        LOG_DEBUG("Callsite check, keep")
        return false;

      case FilterAnalysis::FollowDef:
        LOG_DEBUG("Analyze definition in path");
        // store path (with the callsite) for a function recursive check later
        plist.emplace_back(path);
        break;

      case FilterAnalysis::Continue:
      case FilterAnalysis::Filter:
          break;

      default:
        llvm_unreachable("unknown/undefined enum value");
        break;
      }
    }

    const auto successors = search_dir.search(current, path);
    for (auto* successor : successors) {
      if constexpr (OmpHelper::WithOmp) {
        if (OmpHelper::isTaskRelatedStore(successor)) {
          LOG_DEBUG("Keep, passed to OMP task struct. Current: " << *current << " Succ: " << *successor)
          path.push(successor);
          return false;
        }
      }

      if (path.contains(successor)) {
        // Avoid recursion (e.g., with store inst pointer operands pointing to an allocation)
        continue;
      }

      if (const auto filter = DFSfilter(successor, path, plist); !filter) {
        return false;
      }
    }

    path.pop();
    return true;
  }

  FilterAnalysis callsite(const llvm::CallBase &site, const Path& path) {
    // needs to be either a CallInst or an InvokeInst
    if (llvm::isa<llvm::CallBrInst>(site)) {
      return FilterAnalysis::Continue;
    }

    // Indirect calls (sth. like function pointers)
    if (site.isIndirectCall()) {
      if constexpr (CallSiteHandler::Support::Indirect) {
        auto status = handler.indirect(site, path);
        LOG_DEBUG("Indirect call: " << util::try_demangle(site))
        return status;
      } else {
        LOG_DEBUG("Indirect call, keep: " << util::try_demangle(site))
        return FilterAnalysis::Keep;
      }
    }

    const auto *callee      = site.getCalledFunction();
    const bool is_decl      = callee->isDeclaration();
    const bool is_intrinsic = callee->getIntrinsicID() != Intrinsic::not_intrinsic;

    // Handle decl
    if (is_decl) {
      if (is_intrinsic) {
        if constexpr (CallSiteHandler::Support::Intrinsic) {
          auto status = handler.intrinsic(site, path);
          LOG_DEBUG("Intrinsic call: " << util::try_demangle(site))
          return status;
        } else {
          LOG_DEBUG("Skip intrinsic: " << util::try_demangle(site))
          return FilterAnalysis::Skip;
        }
      }

      if constexpr (OmpHelper::WithOmp) {
        // here we handle microtask executor functions:
        if (OmpHelper::isOmpExecutor(site)) {
          LOG_DEBUG("Omp executor, follow microtask: " << util::try_demangle(site))
          return FilterAnalysis::FollowDef;
        }

        if (OmpHelper::isOmpHelper(site)) {
          LOG_DEBUG("Omp helper, skip: " << util::try_demangle(site))
          return FilterAnalysis::Skip;
        }
      }

      // Handle decl (like MPI calls)
      if constexpr (CallSiteHandler::Support::Declaration) {
        auto status = handler.decl(site, path);
        LOG_DEBUG("Decl call: " << util::try_demangle(site))
        return status;
      } else {
        LOG_DEBUG("Declaration, keep: " << util::try_demangle(site))
        return FilterAnalysis::Keep;
      }
    } else {
      // Handle definitions
      if constexpr (CallSiteHandler::Support::Definition) {
        auto status = handler.def(site, path);
        LOG_DEBUG("Defined call: " << util::try_demangle(site))
        return status;
      } else {
        LOG_DEBUG("Definition, keep: " << util::try_demangle(site))
        return FilterAnalysis::Keep;
      }
    }
  }
};

}  // namespace typeart::filter

#endif  // TYPEART_FILTERBASE_H
