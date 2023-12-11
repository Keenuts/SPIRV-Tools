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

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
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

  void DumpDot() {
    std::cout << "digraph {" << std::endl;
    for (const BasicBlock& block : function_) {
      const auto& successors = context_->cfg()->successors(&block);
      for (const BasicBlock* successor : successors)
        std::cout << "    " << block.id() << " -> " << successor->id() << std::endl;
    }
    std::cout << "}" << std::endl;
  }

  struct SourceInfo {
    const BasicBlock *source;
    uint32_t value_id;
    uint32_t immediate_value;
    const BasicBlock *destination;
  };

  const BasicBlock* CreateExitNode(const std::vector<SourceInfo>& info_list) {
    assert(info_list.size() >= 2);
    BasicBlock *new_block = nullptr;
    {
      const uint32_t new_block_id = context_->TakeNextId();
      std::unique_ptr<Instruction> label_inst(new Instruction(context_, spv::Op::OpLabel, 0, new_block_id, {}));
      std::unique_ptr<BasicBlock> bb(new BasicBlock(std::move(label_inst)));
      new_block = bb.get();
      function_.AddBasicBlock(std::move(bb));
    }

    std::vector<std::pair<Operand::OperandData, uint32_t>> targets;
    std::vector<uint32_t> phi_operands;
    bool first = true;
    for (const auto& info : info_list) {
      BasicBlock *block = context_->cfg()->block(info.source->id());
      if (TailIsBranch(info.source)) {
        FixBranch(block, new_block);
      } else if (TailIsSwitch(info.source)) {
        FixSwitch(block, info.destination, new_block);
      } else {
        FixConditionalBranch(block, info.destination, new_block);
      }

      phi_operands.push_back(info.value_id);
      phi_operands.push_back(info.source->id());

      if (first) {
        first = false;
        continue;
      }

      Operand::OperandData operand_data = { info.immediate_value };
      targets.emplace_back(operand_data, info.destination->id());
    }

    analysis::Integer temp(32, false);
    analysis::TypeManager* type_mgr = context_->get_type_mgr();
    const uint32_t type_id = type_mgr->GetTypeInstruction(&temp);

    InstructionBuilder builder(context_, new_block);
    Instruction *phi = builder.AddPhi(type_id, phi_operands);

    builder.AddSwitch(phi->result_id(), info_list[0].destination->id(), targets);
    return new_block;
  }

  const BasicBlock* CreateExitBlock(const EdgeSet& exits) {
    analysis::ConstantManager* const_mgr = context_->get_constant_mgr();
    analysis::Integer temp(32, false);
    analysis::TypeManager* type_mgr = context_->get_type_mgr();
    const uint32_t type_id = type_mgr->GetTypeInstruction(&temp);
    analysis::Integer* int_type = type_mgr->GetType(type_id)->AsInteger();


    std::vector sorted_exits(exits.cbegin(), exits.cend());
    // This sort is only to help write tests. Operands order comes from a hashmap which order depends
    // on the pointer addresses. This means each new run might re-order operands. This makes writing
    // CHECK tests a bit hard. Ordering them using something we can easily predict.
    std::sort(sorted_exits.begin(), sorted_exits.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.first->id() < rhs.first->id();
    });


    std::vector<SourceInfo> infos;
    uint32_t index = 0;
    for (const auto& [src, dst] : sorted_exits) {
      SourceInfo info;
      info.source = src;
      info.destination = dst;
      info.immediate_value = index;
      info.value_id = const_mgr->GetDefiningInstruction(const_mgr->GetConstant(int_type, { index }))->result_id();
      infos.emplace_back(std::move(info));
      ++index;
    }

    return CreateExitNode(infos);
  }

  void FixPhiNodes(BasicBlock *subject, const BasicBlock *old_src, const BasicBlock *new_src) {
    for (auto& instruction : *subject) {
      if (instruction.opcode() != spv::Op::OpPhi)
        continue;

      for (uint32_t i = 0; i < instruction.NumInOperands(); i += 2) {
        auto& operand = instruction.GetInOperand(i + 1);
        if (operand.AsId() != old_src->id()) {
          continue;
        }
        operand.words[0] = new_src->id();
      }
    }
  }

  bool ProcessPass() {
    const auto& regions = context_->get_convergence_region_mgr()->GetConvergenceRegions(&function_);
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

      std::unordered_map<const BasicBlock*, BasicBlock*> const_to_rw;
      EdgeSet exit_edges;
      for (const BasicBlock *exit : region->exits) {
        const_to_rw.insert_or_assign(exit, context_->cfg()->block(exit->id()));

        const BlockSet& predecessors = context_->cfg()->preds(exit);
        for (const BasicBlock *src : predecessors) {
          const_to_rw.insert_or_assign(src, context_->cfg()->block(src->id()));
          if (region->nodes.count(src) != 0) {
            exit_edges.insert({ src, exit });
          }
        }
      }

      if (exit_edges.size() == 1)
        continue;

      // Create the new exit block for this loop.
      const BasicBlock* new_block = CreateExitBlock(exit_edges);

      for (const auto& [src, dst] : exit_edges) {
        FixPhiNodes(const_to_rw[dst], src, new_block);
      }
      context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
      return true;
    }
    return false;
  }

  Pass::Status Process() {
    bool modified = false;
    while (ProcessPass()) {
      //DumpDot();
      modified = true;
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
      context()->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
      function.ReorderBasicBlocksInStructuredOrder();
      context()->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    }
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
