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

#include "source/opt/structurize_identify_selection_with_merge_pass.h"

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <vector>

#include "source/opt/instruction.h"
#include "source/opt/loop_identify.h"
#include "source/opt/convergence_region.h"
#include "source/opt/ir_context.h"
#include "source/opt/ir_builder.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

namespace {

using BlockSet = analysis::LoopManager::BlockSet;
using EdgeSet = analysis::LoopManager::EdgeSet;
using Region = analysis::ConvergenceRegionManager::Region;
using RegionSet = std::unordered_set<const Region*>;

struct Internal {
  IRContext *context_;
  Function& function_;
  DominatorTree dtree_;

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
  }

#if 0
  const Region* GetRegionForNode(const BasicBlock *block) {
    const auto& regions = context_->get_convergence_region_mgr()->GetConvergenceRegions();

    std::queue<const Region*> to_process;
    for (const Region *r : regions)
      to_process.push(r);

    while (to_process.size() != 0) {
      const Region *item = to_process.front();
      to_process.pop();

      if (item->nodes.count(block) != 0)
        return item;
    }

    return nullptr;
  }

  const BasicBlock* GetMergeNode(const BasicBlock *block) {
    for (const Instruction& i : *block) {
      if (i.opcode() != spv::Op::OpSelectionMerge && i.opcode() != spv::Op::OpLoopMerge) {
        continue;
      }

      return context_->cfg()->block(i.GetSingleWordInOperand(0));
    }

    return nullptr;
  }
#endif

  struct Task {
    BasicBlock *target;
    const BasicBlock *merge;

    Task(BasicBlock *target, const BasicBlock *merge)
      : target(target), merge(merge) { }
  };

  BlockSet GetImmediateRegionBlocks(const Region *region) {
    BlockSet output = region->nodes;

    for (const auto& child : region->children) {
      for (const auto *block : child->nodes)
        output.erase(block);
    }

    return output;
  }

  const Region* GetRegionNode(const BasicBlock* node, const Region* region) {
    for (const Region *child : region->children) {
      if (child->nodes.count(node) != 0)
        return child;
    }
    return nullptr;
  }

  const BasicBlock* GetPreHeader(const Region *region) {
    const auto& predecessors = context_->cfg()->preds(region->entry);
    assert(predecessors.size() == 1);
    return *predecessors.begin();
  }

  // Returns the in-degree of a node, taking into account region-nodes.
  // a Region node is an abstraction of a subregion:
  //  - all the nodes in the subregions becomes 1, meaning 2 edges region->exit becomes a single exit.
  size_t GetDAGInDegree(const Region *region, const BasicBlock *block) {
    assert(region->nodes.count(block) == 1);

    // The nodes stricyly belonging to the region. Excluding subregion nodes.
    BlockSet immediate_region_nodes = GetImmediateRegionBlocks(region);

    size_t in_degree = 0;
    // Checking region-nodes.
    for (const Region *child : region->children) {
      assert(child->exits.size() == 1);
      if ((*child->exits.begin()) == block)
        ++in_degree;
    }

    // Now, other incoming edges.
    EdgeSet back_edges = context_->get_loop_mgr()->GetBackEdges(&function_);
    const auto& predecessors = context_->cfg()->preds(block);
    for (const BasicBlock* predecessor : predecessors) {
      // Excluding back-edges, since we work on the DAG version.
      if (back_edges.count({ predecessor, block }) != 0)
        continue;

      // predecessor outside of the current region, real edge.
      if (region->nodes.count(predecessor) == 0) {
        ++in_degree;
        continue;
      }

      // predecessor in the current immediate region, real edge.
      if (immediate_region_nodes.count(predecessor) != 0) {
        ++in_degree;
        continue;
      }
    }

    return in_degree;
  }

  Pass::Status Process() {
    EdgeSet back_edges = context_->get_loop_mgr()->GetBackEdges(&function_);
    const auto& regions = context_->get_convergence_region_mgr()->GetConvergenceRegions();
    auto dom_analysis = context_->GetDominatorAnalysis(&function_);

    std::vector<Task> tasks;
    std::queue<const Region *> to_process;
    for (const Region *child : regions)
      to_process.push(child);

    while (to_process.size() != 0) {
      const Region *item = to_process.front();
      to_process.pop();
      for (const auto& child : item->children) {
        to_process.push(child);
      }

      BlockSet nodes = GetImmediateRegionBlocks(item);
      BlockSet merge_blocks;

      for (const BasicBlock *block : nodes) {
        size_t in_degree = GetDAGInDegree(item, block);
        if (in_degree > 1)
          merge_blocks.insert(block);
      }

      for (const BasicBlock *block : merge_blocks) {
        const BasicBlock *h = dom_analysis->ImmediateDominator(block);
        assert(h);

        const Region *region_node = GetRegionNode(h, item);
        if (region_node != nullptr) {
          h = GetPreHeader(region_node);
        }

        BasicBlock *target = context_->cfg()->block(h->id());
        tasks.emplace_back(target, block);
      }
    }

    for (const auto& task : tasks) {
      std::cout << "adding OpSelectionMerge to " << task.target->id() << " with merge at " << task.merge->id() << std::endl;
      InstructionBuilder builder(context_, &*task.target->tail());
      builder.AddSelectionMerge(task.merge->id());
      context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    }

    return tasks.size() != 0 ? Pass::Status::SuccessWithChange : Pass::Status::SuccessWithoutChange;
  }
};

} // anonymous namespace

Pass::Status StructurizeIdentifySelectionWithMergePass::Process() {
  bool modified = false;
  for (auto& function : *context()->module()) {
    Internal internal(context(), function);
    Pass::Status status = internal.Process();
    if (status == Status::SuccessWithChange) {
      modified = true;
    }
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
