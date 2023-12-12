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

//#define VERBOSE 1

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
using Region = ConvergenceRegionManager::Region;

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
    case spv::Op::OpConvergenceLoop:
      return instruction->result_id();
    default:
      break;
  }
  return instruction->GetSingleWordInOperand(0);
}

#ifdef VERBOSE
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

void DumpRegion(const ConvergenceRegionManager::Region *region, size_t indent_level = 0) {
  std::string indent(indent_level, '\t');
  std::cout << indent << "{" << std::endl;
  std::cout << indent << "\ttoken: " << region->token << std::endl;

  std::cout << indent << "\tnodes: { ";
  for (const BasicBlock *node : region->nodes)
    std::cout << node->id() << ", ";
  std::cout << "}" << std::endl;

  std::cout << indent << "\texits: { ";
  for (const BasicBlock *node : region->exits)
    std::cout << node->id() << ", ";
  std::cout << "}" << std::endl;

  std::cout << indent << "\tsubregions: { " << std::endl;
  for (const auto *child : region->children)
    DumpRegion(child, indent_level + 1);
  std::cout << indent << "\t}" << std::endl;
  std::cout << indent << " }" << std::endl;
}
#endif

} // anonymous namespace

BlockSet ConvergenceRegionManager::FindPathsToMatch(const EdgeSet& back_edges,
                                                    const BasicBlock *node,
                                                    std::function<bool(const BasicBlock*)> isMatch) const {
  BlockSet output;
  if (isMatch(node))
    output.insert(node);

  for (auto child : context_->cfg()->successors(node)) {
#if 0
    bool is_back_edge = false;
    for (const auto& pair : back_edges) {
      if (pair.first == node && pair.second == child) {
        is_back_edge = true;
        break;
      }
    }
    if (is_back_edge)
#else
    if (back_edges.count({ node, child }) != 0)
#endif
      continue;

    auto child_set = FindPathsToMatch(back_edges, child, isMatch);
    if (child_set.size() == 0)
      continue;

    output.insert(child_set.cbegin(), child_set.cend());
    output.insert(node);
  }

  return output;
}

namespace {

struct RegionInfo {
  uint32_t token;
  BlockSet nodes;
  BlockSet exits;
};

void CreateRegionHierarchy(Region *parent, const LoopInfo& loop, const std::unordered_map<const BasicBlock*, RegionInfo>& loop_to_region) {
  if (loop_to_region.count(loop.header) == 0 || loop_to_region.at(loop.header).nodes.size() == 0) {
    for (const auto& child : loop.children)
      CreateRegionHierarchy(parent, child, loop_to_region);
    return;
  }

  const RegionInfo& info = loop_to_region.at(loop.header);
  Region *region = new Region();
  region->parent = parent;
  region->entry = loop.header;
  region->nodes = info.nodes;
  region->token = info.token;
  region->exits = info.exits;
  parent->children.push_back(region);

  for (const auto& child : loop.children)
    CreateRegionHierarchy(region, child, loop_to_region);
}

void PurgeExitNodes(Region *region) {
  for (Region *child : region->children)
    PurgeExitNodes(child);


  std::vector<const BasicBlock*> to_purge;
  for (const BasicBlock *exit : region->exits) {
    for (const Region *child : region->children) {
      if (child->nodes.count(exit) != 0) {
        to_purge.push_back(exit);
      }
    }
  }

  for (const auto& block : to_purge) {
    region->exits.erase(block);
    region->nodes.insert(block);
  }

  return;
}

} // anonymous namesapce.


void ConvergenceRegionManager::IdentifyConvergenceRegions(const opt::Function& function) {
  auto back_edges = context_->get_loop_mgr()->GetBackEdges(&function);
  auto loops = context_->get_loop_mgr()->GetLoops(&function);
#ifdef VERBOSE
  std::cout << "Loops:" << std::endl;
  for (auto loop : loops)
    DumpLoop(loop);
  for (const auto& [start, end] : back_edges)
    std::cout << "back-edge: " << start->id() << " -> " << end->id() << std::endl;
#endif

  std::unordered_map<const BasicBlock*, RegionInfo> loop_to_region;
  std::queue<LoopInfo> to_process;
  for (const auto& loop : loops)
    to_process.push(loop);
  while (to_process.size() != 0) {
    const LoopInfo loop = to_process.front();
    to_process.pop();
    for (const auto& child : loop.children)
      to_process.push(child);
    auto convergence_instruction = GetConvergenceInstruction(loop.header);
    if (convergence_instruction == nullptr) {
      continue;
    }

    auto token = GetConvergenceToken(convergence_instruction);
    RegionInfo info;
    info.token = token;
    info.nodes = BlockSet(loop.nodes.cbegin(), loop.nodes.cend());

    for (auto exit : loop.exits) {
      auto other_region_nodes = FindPathsToMatch(back_edges, exit, [&token](const BasicBlock *block) {
          auto instruction = GetConvergenceInstruction(block);
          if (instruction == nullptr)
            return false;
          return GetConvergenceToken(instruction) == token; });

      if (other_region_nodes.size() == 0) {
          info.exits.insert(exit);
          continue;
      }

      for (const BasicBlock *node : other_region_nodes) {
        info.nodes.insert(node);
        for (const BasicBlock *child : context_->cfg()->successors(node)) {
          if (other_region_nodes.count(child) == 0)
            info.exits.insert(child);
        }
      }
    }

    loop_to_region.emplace(loop.header, std::move(info));
  }

  //Region fake_region;
  Region *top_level_region = new Region();
  top_level_region->parent = nullptr;
  top_level_region->token = 0;
  top_level_region->entry = function.entry().get();
  top_level_region->exits = function.GetReturnBlocks();

  for (const auto& loop : loops)
    CreateRegionHierarchy(top_level_region, loop, loop_to_region);
  for (Region *region : top_level_region->children)
    PurgeExitNodes(region);

  for (const BasicBlock& block : function)
    top_level_region->nodes.insert(&block);

  assert(top_level_regions_.count(&function) == 0);
  top_level_regions_.insert({ &function, top_level_region });
}

ConvergenceRegionManager::ConvergenceRegionManager(IRContext* context)
  : context_(context), dtree_(/* postdominator= */ false)  {
  for (const opt::Function& function : *context->module()) {
    dtree_.InitializeTree(*context_->cfg(), &function);
    IdentifyConvergenceRegions(function);

#ifdef VERBOSE
    std::cout << "Regions:" << std::endl;
    DumpRegion(top_level_regions_[&function]);
#endif
  }
}

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools
