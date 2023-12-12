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

#include "source/opt/strip_convergence_operations_pass.h"

#include <vector>
#include <unordered_set>

#include "source/opt/instruction.h"
#include "source/opt/ir_context.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

Pass::Status StripConvergenceOperationsPass::Process() {
  std::vector<Instruction*> to_remove;
  context()->module()->ForEachInst([&to_remove](Instruction* inst) {
    if (inst->opcode() == spv::Op::OpConvergenceEntry
     || inst->opcode() == spv::Op::OpConvergenceLoop
     || inst->opcode() == spv::Op::OpConvergenceAnchor
     || inst->opcode() == spv::Op::OpConvergenceControl) {
      to_remove.push_back(inst);
    }
  }, true);

  for (auto* inst : to_remove) {
    context()->KillInst(inst);
  }

  return to_remove.size() != 0 ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools

