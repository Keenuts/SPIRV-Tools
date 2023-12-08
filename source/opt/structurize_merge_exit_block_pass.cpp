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

#include "source/opt/structurize_merge_exit_block_pass.h"

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

namespace {

using BlockSet = analysis::LoopManager::BlockSet;
using EdgeSet = analysis::LoopManager::EdgeSet;
using Region = analysis::ConvergenceRegionManager::Region;

struct Internal {
  IRContext *context_;
  Function& function_;
  DominatorTree dtree_;

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

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
  }

  Pass::Status Process() {
    bool modified = false;

    const auto& regions = context_->get_convergence_region_mgr()->GetConvergenceRegions();
    std::queue<const Region*> to_process;
    for (const Region *region : regions) {
      to_process.push(region);
    }

    while (to_process.size() != 0) {
      const Region *region = to_process.front();
      to_process.pop();

      for (const Region *child : region->children) {
        to_process.push(child);
      }

      if (region->exits.size() <= 1)
        continue;

      modified = true;
      EdgeSet exit_edges;
      for (const BasicBlock *exit : region->exits) {
        const BlockSet& predecessors = context_->cfg()->preds(exit);
        for (const BasicBlock *src : predecessors) {
          if (region->nodes.count(src) != 0) {
            exit_edges.insert({ src, exit });
          }
        }
      }

      assert(exit_edges.size() != 0);
      for (const auto& [src, dst] : exit_edges) {
        std::cout << "Exit edge: " << src->id() << " -> " << dst->id() << std::endl;
      }

      // FIXME: Good luck, this code is orrendous. But it works for simple exits. Need to generalize this
      // to use a swtich + phi node.
      assert(exit_edges.size() == 2 && "FIXME: add switch when the branch count is > than 2");

      analysis::Bool temp;
      analysis::TypeManager* type_mgr = context_->get_type_mgr();
      uint32_t bool_id = type_mgr->GetTypeInstruction(&temp);

      uint32_t const_false_id;
      uint32_t const_true_id;
      {
        analysis::ConstantManager* const_mgr = context_->get_constant_mgr();
        analysis::Bool* bool_type = type_mgr->GetType(bool_id)->AsBool();
        const analysis::Constant* false_const = const_mgr->GetConstant(bool_type, {false});
        const_false_id = const_mgr->GetDefiningInstruction(false_const)->result_id();
        const analysis::Constant* true_const = const_mgr->GetConstant(bool_type, {true});
        const_true_id = const_mgr->GetDefiningInstruction(true_const)->result_id();
      }

      (void) const_false_id;
      (void) const_true_id;

      const uint32_t new_block_id = context_->TakeNextId();
      std::unique_ptr<Instruction> label_inst(new Instruction(context_, spv::Op::OpLabel, 0, new_block_id, {}));
      std::unique_ptr<BasicBlock> bb(new BasicBlock(std::move(label_inst)));
      BasicBlock *new_block = bb.get();
      function_.AddBasicBlock(std::move(bb));

      InstructionBuilder builder(context_, new_block);

      std::vector<uint32_t> phi_operands;
      const BasicBlock *target_lhs = nullptr;
      const BasicBlock *target_rhs = nullptr;

      for (const auto& [src, dst] : exit_edges) {
        BasicBlock *block = context_->cfg()->block(src->id());
        if (TailIsBranch(src)) {
          FixBranch(block, new_block);
        } else if (TailIsSwitch(src)) {
          FixSwitch(block, dst, new_block);
        } else {
          FixConditionalBranch(block, dst, new_block);
        }

        bool is_lhs = phi_operands.size() == 0;
        if (is_lhs)
          target_lhs = dst;
        else
          target_rhs = dst;

        phi_operands.push_back(is_lhs ? const_true_id : const_false_id);
        phi_operands.push_back(src->id());
      }

      Instruction *phi = builder.AddPhi(bool_id, phi_operands);
      builder.AddConditionalBranch(phi->result_id(), target_lhs->id(), target_rhs->id());
    }

    return modified ? Pass::Status::SuccessWithChange : Pass::Status::SuccessWithoutChange;
  }
};

} // anonymous namespace.


Pass::Status StructurizeMergeExitBlockPass::Process() {
  bool modified = false;
  for (auto& function : *context()->module()) {
    Internal internal(context(), function);
    Pass::Status status = internal.Process();
    if (status == Status::SuccessWithChange) {
      modified = true;
    }
  }

  context()->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
  if (modified) {
    for (auto& function : *context()->module()) {
      function.ReorderBasicBlocksInStructuredOrder();
    }
  }
  context()->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
