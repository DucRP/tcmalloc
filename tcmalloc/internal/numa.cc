// Copyright 2021 The TCMalloc Authors
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

#include "tcmalloc/internal/numa.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <array>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "tcmalloc/internal/config.h"
#include "tcmalloc/internal/environment.h"
#include "tcmalloc/internal/logging.h"
#include "tcmalloc/internal/util.h"

GOOGLE_MALLOC_SECTION_BEGIN
namespace tcmalloc {
namespace tcmalloc_internal {

// Returns true iff NUMA awareness should be enabled by default (i.e. in the
// absence of the TCMALLOC_NUMA_AWARE environment variable). This weak
// implementation may be overridden by the one in want_numa_aware.cc.
ABSL_ATTRIBUTE_WEAK bool default_want_numa_aware() { return false; }

int OpenSysfsCpulist(size_t node) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/sys/devices/system/node/node%zu/cpulist",
           node);
  return signal_safe_open(path, O_RDONLY | O_CLOEXEC);
}

cpu_set_t ParseCpulist(absl::FunctionRef<ssize_t(char*, size_t)> read) {
  cpu_set_t set;
  CPU_ZERO(&set);

  std::array<char, 16> buf;
  size_t carry_over = 0;
  int cpu_from = -1;

  while (true) {
    const ssize_t rc = read(buf.data() + carry_over, buf.size() - carry_over);
    CHECK_CONDITION(rc >= 0);

    const absl::string_view current(buf.data(), carry_over + rc);

    // If we have no more data to parse & couldn't read any then we've reached
    // the end of the input & are done.
    if (current.empty() && rc == 0) {
      break;
    }

    size_t consumed;
    const size_t dash = current.find('-');
    const size_t comma = current.find(',');
    if (dash != absl::string_view::npos && dash < comma) {
      CHECK_CONDITION(absl::SimpleAtoi(current.substr(0, dash), &cpu_from));
      consumed = dash + 1;
    } else if (comma != absl::string_view::npos || rc == 0) {
      int cpu;
      CHECK_CONDITION(absl::SimpleAtoi(current.substr(0, comma), &cpu));
      if (comma == absl::string_view::npos) {
        consumed = current.size();
      } else {
        consumed = comma + 1;
      }
      if (cpu_from != -1) {
        for (int c = cpu_from; c <= cpu; c++) {
          CPU_SET(c, &set);
        }
        cpu_from = -1;
      } else {
        CPU_SET(cpu, &set);
      }
    } else {
      consumed = 0;
    }

    carry_over = current.size() - consumed;
    memmove(buf.data(), buf.data() + consumed, carry_over);
  }

  return set;
}

bool InitNumaTopology(size_t cpu_to_scaled_partition[CPU_SETSIZE],
                      uint64_t* const partition_to_nodes,
                      NumaBindMode* const bind_mode,
                      const size_t num_partitions, const size_t scale_by,
                      absl::FunctionRef<int(size_t)> open_node_cpulist) {
  // Node 0 will always map to partition 0; record it here in case the system
  // doesn't support NUMA or the user opts out of our awareness of it - in
  // either case we'll record nothing in the loop below.
  partition_to_nodes[NodeToPartition(0, num_partitions)] |= 1 << 0;

  // If we only compiled in support for one partition then we're trivially
  // done; NUMA awareness is unavailable.
  if (num_partitions == 1) return false;

  // We rely on rseq to quickly obtain a CPU ID & lookup the appropriate
  // partition in NumaTopology::GetCurrentPartition(). If rseq is unavailable,
  // disable NUMA awareness.
  if (!subtle::percpu::IsFast()) return false;

  // Honor default_want_numa_aware() to allow compile time configuration of
  // whether to enable NUMA awareness by default, and allow the user to
  // override that either way by setting TCMALLOC_NUMA_AWARE in the
  // environment.
  //
  // In cases where we don't enable NUMA awareness we simply return. Since the
  // cpu_to_scaled_partition & partition_to_nodes arrays are zero initialized
  // we're trivially done - CPUs all map to partition 0, which contains only
  // CPU 0 added above.
  const char* e =
      tcmalloc::tcmalloc_internal::thread_safe_getenv("TCMALLOC_NUMA_AWARE");
  if (e == nullptr) {
    // Enable NUMA awareness iff default_want_numa_aware().
    if (!default_want_numa_aware()) return false;
  } else if (!strcmp(e, "no-binding")) {
    // Enable NUMA awareness with no memory binding behavior.
    *bind_mode = NumaBindMode::kNone;
  } else if (!strcmp(e, "advisory-binding") || !strcmp(e, "1")) {
    // Enable NUMA awareness with advisory memory binding behavior.
    *bind_mode = NumaBindMode::kAdvisory;
  } else if (!strcmp(e, "strict-binding")) {
    // Enable NUMA awareness with strict memory binding behavior.
    *bind_mode = NumaBindMode::kStrict;
  } else if (!strcmp(e, "0")) {
    // Disable NUMA awareness.
    return false;
  } else {
    Crash(kCrash, __FILE__, __LINE__, "bad TCMALLOC_NUMA_AWARE env var", e);
  }

  // The cpu_to_scaled_partition array has a fixed size so that we can
  // statically allocate it & avoid the need to check whether it has been
  // allocated prior to lookups. It has CPU_SETSIZE entries which ought to be
  // sufficient, but sanity check that indexing it by CPU number shouldn't
  // exceed its bounds.
  int num_cpus = absl::base_internal::NumCPUs();
  CHECK_CONDITION(num_cpus <= CPU_SETSIZE);

  // We could just always report that we're NUMA aware, but if a NUMA-aware
  // binary runs on a system that doesn't include multiple NUMA nodes then our
  // NUMA awareness will offer no benefit whilst incurring the cost of
  // redundant work & stats. As such we only report that we're NUMA aware if
  // there's actually NUMA to be aware of, which we track here.
  bool numa_aware = false;

  for (size_t node = 0;; node++) {
    // Detect NUMA nodes by opening their cpulist files from sysfs.
    const int fd = open_node_cpulist(node);
    if (fd == -1) {
      // We expect to encounter ENOENT once node surpasses the actual number of
      // nodes present in the system. Any other error is a problem.
      CHECK_CONDITION(errno == ENOENT);
      break;
    }

    // Record this node in partition_to_nodes.
    const size_t partition = NodeToPartition(node, num_partitions);
    partition_to_nodes[partition] |= 1 << node;

    // cpu_to_scaled_partition_ entries are default initialized to zero, so
    // skip redundantly parsing CPU lists for nodes that map to partition 0.
    if (partition == 0) {
      signal_safe_close(fd);
      continue;
    }

    // Parse the cpulist file to determine which CPUs are local to this node.
    const cpu_set_t node_cpus =
        ParseCpulist([&](char* const buf, const size_t count) {
          return signal_safe_read(fd, buf, count, /*bytes_read=*/nullptr);
        });

    // Assign local CPUs to the appropriate partition.
    for (size_t cpu = 0; cpu < CPU_SETSIZE; cpu++) {
      if (CPU_ISSET(cpu, &node_cpus)) {
        cpu_to_scaled_partition[cpu + kNumaCpuFudge] = partition * scale_by;
      }
    }

    // If we observed any CPUs for this node then we've now got CPUs assigned
    // to a non-zero partition; report that we're NUMA aware.
    if (CPU_COUNT(&node_cpus) != 0) {
      numa_aware = true;
    }

    signal_safe_close(fd);
  }

  return numa_aware;
}

}  // namespace tcmalloc_internal
}  // namespace tcmalloc
GOOGLE_MALLOC_SECTION_END
