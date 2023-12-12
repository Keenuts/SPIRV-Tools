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

#include "source/opt/structurize_helpers.h"

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <vector>

#include "source/opt/instruction.h"
#include "source/opt/ir_context.h"
#include "source/opt/convergence_region.h"
#include "source/opt/ir_builder.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

bool TailIsBranch(const BasicBlock *block) {
  return block->ctail()->opcode() == spv::Op::OpBranch;
}

bool TailIsSwitch(const BasicBlock *block) {
  return block->ctail()->opcode() == spv::Op::OpSwitch;
}

void FixSwitch(IRContext *context, BasicBlock *block, const BasicBlock *old_target, const BasicBlock *new_target) {
    assert(block->ctail()->opcode() == spv::Op::OpSwitch);

    Instruction *switch_instruction = &*block->tail();
    const uint32_t selector_id = switch_instruction->GetSingleWordInOperand(0);
    const uint32_t default_id = switch_instruction->GetSingleWordInOperand(1);

    std::vector<std::pair<Operand::OperandData, uint32_t>> targets;
    for (uint32_t i = 2; i < switch_instruction->NumInOperands(); i += 2) {
      const uint32_t target_id = switch_instruction->GetSingleWordInOperand(i + 1);
      targets.push_back({ switch_instruction->GetInOperand(i).words, (target_id == old_target->id() ? new_target->id() : target_id ) });
    }

    InstructionBuilder builder(context, switch_instruction);
    builder.AddSwitch(selector_id, default_id, targets);
    context->KillInst(switch_instruction);
}

void FixBranch(IRContext *context, BasicBlock *block, const BasicBlock *target) {
  assert(block->ctail()->opcode() == spv::Op::OpBranch);
  InstructionBuilder builder(context, &*block->tail());
  builder.AddBranch(target->id());
  context->KillInst(&*block->tail());
}

void FixConditionalBranch(IRContext *context, BasicBlock *block, const BasicBlock *old_target, const BasicBlock *target) {
  assert(block->ctail()->opcode() == spv::Op::OpBranchConditional);
  Instruction *branch = &*block->tail();
  const uint32_t condition_id = branch->GetSingleWordInOperand(0);
  std::vector<uint32_t> targets = { branch->GetSingleWordInOperand(1), branch->GetSingleWordInOperand(2) };
  for (size_t i = 0; i < targets.size(); i++) {
    if (targets[i] == old_target->id())
      targets[i] = target->id();
  }

  InstructionBuilder builder(context, branch);
  builder.AddConditionalBranch(condition_id, targets[0], targets[1]);
  context->KillInst(branch);
}

}  // namespace opt
}  // namespace spvtools
