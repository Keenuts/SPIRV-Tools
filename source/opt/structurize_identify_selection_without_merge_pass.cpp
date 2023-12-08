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

#include "source/opt/structurize_identify_selection_without_merge_pass.h"

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
    const BasicBlock *merge;

    Task(BasicBlock *target, const BasicBlock *merge)
      : target(target), merge(merge) { }
  };

  const BasicBlock* GetMergeBlock(const BasicBlock *block) {
    for (const Instruction& i : *block) {
      if (i.opcode() != spv::Op::OpSelectionMerge && i.opcode() != spv::Op::OpLoopMerge) {
        continue;
      }

      return context_->cfg()->block(i.GetSingleWordInOperand(0));
    }
    return nullptr;
  }

  bool IsSelectionHeader(const BasicBlock *block) {
    for (const Instruction& i : *block) {
      if (i.opcode() == spv::Op::OpSelectionMerge || i.opcode() == spv::Op::OpLoopMerge) {
        return true;
      }
    }
    return false;
  }

  Pass::Status Process() {
    BlockSet merge_blocks;
    for (const BasicBlock& block : function_) {
      if (!IsSelectionHeader(&block))
        continue;

      const BasicBlock *merge_block = GetMergeBlock(&block);
      assert(merge_block != nullptr);
      merge_blocks.insert(merge_block);
    }

    std::vector<Task> tasks;
    for (const BasicBlock& block : function_) {
      const auto& successors = context_->cfg()->successors(&block);
      if (successors.size() <= 1)
        continue;

      if (IsSelectionHeader(&block))
        continue;

      const BasicBlock *merge = nullptr;
      for (const BasicBlock *blk : successors) {
        if (merge_blocks.count(blk) != 0)
          continue;
        merge = blk;
        break;
      }
      assert(merge != nullptr);

      BasicBlock *header = context_->cfg()->block(block.id());
      tasks.emplace_back(header, merge);
    }

    for (const auto& task : tasks) {
      InstructionBuilder builder(context_, &*task.target->tail());
      builder.AddSelectionMerge(task.merge->id());
      context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    }

    return tasks.size() != 0 ? Pass::Status::SuccessWithChange : Pass::Status::SuccessWithoutChange;
  }
};

} // anonymous namespace

Pass::Status StructurizeIdentifySelectionWithoutMergePass::Process() {
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
