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
  ConvergenceRegionManager(IRContext* ctx);

  bool HasToken(const BasicBlock* block) const {
    return block_to_token_.count(block) != 0;
  }

  uint32_t GetToken(const BasicBlock* block) const {
    assert(HasToken(block));
    return block_to_token_.at(block);
  }
#if 0
  const LoopManager::BlockSet& GetBlocks(const Instruction* token) const {
    static LoopManager::BlockSet empty_set;
    if (token_to_blocks_.count(token) != 0)
      return token_to_blocks_[token];
    return empty_set;
  }
#endif

private:

  LoopManager::BlockSet FindPathsToMatch(const LoopManager::EdgeSet& back_edges,
                                         const BasicBlock *node,
                                         std::function<bool(const BasicBlock*)> isMatch) const;

  void IdentifyConvergenceRegions(const opt::Function& function);

private:

  IRContext *context_;
  DominatorTree dtree_;

  std::unordered_map<const BasicBlock*, uint32_t> block_to_token_;
  //std::unordered_map<const Instruction*, LoopManager::BlockSet> token_to_blocks_;
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_CONVERGENCE_REGION_H_
