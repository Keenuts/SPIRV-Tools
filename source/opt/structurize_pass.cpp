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
#include "source/opt/ir_builder.h"
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

  std::vector<Construction*> children;
  Construction *parent;

  void dump(size_t indent_size = 0) const {
    std::string indent(indent_size * 4, ' ');

    std::cout << indent << "{" << std::endl;
    std::cout << indent << "  type: " << (continue_target ? "loop" : "condition") << std::endl;
    std::cout << indent << "  header:" << header->id() << std::endl;
    std::cout << indent << "  merge:" << merge->id() << std::endl;
    if (continue_target) {
      std::cout << indent << "  continue:" << continue_target->id() << std::endl;
    }

    std::cout << indent << "  children: {" << std::endl;
    for (const auto& child : children)
      child->dump(indent_size + 1);
    std::cout << indent << "  }" << std::endl;
    std::cout << indent << "}" << std::endl;
  }
};

class Structurizer {
private:
  const CFG& cfg_;
  const Function& function_;

  DominatorTree dtree_;
  DominatorTree pdtree_;

public:
  Structurizer(const CFG& cfg, const Function& function) :
    cfg_(cfg), function_(function), dtree_(/* postdominator= */ false), pdtree_(/* postdominator= */ true) {
    dtree_.InitializeTree(cfg, &function);
    pdtree_.InitializeTree(cfg, &function);
  }

  using BlockSet = std::unordered_set<const BasicBlock*>;

  Pass::Status Structurize(IRContext *context) {
    const BasicBlock *entry = &*function_.entry();
    BlockSet exits;
    for (const BasicBlock& block : function_)
      if (spvOpcodeIsReturn(block.ctail()->opcode()))
        exits.insert(&block);
    assert(exits.size() == 1);

    dtree_.DumpTreeAsDot(std::cout);
    pdtree_.DumpTreeAsDot(std::cout);

    std::vector<Construction*> constructions = FindChildConstructions(nullptr, entry, *exits.begin());
    for (const Construction* c : constructions)
      c->dump();

    return PatchConstructions(context, constructions);
  }

private:
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

  // Returns true if the BB |dst| is reachable from |src|.
  bool IsReachableFrom(const BasicBlock *src, const BasicBlock *dst) {
    std::unordered_set<const BasicBlock*> visited;
    std::queue<const BasicBlock*> to_visit;
    to_visit.push(src);

    while (to_visit.size() != 0) {
      const BasicBlock *item = to_visit.front();
      to_visit.pop();

      if (visited.count(item) != 0)
        continue;
      visited.insert(item);

      if (item == dst)
        return true;

      item->ForEachSuccessorLabel([this, &to_visit](const uint32_t id) {
          to_visit.push(cfg_.block(id));
      });
    }

    return false;
  }

  bool IsDAGOrdered(const BasicBlock *a, const BasicBlock *b, const BasicBlock *c) {
    return IsDAGOrdered(a, b) && IsDAGOrdered(b, c);
  }

  bool IsDAGOrdered(const BasicBlock *a, const BasicBlock *b) {
    std::queue<const BasicBlock*> to_visit;
    to_visit.push(a);
    BlockSet visited;

    while (to_visit.size() > 0) {
      const BasicBlock *item = to_visit.front();
      to_visit.pop();

      if (visited.count(item) != 0)
        continue;
      visited.insert(item);

      if (item == b)
        return true;

      for (const BasicBlock *child : cfg_.successors(item))
        to_visit.push(child);
    }

    return false;
  }

#if 0
  bool IsAfterDAGOrder(const BasicBlock *src, const BasicBlock *dst) {
    if (src == dst)
      return false;

    std::unordered_set<const BasicBlock*> visited;
    std::queue<const BasicBlock*> to_visit;
    to_visit.push(src);

    while (to_visit.size() != 0) {
      const BasicBlock *item = to_visit.front();
      to_visit.pop();

      if (visited.count(item) != 0)
        continue;
      visited.insert(item);

      if (dtree_.StrictlyDominates(item, src))
        continue;

      if (item == dst)
        return true;

      item->ForEachSuccessorLabel([this, &to_visit](const uint32_t id) {
          to_visit.push(cfg_.block(id));
      });
    }

    return false;
  }
#endif

  // Given a block belonging to a loop, returns all the blocks in the loop.
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

  // Given a loop, find all the basic block directly reachable from the loop which are not part of the loop.
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


  // Returns the first basic block strictly post-dominating both |lhs| and |rhs|, nullptr otherwise.
  const BasicBlock* FindImmediateCommonPostDominator(const BasicBlock *lhs, const BasicBlock *rhs) {
    //const BasicBlock *result = nullptr;
    //pdtree_.DumpTreeAsDot(std::cout);

    std::array<const BasicBlock*, 2> nodes = { lhs, rhs };
    std::array<std::vector<const BasicBlock*>, 2> ancestors;
    assert(nodes.size() == 2 && nodes.size() == ancestors.size());

    for (size_t i = 0; i < nodes.size(); i++) {
      DominatorTreeNode *node = pdtree_.GetTreeNode(nodes[i]);
      // Skip self.
      node = node->parent_;
      while (node != nullptr) {
        ancestors[i].push_back(node->bb_);
        node = node->parent_;
      }
    }

    const auto& lhs_ancestors = ancestors[0];
    const auto& rhs_ancestors = ancestors[1];
    for (size_t il = 0; il < lhs_ancestors.size(); il++) {
      for (size_t ir = 0; ir < rhs_ancestors.size(); ir++) {
        if (lhs_ancestors[il] == rhs_ancestors[ir])
          return lhs_ancestors[il];
      }
    }

    return nullptr;
  }

  // Visits all the successors of |block| in DAG order, but only if |block| dominates them.
  void VisitDominatedSuccessors(const BasicBlock *block, std::function<void(const BasicBlock*, size_t)> visit) {
    assert(block != nullptr);

    BlockSet visited;
    size_t depth = 0;
    std::queue<const BasicBlock*> to_visit;
    to_visit.push(block);
    to_visit.push(nullptr);

    while (to_visit.size() != 0) {
      const BasicBlock *item = to_visit.front();
      to_visit.pop();

      if (item == nullptr) {
        depth++;
        if (to_visit.size() != 0)
          to_visit.push(nullptr);
        continue;
      }

      if (visited.count(item) != 0)
        continue;
      visited.insert(item);

      for (const BasicBlock *child : cfg_.successors(item))
        to_visit.push(child);

      visit(item, depth);
    }
  }

  // Given 2 BBs |lhs| and |rhs|, find the first successor in DAG order reachable from both.
  // note: not an actual path finding, doesn't visit back-edges.
  const BasicBlock* FindClosestCommonSuccessor(const BasicBlock *lhs, const BasicBlock *rhs) {
    std::unordered_map<const BasicBlock*, size_t> depth_map;

    VisitDominatedSuccessors(lhs, [&depth_map](const BasicBlock *blk, size_t depth) {
        depth_map[blk] = depth;
    });

    const BasicBlock *best_block = nullptr;
    VisitDominatedSuccessors(rhs, [&depth_map, &best_block](const BasicBlock *blk, size_t depth) {
        if (depth_map.count(blk) == 0)
          return;

        if (best_block == nullptr || depth_map[best_block] > depth)
          best_block = blk;
    });

    return best_block;
  }

private:
  Pass::Status PatchConstructions(IRContext *context, const std::vector<Construction*>& constructions) {
    bool modified = false;
    std::queue<Construction*> to_patch;
    for (Construction *item : constructions)
      to_patch.push(item);

    while (to_patch.size() != 0) {
      const Construction *item = to_patch.front();
      to_patch.pop();

      for (Construction *child : item->children)
        to_patch.push(child);

      const bool is_loop = item->continue_target != nullptr;

      // Get the RW version of the basic block now.
      BasicBlock *header = cfg_.block(item->header->id());
      InstructionBuilder builder(context, &*header->tail());
      const spv::Op opcode = header->tail()->opcode();

      if (!is_loop) {
        std::cout << "patching node " << item->header->id() << std::endl;
        assert(opcode == spv::Op::OpBranchConditional);

        builder.AddSelectionMerge(item->merge->id());
        modified = true;
      } else {
        std::cout << "patching node " << item->header->id() << std::endl;
        assert(opcode == spv::Op::OpBranch || opcode == spv::Op::OpBranchConditional);

        builder.AddLoopMerge(item->merge->id(), item->continue_target->id());
        modified = true;
      }
    }

    return modified ? Pass::Status::SuccessWithChange : Pass::Status::SuccessWithoutChange;
  }

  void DumpSet(const std::string& title, const BlockSet& set) {
    std::cout << title << "{ ";
    for (const BasicBlock *block : set)
      std::cout << block->id() << ", ";
    std::cout << "}" << std::endl;
  }

  const Construction* GetInnermostLoop(const Construction *from) {
    while (from != nullptr) {
      if (from->continue_target != nullptr)
        break;
      from = from->parent;
    }
    return from;
  }

  bool IsConstructionHeaderBlock(const BasicBlock *block) {
      std::unordered_set<const BasicBlock*> predecessors = cfg_.preds(block);
      std::unordered_set<const BasicBlock*> successors = cfg_.successors(block);

      // Not a conditional branch nor a merge node.
      if (predecessors.size() <= 1 && successors.size() <= 1)
        return false;

      std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(block);
      const bool is_loop_header = back_edge_blocks.size() != 0;
      return is_loop_header || successors.size() > 1;
  }

  bool IsMergeBlock(const BasicBlock *block) {
      std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(block);
      std::unordered_set<const BasicBlock*> predecessors = cfg_.preds(block);
      return back_edge_blocks.size() == 0 && predecessors.size() > 1;
  }

  bool IsContinueBlock(const BasicBlock *block) {
      std::unordered_set<const BasicBlock*> successors = cfg_.successors(block);
      for (const BasicBlock *s : successors)
        if (dtree_.StrictlyDominates(s, block))
          return true;
      return false;
  }

  bool IsLoopHeader(const BasicBlock *block) {
      std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(block);
      return back_edge_blocks.size() == 1;
  }

  Construction* FindLoopConstruction(Construction *parent, const BasicBlock *, const BasicBlock *exit, const BasicBlock *header) {
    std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(header);
    assert(back_edge_blocks.size() == 1 && "Only 1 back-edge is allowed.");

    Construction *c = new Construction();
    c->parent = parent;
    c->header = header;
    c->continue_target = *back_edge_blocks.begin();

    const BasicBlock *merge = FindImmediateCommonPostDominator(c->header, c->continue_target);
    if (pdtree_.Dominates(exit, merge))
      c->merge = merge;
    else {
      assert(0 && "Figure this out.");
    }

    c->dump();
    c->children = FindChildConstructions(c, c->header, c->merge);
    return c;
  }

