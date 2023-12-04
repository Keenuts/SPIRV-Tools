// Copyright (c) 2017 Google Inc.
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

#include <string>

#include "spirv-tools/libspirv.hpp"
#include "test/opt/pass_fixture.h"
#include "test/opt/pass_utils.h"

namespace spvtools {
namespace opt {
namespace {

using StructurizeSplitConvergentOperationPassTest = PassTest<::testing::Test>;

TEST_F(StructurizeSplitConvergentOperationPassTest, NoOp) {
  const std::string text =
      R"(
               OpCapability Shader
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %2 "main"
               OpExecutionMode %2 LocalSize 1 1 1
       %void = OpTypeVoid
         %14 = OpTypeFunction %void
       %bool = OpTypeBool
      %false = OpConstantFalse %bool
       %uint = OpTypeInt 32 0
     %uint_0 = OpConstant %uint 0
          %2 = OpFunction %void None %14
          %4 = OpLabel
          %5 = OpConvergenceEntry
; CHECK:  %5 = OpConvergenceEntry
               OpReturn
               OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeSplitConvergentOperationPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithoutChange);
}

TEST_F(StructurizeSplitConvergentOperationPassTest, LinearBlockSplit) {
  const std::string text =
      R"(
                        OpCapability Shader
                        OpMemoryModel Logical GLSL450
                        OpEntryPoint GLCompute %2 "main"
                        OpExecutionMode %2 LocalSize 1 1 1
                %void = OpTypeVoid
                  %14 = OpTypeFunction %void
                %bool = OpTypeBool
               %false = OpConstantFalse %bool
                %uint = OpTypeInt 32 0
              %uint_0 = OpConstant %uint 0
                   %2 = OpFunction %void None %14
                   %4 = OpLabel
                   %5 = OpConvergenceEntry
; CHECK:           %5 = OpConvergenceEntry
; CHECK-NEXT:           OpBranch [[tmp:%\w+]]
; CHECK-NEXT: [[tmp]] = OpLabel
                   %6 = OpConvergenceEntry
; CHECK-NEXT:      %6 = OpConvergenceEntry
                        OpReturn
                        OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeSplitConvergentOperationPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizeSplitConvergentOperationPassTest, MultiplePredecessors) {
  const std::string text =
      R"(
                        OpCapability Shader
                        OpMemoryModel Logical GLSL450
                        OpEntryPoint GLCompute %2 "main"
                        OpExecutionMode %2 LocalSize 1 1 1
                %void = OpTypeVoid
                  %14 = OpTypeFunction %void
                %bool = OpTypeBool
               %false = OpConstantFalse %bool
                %uint = OpTypeInt 32 0
              %uint_0 = OpConstant %uint 0
                   %2 = OpFunction %void None %14
                   %c = OpLabel
                        OpSelectionMerge %4 None
                        OpBranchConditional %false %8 %9
                   %8 = OpLabel
                        OpBranch %4
                   %9 = OpLabel
                        OpBranch %4
                   %4 = OpLabel
                   %5 = OpConvergenceEntry
; CHECK:           %5 = OpConvergenceEntry
; CHECK-NEXT:           OpBranch [[tmp:%\w+]]
; CHECK-NEXT: [[tmp]] = OpLabel
                   %6 = OpConvergenceEntry
; CHECK-NEXT:      %6 = OpConvergenceEntry
                        OpReturn
                        OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeSplitConvergentOperationPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizeSplitConvergentOperationPassTest, TokenInBranch) {
  const std::string text =
      R"(
                        OpCapability Shader
                        OpMemoryModel Logical GLSL450
                        OpEntryPoint GLCompute %2 "main"
                        OpExecutionMode %2 LocalSize 1 1 1
                %void = OpTypeVoid
                  %14 = OpTypeFunction %void
                %bool = OpTypeBool
               %false = OpConstantFalse %bool
                %uint = OpTypeInt 32 0
              %uint_0 = OpConstant %uint 0
                   %2 = OpFunction %void None %14
                   %c = OpLabel
                   %5 = OpConvergenceEntry
                   %6 = OpConvergenceEntry
                        OpSelectionMerge %4 None
                        OpBranchConditional %false %8 %9
; CHECK:           %5 = OpConvergenceEntry
; CHECK-NEXT:           OpBranch [[tmp:%\w+]]
; CHECK-NEXT: [[tmp]] = OpLabel
; CHECK-NEXT:      %6 = OpConvergenceEntry
; CHECK-NEXT:           OpSelectionMerge %4 None
; CHECK-NEXT:           OpBranchConditional %false %8 %9
                   %8 = OpLabel
                        OpBranch %4
                   %9 = OpLabel
                        OpBranch %4
                   %4 = OpLabel
                        OpReturn
                        OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeSplitConvergentOperationPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

}  // namespace
}  // namespace opt
}  // namespace spvtools
