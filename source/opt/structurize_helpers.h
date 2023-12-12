// Copyright (c) 2023 Google Inc.
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

#ifndef SOURCE_OPT_STRUCTURIZE_HELPERS_H_
#define SOURCE_OPT_STRUCTURIZE_HELPERS_H_

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "source/enum_set.h"
#include "source/extensions.h"
#include "source/opt/ir_context.h"
#include "source/opt/module.h"
#include "source/opt/pass.h"
#include "source/spirv_target_env.h"

namespace spvtools {
namespace opt {

void FixSwitch(IRContext *context, BasicBlock *block, const BasicBlock *old_target, const BasicBlock *new_target);
void FixBranch(IRContext *context, BasicBlock *block, const BasicBlock *target);
void FixConditionalBranch(IRContext *context, BasicBlock *block, const BasicBlock *old_target, const BasicBlock *target);
bool TailIsBranch(const BasicBlock *block);
bool TailIsSwitch(const BasicBlock *block);


}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_STRUCTURIZE_HELPERS_H_

