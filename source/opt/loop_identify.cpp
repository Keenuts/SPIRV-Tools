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

#include "source/opt/loop_identify.h"

#include "source/opt/ir_context.h"

namespace spvtools {
namespace opt {
namespace analysis {

namespace {

using BlockSet = LoopManager::BlockSet;
using EdgeSet = LoopManager::EdgeSet;
using LoopInfo = LoopManager::LoopInfo;
using LoopVector = LoopManager::LoopVector;

} // anonymous namespace

std::unordered_set<const BasicBlock*> LoopManager::GetLoopBlocks(const BasicBlock *header) const {
  std::unordered_set<const BasicBlock*> output;
  std::unordered_set<const BasicBlock*> visited;

  std::queue<const BasicBlock*> to_process;
  to_process.push(header);

  while (to_process.size() != 0) {
    auto item = to_process.front();
    to_process.pop();

    if (visited.count(item) != 0)
      continue;
    visited.insert(item);

    if (!dtree_.Dominates(header, item)) {
      continue;
    }

    output.insert(item);
    for (auto p : context_->cfg()->preds(item))
      to_process.push(p);
  }

  return output;
}

std::unordered_set<const BasicBlock*> LoopManager::GetLoopExitBlocks(const BlockSet& loop) const {
  BlockSet output;
  for (const BasicBlock *block : loop) {
    for (auto child : context_->cfg()->successors(block)) {
      if (loop.count(child) == 0)
        output.insert(child);
    }
  }

  return output;
}

bool LoopManager::IsTargetBackEdge(const BasicBlock *block) const {
  BlockSet predecessors = context_->cfg()->preds(block);
  for (const BasicBlock *p : predecessors) {
    if (dtree_.Dominates(block, p))
      return true;
  }

  return false;
}

EdgeSet LoopManager::FindBackEdges(const BasicBlock *entry) const {
  EdgeSet output;

  BlockSet visited;
  std::queue<const BasicBlock*> to_process;
  to_process.push(entry);

  while (to_process.size() != 0) {
    const BasicBlock *item = to_process.front();
    to_process.pop();

    if (visited.count(item) != 0) {
      continue;
    }
    visited.insert(item);

    BlockSet predecessors = context_->cfg()->preds(item);
    for (const BasicBlock *predecessor : predecessors) {
      if (dtree_.Dominates(item, predecessor))
        output.insert({ predecessor, item });
    }

    BlockSet successors = context_->cfg()->successors(item);
    for (const BasicBlock *successor : successors) {
      to_process.push(successor);
    }
  }

  return output;
}

LoopVector LoopManager::FindLoops(const BasicBlock *entry, const BlockSet& area) const {
  LoopVector output;

  BlockSet visited;
  std::queue<const BasicBlock*> to_process;
  to_process.push(entry);

  while (to_process.size() != 0) {
    const BasicBlock *item = to_process.front();
    to_process.pop();

    if (area.count(item) == 0) {
      continue;
    }

    if (visited.count(item) != 0) {
      continue;
    }
    visited.insert(item);

    if (IsTargetBackEdge(item)) {
      LoopInfo loop;
      loop.header = item;
      loop.nodes = GetLoopBlocks(loop.header);
      loop.exits = GetLoopExitBlocks(loop.nodes);

      BlockSet children = context_->cfg()->successors(item);
      BlockSet recursion_area = loop.nodes;
      recursion_area.erase(loop.header);
      for (const BasicBlock *child : children) {
        auto children_loops = FindLoops(child, recursion_area);
        loop.children.insert(loop.children.end(), children_loops.begin(), children_loops.end());
      }

      for (const BasicBlock* exit : loop.exits) {
        to_process.push(exit);
      }
      output.push_back(std::move(loop));

    } else {
      BlockSet children = context_->cfg()->successors(item);
      for (const BasicBlock* child : children) {
        to_process.push(child);
      }
    }
  }

  return output;
}

LoopVector LoopManager::GetLoopsForFunction(const opt::Function& function) const {
  BlockSet area;
  for (const BasicBlock& block : function) {
    area.insert(&block);
  }

  return FindLoops(function.entry().get(), area);
}

LoopManager::LoopManager(IRContext* context)
  : context_(context), dtree_(/* postdominator= */ false)  {
  for (const opt::Function& function : *context->module()) {
    dtree_.InitializeTree(*context_->cfg(), &function);

    const opt::Function *key = &function;
    LoopVector loops = GetLoopsForFunction(function);
    loops_.emplace(std::move(key), std::move(loops));

    EdgeSet back_edges = FindBackEdges(function.entry().get());
    back_edges_.emplace(std::move(key), std::move(back_edges));
  }
}

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools
