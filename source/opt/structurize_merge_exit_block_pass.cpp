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
    }
  }

  Pass::Status Process() {
    return Pass::Status::SuccessWithoutChange;
  }
};

} // anonymous namespace.


Pass::Status StructurizeMergeExitBlockPass::Process() {
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
