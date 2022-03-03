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

  virtual MatchResult match(const CallBase &, const Function &) const = 0;

  virtual ~Matcher() = default;
};

template<Matcher::MatchResult Result>
class StaticMatcher final : public Matcher {
 public:
  MatchResult match(const CallBase &Site, const Function &Callee) const override {
    assert(Site.isIndirectCall() || Site.getCalledFunction() == &Callee);

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

  MatchResult match(const CallBase &Site, const Function &Callee) const override {
    assert(Site.isIndirectCall() || Site.getCalledFunction() == &Callee);

    const auto f_name  = util::demangle(Callee.getName());
    const bool matched = matcher.match(f_name);
    return matched ? MatchResult::Match : MatchResult::NoMatch;
  }
};

class FunctionOracleMatcher final : public Matcher {
  const MemOps mem_operations{};
  const StringMap<MatchResult> knownFunctions = {
    {"sqrt", MatchResult::ShouldContinue},
    {"cos", MatchResult::ShouldContinue},
    {"sin", MatchResult::ShouldContinue},
    {"pow", MatchResult::ShouldContinue},
    {"fabs", MatchResult::ShouldContinue},
    {"abs", MatchResult::ShouldContinue},
    {"log", MatchResult::ShouldContinue},
    {"fscanf", MatchResult::ShouldContinue},
    {"cbrt", MatchResult::ShouldContinue},
    {"gettimeofday", MatchResult::ShouldContinue},

    {"printf", MatchResult::ShouldSkip},
    {"sprintf", MatchResult::ShouldSkip},
    {"snprintf", MatchResult::ShouldSkip},
    {"fprintf", MatchResult::ShouldSkip},
    {"puts", MatchResult::ShouldSkip},
    {"__cxa_atexit", MatchResult::ShouldSkip},
    {"fopen", MatchResult::ShouldSkip},
    {"fclose", MatchResult::ShouldSkip},
    {"scanf", MatchResult::ShouldSkip},
    {"strtol", MatchResult::ShouldSkip},
    {"srand", MatchResult::ShouldSkip},
  };

 public:
  MatchResult match(const CallBase &Site, const Function &Callee) const override {
    assert(Site.isIndirectCall() || Site.getCalledFunction() == &Callee);

    const auto f_name = util::demangle(Callee.getName());
    StringRef f_name_ref{f_name};

    if (auto It = knownFunctions.find(f_name); It != knownFunctions.end()) {
      return It->second;
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
    return MatchResult::NoMatch;
  }
};

}  // namespace typeart::filter

#endif  // TYPEART_MATCHER_H
