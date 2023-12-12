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

#include "source/opt/structurize_merge_back_edge_pass.h"

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <vector>

#include "source/opt/instruction.h"
#include "source/opt/structurize_helpers.h"
#include "source/opt/loop_identify.h"
#include "source/opt/ir_context.h"
#include "source/opt/ir_builder.h"
#include "source/util/string_utils.h"

namespace spvtools {
namespace opt {

namespace {

using BlockSet = analysis::LoopManager::BlockSet;
using EdgeSet = analysis::LoopManager::EdgeSet;

struct Internal {
  IRContext *context_;
  Function& function_;
  DominatorTree dtree_;

  Internal(IRContext *context, Function& function) : context_(context), function_(function), dtree_(/* postdominator= */ false) {
    context_->InvalidateAnalysesExceptFor(IRContext::Analysis::kAnalysisNone);
    dtree_.InitializeTree(*context_->cfg(), &function);
  }

  Pass::Status Process() {
    std::unordered_map<const BasicBlock *, BlockSet> to_fix;

    EdgeSet back_edges = context_->get_loop_mgr()->GetBackEdges(&function_);
    for (auto& edge_lhs : back_edges) {
      for (auto& edge_rhs : back_edges) {
        if (edge_lhs == edge_rhs)
          continue;

        if (edge_lhs.second != edge_rhs.second)
          continue;

        if (to_fix.count(edge_lhs.second) == 0) {
          BlockSet empty;
          const BasicBlock *dst = edge_lhs.second;
          to_fix.insert_or_assign(dst, empty);
        }
        to_fix[edge_lhs.second].insert(edge_lhs.first);
        to_fix[edge_lhs.second].insert(edge_rhs.first);
      }
    }

    for (auto& [dst, srcs] : to_fix) {
      std::cout << dst->id() << " requires fixing." << std::endl;
      for (auto src : srcs) {
        std::cout << "- " << src->id() << std::endl;
      }

      const uint32_t new_block_id = context_->TakeNextId();
      std::unique_ptr<Instruction> label_inst(new Instruction(context_, spv::Op::OpLabel, 0, new_block_id, {}));
      std::unique_ptr<BasicBlock> bb(new BasicBlock(std::move(label_inst)));
      BasicBlock *new_block = bb.get();
      function_.AddBasicBlock(std::move(bb));

      InstructionBuilder builder(context_, new_block);
      builder.AddBranch(dst->id());

      for (auto src : srcs) {
        BasicBlock *block = context_->cfg()->block(src->id());
        if (TailIsBranch(src)) {
          FixBranch(context_, block, new_block);
        } else if (TailIsSwitch(src)) {
          FixSwitch(context_, block, dst, new_block);
        } else {
          FixConditionalBranch(context_, block, dst, new_block);
        }
      }
    }

    return to_fix.size() == 0 ? Pass::Status::SuccessWithoutChange : Pass::Status::SuccessWithChange;
  }
};

} // anonymous namespace

Pass::Status StructurizeMergeBackEdgePass::Process() {
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
