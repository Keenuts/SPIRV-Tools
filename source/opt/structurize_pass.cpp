// Copyright (c) 2018 Google LLC
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

#include "source/opt/structurize_pass.h"

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <vector>

#include "source/opt/instruction.h"
#include "source/opt/ir_context.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

struct Construction {
  const BasicBlock *header;
  const BasicBlock *merge;
  const BasicBlock *continue_target;
};

// Returns all basic blocks with an in-degree of 2 or more.
std::vector<const BasicBlock*> find_interesting_blocks(const CFG& cfg, const Function& function) {
  std::vector<const BasicBlock*> interesting;

  for (const auto& block : function) {
    if (cfg.preds(block.id()).size() > 1) {
      interesting.push_back(&block);
    }
  }

  return interesting;
}

// Given a list of basic blocks with in-degree >= 2, the first post-dominator.
const BasicBlock* get_next_task(const CFG& cfg, const std::vector<const BasicBlock*>& candidates) {
  assert(candidates.size() != 0);
  DominatorTree dtree;
  dtree.InitializeTree(cfg, candidates[0]->GetParent());
  //const auto id = dtree.post_begin()->id();
  auto first = std::find_first_of(dtree.begin(), dtree.end(), candidates.begin(), candidates.end(), [](auto lhs, auto rhs) {
      return lhs.id() == rhs->id();
  });
  return first != dtree.end() ? cfg.block(first->id()) : nullptr;
}

std::unordered_set<const BasicBlock*> get_loop_nodes(const CFG& cfg, const DominatorTree& dtree, const BasicBlock* start) {
  std::unordered_set<const BasicBlock*> output;
  std::unordered_set<const BasicBlock*> visited;

  std::queue<const BasicBlock*> to_process;
  to_process.push(start);

  while (to_process.size() != 0) {
    auto item = to_process.front();
    to_process.pop();

    if (visited.count(item) != 0)
      continue;
    visited.insert(item);

    if (!dtree.Dominates(start, item)) {
      continue;
    }

    output.insert(item);
    for (auto p : cfg.preds(item))
      to_process.push(p);
  }

  return output;
}

std::unordered_set<const BasicBlock*> find_exit_nodes(const CFG& cfg, const std::unordered_set<const BasicBlock*>& loop) {
  std::unordered_set<const BasicBlock*> output;

  for (const auto node : loop) {
    node->ForEachSuccessorLabel([&cfg, &loop, &output](const uint32_t id) {
      if (loop.count(cfg.block(id)) == 0)
        output.insert(cfg.block(id));
    });
  }

  return output;
}

const BasicBlock* find_merge_block(const CFG& cfg, const DominatorTree& pdtree, std::unordered_set<const BasicBlock*> exit_blocks) {
  assert(pdtree.IsPostDominator());

  std::queue<const BasicBlock*> to_process;
  std::unordered_set<const BasicBlock *> visited;
  for (auto blk : exit_blocks)
    to_process.push(blk);

  while (to_process.size() != 0) {
    auto blk = to_process.front();
    to_process.pop();

    if (visited.count(blk) != 0)
      continue;
    visited.insert(blk);

    const bool immediate_pdom = std::all_of(exit_blocks.cbegin(), exit_blocks.cend(), [&pdtree, &blk](const BasicBlock* pred) {
          return pdtree.Dominates(blk, pred);
    });

    if (immediate_pdom) {
      return blk;
    }

    blk->ForEachSuccessorLabel([&cfg, &to_process](const uint32_t id) {
      to_process.push(cfg.block(id));
    });
  }

  return nullptr;
}

// Given a list of basic-blocks with an in-degree of 2, this function figures out what kind of
// construct those are attached to.
std::vector<Construction> identify_constructions(const CFG& cfg, std::vector<const BasicBlock*> candidates) {
  std::vector<Construction> output;
  if (candidates.size() == 0)
    return output;

  DominatorTree dtree;
  dtree.InitializeTree(cfg, candidates[0]->GetParent());

  DominatorTree pdtree(/* postdominator= */ true);
  pdtree.InitializeTree(cfg, candidates[0]->GetParent());

  while (candidates.size() != 0) {
    const BasicBlock* item = get_next_task(cfg, candidates);
    candidates.erase(std::find(candidates.cbegin(), candidates.cend(), item));
    std::cout << "Figuring out construction for node " << item->id() << std::endl;

    std::vector<const BasicBlock*> predecessors = cfg.preds(item);
    assert(predecessors.size() >= 2);

    std::vector<const BasicBlock*> dominated;
    for (auto pred : predecessors) {
      if (dtree.Dominates(item, pred))
        dominated.push_back(pred);
    }

    // There is no back-edge.
    if (dominated.size() == 0) {
      std::cout << "block is a condition merge block." << std::endl;
      Construction c;
      c.merge = item;
      c.header = dtree.ImmediateDominator(item);
      c.continue_target = nullptr;

      assert(c.header != nullptr);
      assert(c.merge != nullptr);
      assert(c.continue_target == nullptr);
      output.push_back(c);
      continue;
    }

    // There is a back-edge.
    std::cout << "block is part of a loop." << std::endl;
    auto nodes_in_loop = get_loop_nodes(cfg, dtree, item);
    std::cout << "nodes in the loop:" << std::endl;
    for (const auto& node : nodes_in_loop) {
      std::cout << "   - " << node->id() << std::endl;
    }

    Construction c;
    c.header = item;
    c.merge = nullptr;
    c.continue_target = nullptr;

    // Finding the continue target.
    for (auto pred : predecessors) {
      if (nodes_in_loop.count(pred) != 0) {
        assert(c.continue_target == nullptr);
        c.continue_target = pred;
      }
    }

    auto exit_blocks = find_exit_nodes(cfg, nodes_in_loop);
    if (exit_blocks.size() == 0)
      assert(0 && "Loop with no exit. Don't know what to do.");
    else if (exit_blocks.size() == 1)
      c.merge = *exit_blocks.begin();
    else {
      c.merge = find_merge_block(cfg, pdtree, exit_blocks);
    }

    assert(c.header != nullptr);
    assert(c.continue_target != nullptr);
    assert(c.merge != nullptr);
    output.push_back(c);
  }

  return output;
}

Pass::Status StructurizePass::Process() {
  bool modified = false;
  const auto& cfg = *context()->cfg();

  for (const auto& function : *context()->module()) {
    auto interesting = find_interesting_blocks(cfg, function);
    for (auto block : interesting) {
      std::cout << "candidate: " << block->id() << std::endl;
    }
    if (interesting.size() == 0)
      continue;

    auto constructions = identify_constructions(cfg, interesting);
    for (auto item : constructions) {
      std::cout << (item.continue_target ? "loop:" : "condition:") << std::endl;
      std::cout << "    header:" << item.header->id() << std::endl;
      std::cout << "     merge:" << item.merge->id() << std::endl;
      if (item.continue_target) {
        std::cout << "  continue:" << item.continue_target->id() << std::endl;
      }
    }
    //const auto task = get_next_task(cfg, interesting);
    //std::cout << "task: " << task->id() << std::endl;
  }


  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools

