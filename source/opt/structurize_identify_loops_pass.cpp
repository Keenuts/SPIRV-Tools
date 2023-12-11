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

#include "source/opt/structurize_identify_loops_pass.h"

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

struct Internal {
  IRContext *context_;
  Function& function_;
  DominatorTree dtree_;

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
  }

  struct Task {
    BasicBlock *target;
    const BasicBlock *merge_target;
    const BasicBlock *continue_target;

    Task(BasicBlock *target, const BasicBlock *merge_target, const BasicBlock *continue_target)
      : target(target), merge_target(merge_target), continue_target(continue_target) { }
  };

  bool HasBackEdge(const BasicBlock *block) {
    EdgeSet back_edges = context_->get_loop_mgr()->GetBackEdges(&function_);
    for (const auto& [src, dst] : back_edges) {
      if (dst == block)
        return true;
    }
    return false;
  }

  Pass::Status Process() {
    //EdgeSet back_edges = context_->get_loop_mgr()->GetCon(&function_);
    EdgeSet back_edges = context_->get_loop_mgr()->GetBackEdges(&function_);
    const auto& regions = context_->get_convergence_region_mgr()->GetConvergenceRegions(&function_);
    std::queue<const Region*> to_process;
    for (const Region *region : regions) {
      to_process.push(region);
    }

    std::vector<Task> tasks;

    while (to_process.size() != 0) {
      const Region *region = to_process.front();
      to_process.pop();
      for (const Region *child : region->children) {
        to_process.push(child);
      }

      std::cout << "Region: header: " << region->entry->id() << std::endl;
      if (!HasBackEdge(region->entry)) {
        // Not a loop, can skip.
        continue;
      }
      assert(region->exits.size() == 1);

      BasicBlock *block = context_->cfg()->block(region->entry->id());
      const BasicBlock *continue_target = nullptr;
      for (const auto& [src, dst] : back_edges) {
        if (dst == block) {
          continue_target = src;
          break;
        }
      }
      assert(continue_target != nullptr);

      tasks.emplace_back(block, *region->exits.begin(), continue_target);
    }

    for (const auto& task : tasks) {
      InstructionBuilder builder(context_, &*task.target->tail());
      builder.AddLoopMerge(task.merge_target->id(), task.continue_target->id());
      context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    }

    return tasks.size() != 0 ? Pass::Status::SuccessWithChange : Pass::Status::SuccessWithoutChange;
  }
};

} // anonymous namespace

Pass::Status StructurizeIdentifyLoopsPass::Process() {
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
