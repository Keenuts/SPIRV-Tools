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

#ifndef SOURCE_OPT_STRUCTURIZE_SPLIT_HEADER_BLOCKS_PASS_H_
#define SOURCE_OPT_STRUCTURIZE_SPLIT_HEADER_BLOCKS_PASS_H_

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

// See optimizer.hpp for documentation.
class StructurizeSplitHeaderBlocksPass : public Pass {
 public:
  const char* name() const override { return "structurize-split-header-blocks"; }
  Status Process() override;
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_STRUCTURIZE_SPLIT_HEADER_BLOCKS_PASS_H_
