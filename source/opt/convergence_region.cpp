// Copyright (c) 2022 The Khronos Group Inc.
// Copyright (c) 2022 LunarG Inc.
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

#include <queue>
#include "source/opt/convergence_region.h"

#include "source/opt/ir_context.h"
#include "source/opt/loop_identify.h"

namespace spvtools {
namespace opt {
namespace analysis {

namespace {
using LoopInfo = LoopManager::LoopInfo;
using LoopVector = LoopManager::LoopVector;
using BlockSet = LoopManager::BlockSet;
using EdgeSet = LoopManager::EdgeSet;

bool IsConvergentInstruction(const Instruction& instruction) {
  return instruction.opcode() == spv::Op::OpConvergenceEntry
      || instruction.opcode() == spv::Op::OpConvergenceLoop
      || instruction.opcode() == spv::Op::OpConvergenceAnchor
      || instruction.opcode() == spv::Op::OpConvergenceControl;
}

const Instruction* GetConvergenceInstruction(const BasicBlock *block) {
  for (const Instruction& instruction : *block) {
    if (IsConvergentInstruction(instruction))
      return &instruction;
  }
  return nullptr;
}

uint32_t GetConvergenceToken(const Instruction *instruction) {
  assert(IsConvergentInstruction(*instruction));

  switch (instruction->opcode()) {
    case spv::Op::OpConvergenceEntry:
    case spv::Op::OpConvergenceAnchor:
      return instruction->result_id();
    default:
      break;
  }
  return instruction->GetSingleWordInOperand(0);
}

void DumpLoop(const LoopInfo& loop, size_t indent_level = 0) {
  std::string indent(indent_level, '\t');
  std::cout << indent << "{" << std::endl;
  std::cout << indent << "\theader: " << loop.header->id() << std::endl;

  std::cout << indent << "\texits: { ";
  for (const BasicBlock *exit : loop.exits)
    std::cout << exit->id() << ", ";
  std::cout << " }" << std::endl;

  std::cout << indent << "\tnodes: { ";
  for (const BasicBlock *node : loop.nodes)
    std::cout << node->id() << ", ";
  std::cout << " }" << std::endl;

  for (const auto& child : loop.children)
    DumpLoop(child, indent_level + 1);
  std::cout << indent << "}" << std::endl;
}

} // anonymous namespace

BlockSet ConvergenceRegionManager::FindPathsToMatch(const EdgeSet& back_edges,
                                                    const BasicBlock *node,
                                                    std::function<bool(const BasicBlock*)> isMatch) const {
  BlockSet output;
  if (isMatch(node))
    output.insert(node);

  for (auto child : context_->cfg()->successors(node)) {
    if (back_edges.count({ node, child }) != 0)
      continue;

    auto child_set = FindPathsToMatch(back_edges, child, isMatch);
    if (child_set.size() == 0)
      continue;

    output.insert(child_set.cbegin(), child_set.cend());
    output.insert(node);
  }

  return output;
}

void ConvergenceRegionManager::IdentifyConvergenceRegions(const opt::Function& function) {
  auto back_edges = context_->get_loop_mgr()->GetBackEdges(&function);
  auto loops = context_->get_loop_mgr()->GetLoops(&function);
  for (auto loop : loops)
    DumpLoop(loop);
  for (const auto& [start, end] : back_edges)
    std::cout << "back-edge: " << start->id() << " -> " << end->id() << std::endl;

  std::queue<LoopInfo> to_process;
  for (const auto& loop : loops)
    to_process.push(loop);

  while (to_process.size() != 0) {
    const LoopInfo loop = to_process.front();
    to_process.pop();

    for (const auto& child : loop.children)
      to_process.push(child);

    auto convergence_instruction = GetConvergenceInstruction(loop.header);
    if (convergence_instruction == nullptr)
      continue;

    auto token = GetConvergenceToken(convergence_instruction);

    for (const BasicBlock *node : loop.nodes)
      block_to_token_.insert_or_assign(node, token);

    for (auto exit : loop.exits) {
      auto nodes = FindPathsToMatch(back_edges, exit, [&token](const BasicBlock *block) {
          auto instruction = GetConvergenceInstruction(block);
          if (instruction == nullptr)
            return false;
          return GetConvergenceToken(instruction) == token; });
      for (const BasicBlock *node : nodes)
        block_to_token_.insert_or_assign(node, token);
    }
  }
}

ConvergenceRegionManager::ConvergenceRegionManager(IRContext* context)
  : context_(context), dtree_(/* postdominator= */ false)  {
  for (const opt::Function& function : *context->module()) {
    dtree_.InitializeTree(*context_->cfg(), &function);
    IdentifyConvergenceRegions(function);

    for (const auto& [block, token] : block_to_token_) {
      std::cout << " - block " << block->id() << ", token=" << token << std::endl;
    }
  }
}

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools