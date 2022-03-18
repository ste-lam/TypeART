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

#ifndef TYPEART_FILTER_H
#define TYPEART_FILTER_H

namespace llvm {
class Value;
class Function;
class Module;
}  // namespace llvm

namespace typeart::filter {

class Filter {
 public:
  Filter()              = default;
  Filter(const Filter&) = default;
  Filter(Filter&&)      = default;
  Filter& operator=(const Filter&) = default;
  Filter& operator=(Filter&&) = default;
  virtual ~Filter() = default;

  virtual void reset(llvm::Module &)                = 0;
  virtual bool filter(llvm::Value*)                 = 0;
  virtual void setStartingFunction(llvm::Function*) = 0;
  virtual void setMode(bool)                        = 0;
};

class NoOpFilter final : public Filter {
 public:
  void reset(llvm::Module& Module) override {
  }
  bool filter(llvm::Value*) override {
    return false;
  }
  void setStartingFunction(llvm::Function*) override {
  }
  void setMode(bool) override {
  }
};

}  // namespace typeart::filter

#endif  // TYPEART_FILTER_H
