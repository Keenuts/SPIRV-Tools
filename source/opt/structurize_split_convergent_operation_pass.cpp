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

  std::vector<Task> FindProblematicBlocks() {
    std::vector<Task> output;

    for (const BasicBlock& block : function_) {
      size_t convergence_operation_count = 0;
      for (const Instruction& instruction : block) {
        if (instruction.opcode() == spv::Op::OpConvergenceEntry
            || instruction.opcode() == spv::Op::OpConvergenceLoop
            || instruction.opcode() == spv::Op::OpConvergenceAnchor) {
          ++convergence_operation_count;
        }

        if (convergence_operation_count > 1) {
          output.push_back({ &block, context_->cfg()->successors(&block) });
          break;
        }
      }
    }

    return output;
  }

  Pass::Status Process() {
    std::vector<Task> tasks = FindProblematicBlocks();

    (void)tasks;
    return Pass::Status::SuccessWithoutChange;
  }
};

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
