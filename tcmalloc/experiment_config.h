// Copyright 2019 The TCMalloc Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TCMALLOC_EXPERIMENT_CONFIG_H_
#define TCMALLOC_EXPERIMENT_CONFIG_H_

#include "absl/strings/string_view.h"

// Autogenerated by experiments_proto_test --experiments_generate_config=true
namespace tcmalloc {

enum class Experiment : int {
  TEST_ONLY_TCMALLOC_POW2_SIZECLASS,
  TEST_ONLY_TCMALLOC_POW2_BELOW64_SIZECLASS,
  TEST_ONLY_TCMALLOC_RING_BUFFER_TRANSFER_CACHE,
  TEST_ONLY_TCMALLOC_SHARDED_TRANSFER_CACHE,
  TCMALLOC_HETEROGENEOUS_CACHES,
  kMaxExperimentID,
};

struct ExperimentConfig {
  Experiment id;
  absl::string_view name;
};

// clang-format off
inline constexpr ExperimentConfig experiments[] = {
    {Experiment::TEST_ONLY_TCMALLOC_POW2_SIZECLASS, "TEST_ONLY_TCMALLOC_POW2_SIZECLASS"},
    {Experiment::TEST_ONLY_TCMALLOC_POW2_BELOW64_SIZECLASS, "TEST_ONLY_TCMALLOC_POW2_BELOW64_SIZECLASS"},
    {Experiment::TEST_ONLY_TCMALLOC_RING_BUFFER_TRANSFER_CACHE, "TEST_ONLY_TCMALLOC_RING_BUFFER_TRANSFER_CACHE"},
    {Experiment::TEST_ONLY_TCMALLOC_SHARDED_TRANSFER_CACHE, "TEST_ONLY_TCMALLOC_SHARDED_TRANSFER_CACHE"},
    {Experiment::TCMALLOC_HETEROGENEOUS_CACHES, "TCMALLOC_HETEROGENEOUS_CACHES"},
};
// clang-format on

}  // namespace tcmalloc

#endif  // TCMALLOC_EXPERIMENT_CONFIG_H_
