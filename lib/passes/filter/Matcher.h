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

#ifndef TYPEART_MATCHER_H
#define TYPEART_MATCHER_H

#include "../analysis/MemOpData.h"
#include "../support/Util.h"

#include "llvm/ADT/StringSet.h"

namespace typeart::filter {

class Matcher {
 public:
  enum class MatchResult : int { Match, NoMatch, ShouldSkip, ShouldContinue };
  Matcher()               = default;
  Matcher(const Matcher&) = default;
  Matcher(Matcher&&)      = default;
  Matcher& operator=(const Matcher&) = default;
  Matcher& operator=(Matcher&&) = default;

  virtual MatchResult match(const llvm::CallBase &) const = 0;

  virtual ~Matcher() = default;
};

template<Matcher::MatchResult Result>
class StaticMatcher final : public Matcher {
 public:
  MatchResult match(const llvm::CallBase &) const override {
    return Result;
  };
};

using NoMatcher = StaticMatcher<Matcher::MatchResult::NoMatch>;
using AnyMatcher = StaticMatcher<Matcher::MatchResult::Match>;

class DefaultStringMatcher final : public Matcher {
  Regex matcher;

 public:
  explicit DefaultStringMatcher(const std::string& regex) : matcher(regex, Regex::NoFlags) {
  }

  MatchResult match(const llvm::CallBase &c) const override {
    auto *const f = c.getCalledFunction();
    if (f != nullptr) {
      const auto f_name  = util::demangle(f->getName());
      const bool matched = matcher.match(f_name);
      if (matched) {
        return MatchResult::Match;
      }
    }
    return MatchResult::NoMatch;
  }
};

class FunctionOracleMatcher final : public Matcher {
  const MemOps mem_operations{};
  llvm::SmallDenseSet<llvm::StringRef> continue_set{{"sqrt"}, {"cos"}, {"sin"},    {"pow"},  {"fabs"},
                                                    {"abs"},  {"log"}, {"fscanf"}, {"cbrt"}, {"gettimeofday"}};
  llvm::SmallDenseSet<llvm::StringRef> skip_set{{"printf"}, {"sprintf"},      {"snprintf"}, {"fprintf"},
                                                {"puts"},   {"__cxa_atexit"}, {"fopen"},    {"fclose"},
                                                {"scanf"},  {"strtol"},       {"srand"}};

 public:
  MatchResult match(const llvm::CallBase &c) const override {
    auto *const f = c.getCalledFunction();
    if (f != nullptr) {
      const auto f_name = util::demangle(f->getName());
      StringRef f_name_ref{f_name};
      if (continue_set.count(f_name) > 0) {
        return MatchResult::ShouldContinue;
      }
      if (skip_set.count(f_name) > 0) {
        return MatchResult::ShouldSkip;
      }
      if (f_name_ref.startswith("__typeart_")) {
        return MatchResult::ShouldSkip;
      }
      if (mem_operations.kind(f_name)) {
        return MatchResult::ShouldSkip;
      }
      if (f_name_ref.startswith("__ubsan") || f_name_ref.startswith("__asan") || f_name_ref.startswith("__msan")) {
        return MatchResult::ShouldContinue;
      }
    }
    return MatchResult::NoMatch;
  }
};

}  // namespace typeart::filter

#endif  // TYPEART_MATCHER_H
