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

#include "filter/Filter.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/Error.h>

#include <string>
#include <utility>
#include <memory>
#include <vector>
#include <functional>

namespace llvm {
class AllocaInst;
class GlobalValue;
}

namespace typeart::analysis {

using namespace llvm;

enum class FilterImplementation { none, standard, cg, plugin };

struct FilterConfig {
  bool ClFilterNonArrayAlloca{false};
  bool ClFilterMallocAllocPair{false};
  bool ClFilterGlobal{true};
  bool ClUseCallFilter{false};
  bool ClFilterPointerAlloca{false};

  // std::string ClCallFilterImpl{"default"};
  FilterImplementation implementation{FilterImplementation::standard};
  std::string ClCallFilterGlob{"*MPI_*"};
  std::string ClCallFilterDeepGlob{"MPI_*"};
  std::string ClCallFilterCGFile{};
  std::string ClCallFilterPlugin{};
  std::vector<std::string> ClCallFilterPluginOptions{};
};

class FilterBuilder {

  std::function<std::unique_ptr<typeart::filter::Filter>(const FilterConfig &)> builderCallback;
  const FilterConfig &config;

 public:
  explicit FilterBuilder(const FilterConfig &);
  ~FilterBuilder();

  void registerBuilderCallback(const std::function<std::unique_ptr<typeart::filter::Filter>(const FilterConfig &)> &);

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
  ~CallFilter() = default;

  bool operator()(llvm::AllocaInst*);
  bool operator()(llvm::GlobalValue*);
};



/// based on LLVMs pass plugin loader, adapted for TypeART

#define TYPEART_PLUGIN_API_VERSION 1

extern "C" {
struct FilterPluginLibraryInfo {
  /// The API version understood by this plugin, usually \c
  /// TYPEART_PLUGIN_API_VERSION
  uint32_t APIVersion;

  /// A meaningful name of the plugin.
  const char* PluginName;

  /// The version of the plugin.
  const char* PluginVersion;

  /// The callback for registering a plugin pass with a \c FilterBuilder
  /// instance
  void (*BuilderCallback)(analysis::FilterBuilder&);
};

/// The public entry point for a filter plugin.
///
/// When a plugin is loaded by the driver, it will call this entry point to
/// obtain information about this plugin and about how to register its passes.
/// This function needs to be implemented by the plugin, see the example below:
///
/// ```
/// extern "C"  ::typeart::FilterPluginLibraryInfo LLVM_ATTRIBUTE_UNUSED LLVM_ATTRIBUTE_WEAK
/// typeartGetFilterPluginInfo() {
///   return {
///     TYPEART_PLUGIN_API_VERSION, "MyPlugin", "v0.1", [](::typeart::analysis::FilterBuilder &FB) { ... }
///   };
/// }
/// ```
FilterPluginLibraryInfo LLVM_ATTRIBUTE_WEAK typeartGetFilterPluginInfo();
}

/// A loaded filter plugin.
///
/// An instance of this class wraps a loaded pass plugin and gives access to
/// its interface defined by the \c FilterPluginLibraryInfo it exposes.
class FilterPlugin {
 public:
  /// Attempts to load a filter plugin from a given file.
  ///
  /// \returns Returns an error if either the library cannot be found or loaded,
  /// there is no public entry point, or the plugin implements the wrong API
  /// version.
  static Expected<FilterPlugin> load(const std::string& Filename);

  /// Get the filename of the loaded plugin.
  [[nodiscard]] StringRef getFilename() const {
    return Filename;
  }

  /// Get the plugin name
  [[nodiscard]] StringRef getPluginName() const {
    return Info.PluginName;
  }

  /// Get the plugin version
  [[nodiscard]] StringRef getPluginVersion() const {
    return Info.PluginVersion;
  }

  /// Get the plugin API version
  [[nodiscard]] uint32_t getAPIVersion() const {
    return Info.APIVersion;
  }

  /// Invoke the PassBuilder callback registration
  void registerBuilderCallback(analysis::FilterBuilder &Builder) const {
    Info.BuilderCallback(Builder);
  }

 private:
  FilterPlugin(std::string Filename);

  std::string Filename;
  FilterPluginLibraryInfo Info;
};

}  // namespace typeart::analysis



#endif  // TYPEART_LIB_PASSES_ANALYSIS_CALLFILTER_H
