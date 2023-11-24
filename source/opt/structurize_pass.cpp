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

#if 0

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
#endif

namespace {

} // end anonymous namespace.


struct Construction {
  const BasicBlock *header;
  const BasicBlock *merge;
  const BasicBlock *continue_target;

  std::unordered_set<Construction*> children;
};

class Structurizer {
private:
  const CFG& cfg_;
  const Function& function_;

  DominatorTree dtree_;
  DominatorTree pdtree_;

  std::unordered_set<const BasicBlock*> candidates_;
  std::unordered_set<const Construction*> constructions_;

public:
  Structurizer(const CFG& cfg, const Function& function) :
    cfg_(cfg), function_(function), dtree_(/* postdominator= */ false), pdtree_(/* postdominator= */ true) {
    dtree_.InitializeTree(cfg, &function);
    pdtree_.InitializeTree(cfg, &function);
  }

  void Structurize() {
    FindCandidates();
    for (auto block : candidates_) {
      std::cout << "candidate: " << block->id() << std::endl;
    }

    IdentifyConstructs();
    for (const auto& item : constructions_) {
      std::cout << (item->continue_target ? "loop:" : "condition:") << std::endl;
      std::cout << "    header:" << item->header->id() << std::endl;
      std::cout << "     merge:" << item->merge->id() << std::endl;
      if (item->continue_target) {
        std::cout << "  continue:" << item->continue_target->id() << std::endl;
      }
    }
  }

private:
  // Finds all blocks with an in-degree or out-degree of 2 or more.
  void FindCandidates() {
    assert(candidates_.size() == 0);
    for (const auto& block : function_) {
      if (cfg_.preds(block.id()).size() > 1 ||
          block.GetSuccessorCount() > 1) {
        candidates_.insert(&block);
      }
    }
  }

  // Given a list of basic blocks, returns the single block dominating all the others.
  // If the dominator tree candidates are siblings, returns the one post-dominating the other siblings.
  // If the candidates are sibling in both dom, and pdom trees, a random one is returned.
  const BasicBlock* GetNextTask() const {
    std::queue<const DominatorTreeNode*> to_visit;
    // This is a function CFG, we can have only 1 root.
    to_visit.push(dtree_.GetRoot());
    to_visit.push(nullptr);

    const BasicBlock* task = nullptr;

    while (to_visit.size() != 0) {
      const auto *node = to_visit.front();
      to_visit.pop();

      // Each null inserted marks the end of a siblings level.
      if (node == nullptr) {
        to_visit.push(nullptr);

        // If we found a candidate, we can return.
        if (task != nullptr)
          break;

        // Otherwise, let's continue onto the next level.
        continue;
      }

      // The node we traverse is a candidate.
      if (candidates_.count(node->bb_) != 0) {
        // This is a BFS. Either we already found the best node (and exited),
        // or the alternative is a sibling.

        // First case: no candidate found yet. Select this one.
        if (task == nullptr) {
          task = node->bb_;
        } else if (pdtree_.Dominates(node->bb_, task)) {
          // Or the alternative is a sibling. 2 Options:
          // - siblings are in 2 independent construct. Order doesn't matter.
          // - one sibling construct wraps the other. If it doesn't dominates, maybe it post-dominates?
          task = node->bb_;
        }
      }

      // Continue with the children.
      for (auto *child : node->children_)
        to_visit.push(child);
    }

    assert(task != nullptr);
    return task;
  }

  // Given a basic block `A`, returns all the blocks `B` dominated by `A` with an edge `B` -> `A`.
  std::unordered_set<const BasicBlock*> GetBackEdgeBlocks(const BasicBlock* block) const {
    std::unordered_set<const BasicBlock*> predecessors = cfg_.preds(block);
    std::unordered_set<const BasicBlock*> output;

    for (const auto* item : predecessors) {
      if (dtree_.Dominates(block, item))
        output.insert(item);
    }
    return output;
  }

  Construction* IdentifySelectionConstructFromMerge(const BasicBlock*) {
    assert(0 && "unimplemented");
    return nullptr;
  }

  Construction* IdentifySelectionConstructFromHeader(const BasicBlock*) {
    assert(0 && "unimplemented");
    return nullptr;
  }

