// Copyright (c) 2022 The Khronos Group Inc.
// Copyright (c) 2022 LunarG Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SOURCE_OPT_CONVERGENCE_REGION_H_
#define SOURCE_OPT_CONVERGENCE_REGION_H_

#include <cstdint>
#include <unordered_set>
#include "source/opt/basic_block.h"
#include "source/opt/cfg.h"

namespace spvtools {
namespace opt {

class IRContext;
class Instruction;

namespace analysis {

struct Region;

namespace {
  using BlockSet = std::unordered_set<const BasicBlock*>;
  using RegionSet = std::unordered_set<std::unique_ptr<Region>>;
} // namespace

struct Region {

  // The entry and exit nodes to this region.
  const BasicBlock *entry;
  const BasicBlock *exit;

  // All the nodes in this region, including subregion nodes, and entry/exit nodes.
  BlockSet nodes;

  // Regions contained inside this region.
  RegionSet subregions;

  Region(const BasicBlock *entry, const BasicBlock *exit, BlockSet nodes, RegionSet&& subregions)
    : entry(entry), exit(exit), nodes(nodes), subregions(std::move(subregions))
  {
    assert(nodes.count(entry) != 0 && nodes.count(exit) != 0);
    for (const auto& subregion : subregions)
      for (const BasicBlock *node : subregion->nodes)
        assert(nodes.count(node) != 0);
  }

  Region(const BasicBlock *entry, const BasicBlock *exit)
    : entry(entry), exit(exit) {
      nodes.insert(entry);
      nodes.insert(exit);
  }

};

class ConvergenceRegionManager {
  IRContext *context_;
  RegionSet regions_;

 public:
  ConvergenceRegionManager(IRContext* ctx);

  const RegionSet& GetRegions() const {
    (void) context_;
    return regions_;
  }
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_CONVERGENCE_REGION_H_
