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

using StructurizePreHeadersPassTest = PassTest<::testing::Test>;

TEST_F(StructurizePreHeadersPassTest, NoLoop) {
  const std::string text =
      R"(
               OpCapability Shader
; CHECK:       OpCapability Shader
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
               OpBranch %5
          %5 = OpLabel
               OpBranchConditional %false %6 %7
          %6 = OpLabel
               OpBranch %8
          %7 = OpLabel
               OpBranch %8
          %8 = OpLabel
               OpReturn
               OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizePreHeadersPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithoutChange);
}

TEST_F(StructurizePreHeadersPassTest, EntryLoop) {
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
; CHECK:  [[entry:%\w+]] = OpLabel
; CHECK:                   OpBranch %4
                      %4 = OpLabel
                           OpBranchConditional %false %6 %7
                      %6 = OpLabel
                           OpBranch %4
                      %7 = OpLabel
                           OpReturn
                           OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizePreHeadersPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizePreHeadersPassTest, LoopInBranch) {
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
                      %5 = OpLabel
                           OpBranchConditional %false %4 %8
; CHECK:                   OpBranchConditional %false [[entry:%\w+]] %8
; CHECK:       [[entry]] = OpLabel
; CHECK:                   OpBranch %4
                      %4 = OpLabel
                           OpBranchConditional %false %6 %7
                      %6 = OpLabel
                           OpBranch %4
                      %7 = OpLabel
                           OpReturn
                      %8 = OpLabel
                           OpReturn
                           OpFunctionEnd)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizePreHeadersPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizePreHeadersPassTest, LoopInSwitchCase) {
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
                         %5 = OpLabel
                              OpSwitch %uint_0 %8 1 %4 2 %8 3 %4
; CHECK-DAG:                  OpSwitch %uint_0 %8 1 [[entry:%\w+]] 2 %8
; CHECK-SAME:                                                           3 [[entry]]
; CHECK:          [[entry]] = OpLabel
; CHECK:                      OpBranch %4
                         %4 = OpLabel
                              OpBranchConditional %false %6 %7
                         %6 = OpLabel
                              OpBranch %4
                         %7 = OpLabel
                              OpReturn
                         %8 = OpLabel
                              OpReturn
                              OpFunctionEnd
)";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizePreHeadersPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

}  // namespace
}  // namespace opt
}  // namespace spvtools