  Construction* IdentifySelectionConstruct(const BasicBlock *block) {
    std::unordered_set<const BasicBlock*> predecessors = cfg_.preds(block);
    std::unordered_set<const BasicBlock*> successors = cfg_.successors(block);

    if (successors.size() == 1) {
      assert(predecessors.size() != 1);
      return IdentifySelectionConstructFromMerge(block);
    }

    if (predecessors.size() == 1) {
      assert(successors.size() != 1);
      return IdentifySelectionConstructFromHeader(block);
    }

    assert(0 && "Block is a merge and a header. Not handled.");
    return nullptr;
  }

  std::unordered_set<const BasicBlock*> GetLoopBlocks(const BasicBlock *header) const {
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
      for (auto p : cfg_.preds(item))
        to_process.push(p);
    }

    return output;
  }

  std::unordered_set<const BasicBlock*> FindExitNodes(const std::unordered_set<const BasicBlock*>& loop) const {
    std::unordered_set<const BasicBlock*> output;
    for (const auto node : loop) {
      node->ForEachSuccessorLabel([this, &loop, &output](const uint32_t id) {
        if (loop.count(cfg_.block(id)) == 0)
          output.insert(cfg_.block(id));
      });
    }

    return output;
  }

  const BasicBlock* FindMergeBlock(std::unordered_set<const BasicBlock*> branches) const {
    std::queue<const BasicBlock*> to_process;
    std::unordered_set<const BasicBlock *> visited;
    for (auto blk : branches)
      to_process.push(blk);

    while (to_process.size() != 0) {
      auto blk = to_process.front();
      to_process.pop();

      if (visited.count(blk) != 0)
        continue;
      visited.insert(blk);

      const bool immediate_pdom = std::all_of(branches.cbegin(), branches.cend(), [this, &blk](const BasicBlock* pred) {
            return pdtree_.Dominates(blk, pred);
      });

      if (immediate_pdom) {
        return blk;
      }

      blk->ForEachSuccessorLabel([this, &to_process](const uint32_t id) {
        to_process.push(cfg_.block(id));
      });
    }
    return nullptr;
  }

  Construction* IdentifyLoopConstruct(const BasicBlock *block) {
    std::unordered_set<const BasicBlock*> predecessors = cfg_.preds(block);
    std::unordered_set<const BasicBlock*> successors = cfg_.successors(block);
    std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(block);

    // Don't think we can have 2 back-edges. FIXME: figure this out.
    assert(back_edge_blocks.size() == 1);
    // A loop header cannot be a merge block. FIXME: create a pre-process to split those cases.
    assert(predecessors.size() == 2);

    std::unordered_set<const BasicBlock*> loop_blocks = GetLoopBlocks(block);
    std::unordered_set<const BasicBlock*> exit_blocks = FindExitNodes(loop_blocks);

    Construction *output = new Construction();
    output->continue_target = *back_edge_blocks.begin();
    output->header = block;
    output->merge = FindMergeBlock(exit_blocks);

    candidates_.erase(output->header);
    candidates_.erase(output->merge);
    candidates_.erase(output->continue_target);
    return output;
  }

  // Identifies selection/loop constructs in the function.
  void IdentifyConstructs() {
    while (candidates_.size() != 0) {
      const BasicBlock* item = GetNextTask();
      candidates_.erase(item);
      std::cout << "Figuring out construction for node " << item->id() << std::endl;

      std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(item);
      const bool is_loop_header = back_edge_blocks.size() != 0;

      Construction *construction = nullptr;
      if (is_loop_header)
        construction = IdentifyLoopConstruct(item);
      else {
        //construction = IdentifySelectionConstruct(item);
      }

      if (construction != nullptr)
        constructions_.insert(construction);
      //assert(construction != nullptr);
      // FIXME: figure out how to detect sibling/child construct, so I can pass the parent.
    }
  }
};


#if 0


    if (!isLoopHeader(dtree, item)) {
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
#endif

Pass::Status StructurizePass::Process() {
  const auto& cfg = *context()->cfg();
  bool modified = false;

  for (const auto& function : *context()->module()) {
    Structurizer structurizer(cfg, function);
    structurizer.Structurize();
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools

