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

#include "source/opt/strip_headers_pass.h"

#include <vector>
#include <unordered_set>

#include "source/opt/instruction.h"
#include "source/opt/ir_context.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

Pass::Status StripHeadersPass::StripInstructions() {
  std::vector<Instruction*> to_remove;

  const std::unordered_set<spv::Op> unstripped_opcodes = {
    spv::Op::OpBranch,
    spv::Op::OpBranchConditional,
    spv::Op::OpEntryPoint,
    spv::Op::OpExecutionMode,
    spv::Op::OpFunction,
    spv::Op::OpFunctionEnd,
    spv::Op::OpLabel,
    spv::Op::OpMemoryModel,
    spv::Op::OpReturn,
    spv::Op::OpSwitch,
    spv::Op::OpTypeFunction,
    spv::Op::OpTypeVoid,
  };

  context()->module()->ForEachInst(
#if 1
      [&to_remove, &unstripped_opcodes](Instruction* inst) {
      if (inst->opcode() == spv::Op::OpCapability &&
          inst->GetSingleWordInOperand(0) == static_cast<uint32_t>(spv::Capability::Shader)) {
          return;
      }

      if (unstripped_opcodes.count(inst->opcode()) != 0) {
        return;
      }

      to_remove.push_back(inst);
#else
      [&to_remove](Instruction* inst) {
      if (inst->opcode() == spv::Op::OpSelectionMerge ||
          inst->opcode() == spv::Op::OpLoopMerge) {
        to_remove.push_back(inst);
      }
#endif
    }, true);

  for (auto* inst : to_remove) {
    context()->KillInst(inst);
  }

  return to_remove.size() != 0 ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

uint32_t StripHeadersPass::GetFalseId() {
  auto* module = context()->module();
  uint32_t falseId = context()->module()->GetGlobalValue(spv::Op::OpConstantFalse);
  if (falseId != 0)
    return falseId;

  uint32_t boolId = module->GetGlobalValue(spv::Op::OpTypeBool);
  if (boolId == 0) {
    boolId = context()->TakeNextId();
    get_module()->AddGlobalValue(spv::Op::OpTypeBool, boolId, 0);
  }

  falseId = context()->TakeNextId();
  module->AddGlobalValue(spv::Op::OpConstantFalse, falseId, boolId);
  return falseId;
}

Pass::Status StripHeadersPass::PatchConditions(uint32_t conditionId) {
  bool modified = false;
  context()->module()->ForEachInst(
      [&modified, conditionId](Instruction* inst) {
        if (inst->opcode() != spv::Op::OpBranchConditional)
          return;
        inst->GetInOperand(0).words[0] = conditionId;
        modified = true;
      });

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

Pass::Status StripHeadersPass::PatchSwitch(uint32_t valueId) {
  bool modified = false;
  context()->module()->ForEachInst(
      [&modified, valueId](Instruction* inst) {
        if (inst->opcode() != spv::Op::OpSwitch)
          return;
        inst->GetInOperand(0).words[0] = valueId;
        modified = true;
      });

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

Pass::Status StripHeadersPass::Process() {
  bool modified = StripInstructions() == Status::SuccessWithChange;

  const uint32_t falseId = GetFalseId();
  const uint32_t zeroId = context()->get_constant_mgr()->GetUIntConstId(0);
  assert(falseId != 0);
  assert(zeroId != 0);

  if (PatchConditions(falseId) == Status::SuccessWithChange)
    modified = true;
  if (PatchSwitch(zeroId) == Status::SuccessWithChange)
    modified = true;

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
