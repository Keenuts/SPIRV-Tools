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

#include "source/opt/structurize_pre_headers_pass.h"

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

  BlockSet FindLoopHeaders(const BasicBlock *root) {
    BlockSet output;
    BlockSet visited;
    std::queue<const BasicBlock*> to_process;

    to_process.push(root);
    while (to_process.size() != 0) {
      const BasicBlock *item = to_process.front();
      to_process.pop();

      if (visited.count(item) != 0) {
        continue;
      }
      visited.insert(item);

      BlockSet children = context_->cfg()->successors(item);
      for (const BasicBlock *child : children) {
        to_process.push(child);

        if (dtree_.Dominates(child, item))
          output.insert(child);
      }
    }

    return output;
  }

  BlockSet GetAncestors(const BasicBlock *block) {
    BlockSet predecessors = context_->cfg()->preds(block);
    BlockSet ancestors;
    for (const BasicBlock *p : predecessors) {
      if (dtree_.Dominates(block, p))
        continue;
      ancestors.insert(p);
    }
    return ancestors;
  }

  BlockSet GetHeadersRequiringPreHeader(const BlockSet& input) {
    BlockSet output;
    for (const BasicBlock *header : input) {
      BlockSet ancestors = GetAncestors(header);
      // Has multiple ancestors (not including back-edge): requires a pre-header.
      if (ancestors.size() != 1) {
        output.insert(header);
        continue;
      }

      BlockSet successors = context_->cfg()->successors(*ancestors.begin());
      if (successors.size() != 1) {
        output.insert(header);
        continue;
      }
    }

    return output;
  }

  static bool TailIsBranch(const BasicBlock *block) {
    return block->ctail()->opcode() == spv::Op::OpBranch;
  }

  static bool TailIsSwitch(const BasicBlock *block) {
    return block->ctail()->opcode() == spv::Op::OpSwitch;
  }

  void FixBranch(const BasicBlock *ro_block, const BasicBlock *target) {
    assert(ro_block->ctail()->opcode() == spv::Op::OpBranch);
    BasicBlock *block = context_->cfg()->block(ro_block->id());
    InstructionBuilder builder(context_, &*block->tail());
    builder.AddBranch(target->id());
    context_->KillInst(&*block->tail());
  }

  void FixSwitch(const BasicBlock *ro_block, const BasicBlock *old_target, const BasicBlock *target) {
    assert(ro_block->ctail()->opcode() == spv::Op::OpSwitch);
    BasicBlock *block = context_->cfg()->block(ro_block->id());
    Instruction *switch_instruction = &*block->tail();
    const uint32_t selector_id = switch_instruction->GetSingleWordInOperand(0);
    const uint32_t default_id = switch_instruction->GetSingleWordInOperand(1);

    std::vector<std::pair<Operand::OperandData, uint32_t>> targets;
    for (uint32_t i = 2; i < switch_instruction->NumInOperands(); i += 2) {
      const uint32_t target_id = switch_instruction->GetSingleWordInOperand(i + 1);
      targets.push_back({ switch_instruction->GetInOperand(i).words, (target_id == old_target->id() ? target->id() : target_id ) });
    }

    InstructionBuilder builder(context_, switch_instruction);
    builder.AddSwitch(selector_id, default_id, targets);
    context_->KillInst(switch_instruction);
  }

  void FixConditionalBranch(const BasicBlock *ro_block, const BasicBlock *old_target, const BasicBlock *target) {
    assert(ro_block->ctail()->opcode() == spv::Op::OpBranchConditional);
    BasicBlock *block = context_->cfg()->block(ro_block->id());
    Instruction *branch = &*block->tail();
    const uint32_t condition_id = branch->GetSingleWordInOperand(0);
    std::vector<uint32_t> targets = { branch->GetSingleWordInOperand(1), branch->GetSingleWordInOperand(2) };
    for (size_t i = 0; i < targets.size(); i++) {
      if (targets[i] == old_target->id())
        targets[i] = target->id();
    }

    InstructionBuilder builder(context_, branch);
    builder.AddConditionalBranch(condition_id, targets[0], targets[1]);
    context_->KillInst(branch);
  }

  void FixHeaders(const BlockSet& tasks) {
    struct FixData {
      BlockSet ancestors;
      const BasicBlock* header;
    };

    // We gather the tasks before modifying the module as this would cause the
    // CFG to be invalidated.
    std::vector<FixData> to_process;
    for (const BasicBlock *item : tasks) {
      FixData data;
      data.header = item;
      data.ancestors = GetAncestors(item);
      to_process.push_back(data);
    }

    // Now, we can patch the module.
    for (const auto& item : to_process) {

      const uint32_t new_block_id = context_->TakeNextId();
      std::unique_ptr<Instruction> label_inst(
          new Instruction(context_, spv::Op::OpLabel, 0, new_block_id, {}));
      std::unique_ptr<BasicBlock> bb(new BasicBlock(std::move(label_inst)));
      BasicBlock *new_block = bb.get();

      function_.AddBasicBlock(std::move(bb), function_.FindBlock(item.header->id()));

      InstructionBuilder builder(context_, new_block);
      builder.AddBranch(item.header->id());

      for (const BasicBlock *block : item.ancestors) {
        if (TailIsBranch(block)) {
          FixBranch(block, new_block);
        } else if (TailIsSwitch(block)) {
          FixSwitch(block, item.header, new_block);
        } else {
          FixConditionalBranch(block, item.header, new_block);
        }
      }
    }
  }

  Pass::Status Process() {
    const BasicBlock *entry = &*function_.entry();
    BlockSet headers = FindLoopHeaders(entry);
    BlockSet headers_to_fix = GetHeadersRequiringPreHeader(headers);

    if (headers_to_fix.size() == 0)
      return Pass::Status::SuccessWithoutChange;

    FixHeaders(headers_to_fix);
    return Pass::Status::SuccessWithChange;
  }
};

Pass::Status StructurizePreHeadersPass::Process() {
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
