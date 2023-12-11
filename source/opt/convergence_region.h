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
#include <utility>
#include <functional>
#include "source/opt/basic_block.h"
#include "source/opt/dominator_analysis.h"
#include "source/opt/loop_identify.h"
#include "source/opt/cfg.h"

namespace spvtools {
namespace opt {

class IRContext;
class Instruction;

namespace analysis {

class ConvergenceRegionManager {
public:
  struct Region {
    uint32_t token;
    const BasicBlock *entry;

    // Blocks belonging to this region. Includes nodes in subregions.
    LoopManager::BlockSet nodes;

    // Blocks exiting to the parent region.
    LoopManager::BlockSet exits;

    // Child regions contained in this region, and thus only accessible from this region.
    std::vector<Region*> children;
  };

  ConvergenceRegionManager(IRContext* ctx);

  bool HasToken(const BasicBlock* block) const {
    return block_to_token_.count(block) != 0;
  }

  uint32_t GetToken(const BasicBlock* block) const {
    assert(HasToken(block));
    return block_to_token_.at(block);
  }

  const std::vector<const Region*>& GetConvergenceRegions(const opt::Function *function) const {
    assert(top_level_regions_.count(function) == 1);
    return top_level_regions_.at(function);
  }

private:

  LoopManager::BlockSet FindPathsToMatch(const LoopManager::EdgeSet& back_edges,
                                         const BasicBlock *node,
                                         std::function<bool(const BasicBlock*)> isMatch) const;

  void IdentifyConvergenceRegions(const opt::Function& function);

  //void CreateRegionHierarchy(const opt::Function& function);
  //void CreateRegionHierarchy(Region *parent, const LoopManager::LoopInfo& loop);

private:

  IRContext *context_;
  DominatorTree dtree_;

  std::vector<Region*> regions_;
  std::unordered_map<const opt::Function*, std::vector<const Region*>> top_level_regions_;

  std::unordered_map<const BasicBlock*, uint32_t> block_to_token_;
  std::unordered_map<uint32_t, const Region*> token_to_region_;
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_CONVERGENCE_REGION_H_
