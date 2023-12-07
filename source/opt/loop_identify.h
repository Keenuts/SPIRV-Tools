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

#ifndef SOURCE_OPT_LOOP_IDENTIFY_H_
#define SOURCE_OPT_LOOP_IDENTIFY_H_

#include <cstdint>
#include <unordered_set>
#include "source/opt/basic_block.h"
#include "source/opt/dominator_analysis.h"
#include "source/opt/cfg.h"

namespace spvtools {
namespace opt {

class IRContext;
class Instruction;

namespace analysis {

namespace {
  template<typename T>
  struct PairHash {
    size_t operator()(const std::pair<T, T>& p) const {
      const size_t a = std::hash<T>{}(p.first);
      const size_t b = std::hash<T>{}(p.second);
      return a ^ (b << 1);
    }
  };
} // end anonymous namespace.

class LoopManager {
public:
  using BlockSet = std::unordered_set<const BasicBlock*>;
  using EdgeSet = std::unordered_set<std::pair<const BasicBlock*, const BasicBlock*>, PairHash<const BasicBlock*>>;

  struct LoopInfo {
    const BasicBlock *header;
    BlockSet nodes;
    BlockSet exits;
    std::vector<LoopInfo> children;
  };

  using LoopVector = std::vector<LoopInfo>;

 public:
  LoopManager(IRContext* ctx);

  const LoopVector& GetLoops(const opt::Function* function) const {
    assert(loops_.count(function) != 0);
    return loops_.at(function);
  }

  const EdgeSet& GetBackEdges(const opt::Function* function) const {
    assert(back_edges_.count(function) != 0);
    return back_edges_.at(function);
  }
 private:

  std::unordered_set<const BasicBlock*> GetLoopBlocks(const BasicBlock *header) const;
  std::unordered_set<const BasicBlock*> GetLoopExitBlocks(const BlockSet& loop) const;
  bool IsTargetBackEdge(const BasicBlock *block) const;
  LoopVector FindLoops(const BasicBlock *entry, const BlockSet& area) const;
  LoopVector GetLoopsForFunction(const opt::Function& function) const;

  EdgeSet FindBackEdges(const BasicBlock *entry) const;

private:

  IRContext *context_;
  DominatorTree dtree_;
  std::unordered_map<const opt::Function*, LoopVector> loops_;
  std::unordered_map<const opt::Function*, EdgeSet> back_edges_;
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_LOOP_IDENTIFY_H_
