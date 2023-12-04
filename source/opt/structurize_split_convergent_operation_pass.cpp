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

#include "source/opt/structurize_split_convergent_operation_pass.h"

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

namespace {

using BlockSet = std::unordered_set<const BasicBlock*>;

struct Internal {
  IRContext *context_;
  Function& function_;
  DominatorTree dtree_;

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
  }

  struct Task {
    const BasicBlock *block;
    BlockSet successors;
  };

  static bool IsConvergentInstruction(const Instruction& instruction) {
    return instruction.opcode() == spv::Op::OpConvergenceEntry
        || instruction.opcode() == spv::Op::OpConvergenceLoop
        || instruction.opcode() == spv::Op::OpConvergenceAnchor;
  }

  std::vector<Task> FindProblematicBlocks() {
    std::vector<Task> output;

    for (const BasicBlock& block : function_) {
      size_t convergence_operation_count = 0;
      for (const Instruction& instruction : block) {
        if (Internal::IsConvergentInstruction(instruction))
          ++convergence_operation_count;

        if (convergence_operation_count > 1) {
          output.push_back({ &block, context_->cfg()->successors(&block) });
          break;
        }
      }
    }

    return output;
  }

  void FixBlock(const Task& task) {
    BasicBlock *block = &*function_.FindBlock(task.block->id());

    while (true) {
      bool has_split = false;
      size_t convergence_instruction_count = 0;

      for (auto it = block->begin(); it != block->end(); ++it) {
        if (Internal::IsConvergentInstruction(*it)) {
          ++convergence_instruction_count;
        }

        if (convergence_instruction_count <= 1) {
          continue;
        }

        BasicBlock *new_block = block->SplitBasicBlock(context_, context_->TakeNextId(), it);
        InstructionBuilder builder(context_, block);
        builder.AddBranch(new_block->id());
        block = new_block;
        has_split = true;
        break;
      }

      if (!has_split)
        break;
    }
  }

  Pass::Status Process() {
    std::vector<Task> tasks = FindProblematicBlocks();
    if (tasks.size() == 0)
      return Pass::Status::SuccessWithoutChange;

    for (const Task& task : tasks)
      FixBlock(task);
    return Pass::Status::SuccessWithChange;
  }
};

} // anonymous namespace

Pass::Status StructurizeSplitConvergentOperationPass::Process() {
  bool modified = false;
  for (auto& function : *context()->module()) {
    Internal internal(context(), function);
    Pass::Status status = internal.Process();
    if (status == Status::SuccessWithChange)
      modified = true;
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
