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

#include "FilterPlugin.h"

namespace typeart {

llvm::Expected<FilterPlugin> FilterPlugin::load(const std::string& Filename) {
  std::string Error;
  auto Library = sys::DynamicLibrary::getPermanentLibrary(Filename.c_str(), &Error);
  if (!Library.isValid()) {
    return make_error<StringError>(Twine("Could not load library '") + Filename + "': " + Error,
                                   inconvertibleErrorCode());
  }

  auto *SymbolAddress = Library.getAddressOfSymbol("typeartGetFilterPluginInfo");

  if (SymbolAddress == nullptr) {
    // If the symbol isn't found, this is probably a legacy plugin, which is an
    // error
    return make_error<StringError>(Twine("Plugin entry point not found in '") + Filename + "'.",
                                   inconvertibleErrorCode());
  }

  const auto& LibraryInfo = reinterpret_cast<decltype(typeartGetFilterPluginInfo)*>(SymbolAddress)();

  if (LibraryInfo.APIVersion != TYPEART_PLUGIN_API_VERSION) {
    return make_error<StringError>(Twine("Wrong API version on plugin '") + Filename + "'. Got version " +
                                       Twine(LibraryInfo.APIVersion) + ", supported version is " +
                                       Twine(TYPEART_PLUGIN_API_VERSION) + ".",
                                   inconvertibleErrorCode());
  }

  if (LibraryInfo.BuilderCallbacks == nullptr) {
    return make_error<StringError>(Twine("Empty entry callback in plugin '") + Filename + "'.'",
                                   inconvertibleErrorCode());
  }

  return FilterPlugin{Filename, LibraryInfo};
}

}  // namespace typeart
