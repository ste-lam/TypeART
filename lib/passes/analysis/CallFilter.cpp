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
#include "FilterPlugin.h"

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
        std::exit(1);
      }
      LOG_DEBUG("Return CG filter with CG file @ " << config.ClCallFilterCGFile)

      const auto glob = config.ClCallFilterGlob;
      auto json_cg    = JSONCG::getJSON(config.ClCallFilterCGFile);
      auto matcher    = std::make_unique<DefaultStringMatcher>(util::glob2regex(glob));
      return std::make_unique<CGForwardFilter>(glob, std::move(json_cg), std::move(matcher));
    }

    case FilterImplementation::external: {
      return builderCallbacks.back()(config);
    }
  }
}

FilterBuilder::FilterBuilder(const MemInstFinderConfig::Filter& Config) : config(Config) {
  FilterBuilder::registerBuilderCallback([](const MemInstFinderConfig::Filter&) {
    LOG_DEBUG("Return no-op filter")
    return std::make_unique<filter::NoOpFilter>();
  });

  if (const auto &Filename = config.ClCallFilterPlugin; !Filename.empty()) {
    auto Plugin = FilterPlugin::load(Filename);
    if (Plugin) {
      Plugin->registerBuilderCallbacks(*this);
    }
  }
}

void FilterBuilder::registerBuilderCallback(const FilterBuilder::CallT &C) {
  builderCallbacks.push_back(C);
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


void CallFilter::reset(llvm::Module& M) {
  LOG_DEBUG("Reset to module: " << M.getName() << "\n");
  fImpl->reset(M);
}

}
// namespace typeart::analysis