  Construction* FindSelectionConstruction(Construction *parent, const BasicBlock *, const BasicBlock *exit, const BasicBlock *header) {
    std::unordered_set<const BasicBlock*> back_edge_blocks = GetBackEdgeBlocks(header);
    std::unordered_set<const BasicBlock*> successors = cfg_.successors(header);
    assert(back_edge_blocks.size() == 0);
    assert(successors.size() == 2);

    Construction *c = new Construction();
    c->parent = parent;
    c->header = header;
    c->continue_target = nullptr;

    // Multiple cases for the branch:
    //  - both branches merge into a single node, the first common immediate pdom.
    //  - one branch early returns.
    //  - one branch breaks out of the innermost loop.
    //  - one branch continues the innermost loop.

    const BasicBlock *lhs = *successors.begin();
    const BasicBlock *rhs = *std::next(successors.begin());

    do {
      const Construction *innermost_loop = GetInnermostLoop(parent);
      const BasicBlock *exit_continue = innermost_loop ? innermost_loop->continue_target : nullptr;
      const BasicBlock *exit_merge = innermost_loop ? innermost_loop->merge : nullptr;

      // both branches merge into a single node, first immediate pdom, dominated by the parent construct.
      const BasicBlock *merge = FindClosestCommonSuccessor(lhs, rhs);
      //FindImmediateCommonPostDominator(lhs, rhs);
      if (merge != nullptr)
        std::cout << "  Found common pdom: " << merge->id() << std::endl;
      else
        std::cout << "  No common pdom." << std::endl;

      if (merge != nullptr && merge != exit_merge && merge != exit_continue && pdtree_.StrictlyDominates(exit, merge)) {
        std::cout << "  selecting " << merge->id() << " because obvious selection merge node.";
        c->merge = merge;
        break;
      }


      // In the normal case, the merge node strictly dominates the exit node. But if the function return
      // is the merge node, then the exit could also be this construction merge node.
      if (innermost_loop == nullptr) {
        // But I don't want an inner construct merge node to share the parent construct merge, hence
        // this is only allowed for the top-level constructs.
        assert(parent == nullptr);
        c->merge = merge;
        break;
      }

      assert(innermost_loop != nullptr);
      BlockSet loop_blocks = GetLoopBlocks(innermost_loop->header);
      std::cout << "loop: {";
      for (const BasicBlock *blk : loop_blocks)
        std::cout << blk->id() << ", ";
      std::cout << std::endl;

      // The first common pdom is outside or the parent construct, or there is none. This means
      // one branch returns, or merge/continues in the innermost loop.
      if (loop_blocks.count(lhs) == 0) {
        std::cout << "  selecting " << rhs->id() << " because " << lhs->id() << " exits the innermost loop.\n";
        c->merge = rhs;
      }
      else {
        std::cout << "  selecting " << lhs->id() << " because " << rhs->id() << " exits the innermost loop.\n";
        c->merge = lhs;
      }
    } while (false);

    assert(c->merge != nullptr);
    c->dump();
    c->children = FindChildConstructions(c, c->header, c->merge);
    return c;
  }

  std::vector<Construction*> FindChildConstructions(Construction *parent, const BasicBlock *entry, const BasicBlock *exit) {
    std::queue<const BasicBlock*> to_process;
    {
      const auto children = cfg_.successors(entry);
      for (const BasicBlock *child : children)
        to_process.push(child);
    }

    std::vector<Construction*> output;
    BlockSet visited;

    while (to_process.size() != 0) {
      const BasicBlock *item = to_process.front();
      to_process.pop();
      std::cout << "{ entry=" << entry->id() << ", exit=" << exit->id() << ", item=" << item->id() << "}" << std::endl;

      // Handle loops for visitor.
      if (visited.count(item) != 0) {
        std::cout << " - visited." << std::endl;
        continue;
      }
      visited.insert(item);

      // The reached block is outside of the parent construction. Ignoring.
      if (!IsDAGOrdered(entry, item, exit) || item == exit) {
      //if (!IsAfterDAGOrder(entry, item) || !IsAfterDAGOrder(item, exit)) {
        std::cout << " - not in range." << std::endl;
        continue;
      }

      if (IsContinueBlock(item))
        continue;

      const auto children = cfg_.successors(item);
      if (!IsConstructionHeaderBlock(item)) {
        for (const BasicBlock *child : children)
          to_process.push(child);
        continue;
      }

      std::cout << "Figuring out construction for node " << item->id() << std::endl;


      Construction *construction = nullptr;
      if (IsLoopHeader(item)) {
        construction = FindLoopConstruction(parent, entry, exit, item);
      } else {
        construction = FindSelectionConstruction(parent, entry, exit, item);
      }
      output.push_back(construction);
      to_process.push(construction->merge);
    }
    return output;
  }
};


Pass::Status StructurizePass::Process() {
  const auto& cfg = *context()->cfg();
  bool modified = false;

  for (const auto& function : *context()->module()) {
    Structurizer structurizer(cfg, function);
    Pass::Status status = structurizer.Structurize(context());
    if (status == Status::SuccessWithChange)
      modified = true;
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools

