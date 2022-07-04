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

#include "CallFilter.h"

#include "Matcher.h"
#include "StdForwardFilter.h"
#include "CGForwardFilter.h"

namespace typeart::analysis {

std::unique_ptr<typeart::filter::Filter> FilterBuilder::operator()() {

  using namespace typeart::filter;
  const auto FilterImplementation = config.ClUseCallFilter ? config.implementation : FilterImplementation::none;

  switch (FilterImplementation) {
    case FilterImplementation::none: {
      LOG_DEBUG("Return no-op filter")
      return std::make_unique<NoOpFilter>();
    }

    case FilterImplementation::standard: {
      LOG_DEBUG("Return default filter")
      const auto glob      = config.ClCallFilterGlob;
      auto matcher         = std::make_unique<DefaultStringMatcher>(util::glob2regex(glob));
      const auto deep_glob = config.ClCallFilterDeepGlob;
      auto deep_matcher    = std::make_unique<DefaultStringMatcher>(util::glob2regex(deep_glob));
      return std::make_unique<StandardForwardFilter>(std::move(matcher), std::move(deep_matcher));
    }

    case FilterImplementation::cg: {
      if (config.ClCallFilterCGFile.empty()) {
        LOG_FATAL("CG File not set!");
        std::exit(EXIT_FAILURE);
      }
      LOG_DEBUG("Return CG filter with CG file @ " << config.ClCallFilterCGFile)

      const auto glob = config.ClCallFilterGlob;
      auto json_cg    = JSONCG::getJSON(config.ClCallFilterCGFile);
      auto matcher    = std::make_unique<DefaultStringMatcher>(util::glob2regex(glob));
      return std::make_unique<CGForwardFilter>(glob, std::move(json_cg), std::move(matcher));
    }

    case FilterImplementation::plugin: {
      assert(builderCallback);
      return builderCallback(config);
    }
  }
}

FilterBuilder::FilterBuilder(const FilterConfig &Config) : config(Config) {
  FilterBuilder::registerBuilderCallback([](const FilterConfig &) -> std::unique_ptr<typeart::filter::Filter> {
    LOG_FATAL("Plugin Callback-Handler not set!");
    std::exit(EXIT_FAILURE);
  });

  if (const auto &Filename = config.ClCallFilterPlugin; !Filename.empty()) {
    auto Plugin = FilterPlugin::load(Filename);
    if (Plugin) {
      Plugin->registerBuilderCallback(*this);
    } else {
      LOG_ERROR(toString(Plugin.takeError()))
    }
  }
}

void FilterBuilder::registerBuilderCallback(const std::function<std::unique_ptr<typeart::filter::Filter>(const FilterConfig &)> &C) {
  builderCallback = C;
}

FilterBuilder::~FilterBuilder() {
  builderCallback = nullptr;
}

CallFilter::CallFilter(std::unique_ptr<typeart::filter::Filter> filter) : fImpl(std::move(filter)) {}


bool CallFilter::operator()(llvm::AllocaInst* in) {
  LOG_DEBUG("Analyzing value: " << util::dump(*in));
  fImpl->setMode(/*search mallocs = */ false);
  fImpl->setStartingFunction(in->getFunction());
  const auto filter_ = fImpl->filter(in);
  if (filter_) {
    LOG_DEBUG("Filtering value: " << util::dump(*in) << "\n");
  } else {
    LOG_DEBUG("Keeping value: " << util::dump(*in) << "\n");
  }
  return filter_;
}

bool CallFilter::operator()(llvm::GlobalValue* g) {
  LOG_DEBUG("Analyzing value: " << util::dump(*g));
  fImpl->setMode(/*search mallocs = */ false);
  fImpl->setStartingFunction(nullptr);
  const auto filter_ = fImpl->filter(g);
  if (filter_) {
    LOG_DEBUG("Filtering value: " << util::dump(*g) << "\n");
  } else {
    LOG_DEBUG("Keeping value: " << util::dump(*g) << "\n");
  }
  return filter_;
}


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

  FilterPlugin Plugin{Filename};
  Plugin.Info = reinterpret_cast<decltype(typeartGetFilterPluginInfo)*>(SymbolAddress)();

  if (Plugin.Info.APIVersion != TYPEART_PLUGIN_API_VERSION) {
    return make_error<StringError>(Twine("Wrong API version on plugin '") + Filename + "'. Got version " +
                                       Twine(Plugin.Info.APIVersion) + ", supported version is " +
                                       Twine(TYPEART_PLUGIN_API_VERSION) + ".",
                                   inconvertibleErrorCode());
  }

  if (Plugin.Info.BuilderCallback == nullptr) {
    return make_error<StringError>(Twine("Empty entry callback in plugin '") + Filename + "'.'",
                                   inconvertibleErrorCode());
  }

  return {Plugin};
}

FilterPlugin::FilterPlugin(std::string Filename)
    : Filename(std::move(Filename)) {
}
}
// namespace typeart::analysis
