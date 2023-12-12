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

using StructurizeMergeExitBlockPassTest = PassTest<::testing::Test>;

TEST_F(StructurizeMergeExitBlockPassTest, NoOp) {
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
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithoutChange);
}

TEST_F(StructurizeMergeExitBlockPassTest, NoOpSingleRegionSingleExit) {
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
              %50 = OpConvergenceEntry
                    OpBranchConditional %false %6 %8
               %6 = OpLabel
                    OpBranch %7
               %7 = OpLabel
                    OpBranch %5
               %8 = OpLabel
                    OpReturn
; CHECK:      %50 = OpConvergenceEntry
; CHECK-NEXT:       OpBranchConditional %false %6 %8
; CHECK-NEXT:  %6 = OpLabel
; CHECK-NEXT:       OpBranch %7
; CHECK-NEXT:  %7 = OpLabel
; CHECK-NEXT:       OpBranch %5
; CHECK-NEXT:  %8 = OpLabel
; CHECK-NEXT:       OpReturn
               OpFunctionEnd
  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithoutChange);
}

TEST_F(StructurizeMergeExitBlockPassTest, NoOpMultipleEdgesButSingleExit) {
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
              %50 = OpConvergenceEntry
                    OpBranchConditional %false %6 %8
               %6 = OpLabel
                    OpBranchConditional %false %7 %8
               %7 = OpLabel
                    OpBranch %5
               %8 = OpLabel
                    OpReturn
; CHECK:      %50 = OpConvergenceEntry
; CHECK-NEXT:       OpBranchConditional %false %6 %8
; CHECK-NEXT:  %6 = OpLabel
; CHECK-NEXT:       OpBranchConditional %false %7 %8
; CHECK-NEXT:  %7 = OpLabel
; CHECK-NEXT:       OpBranch %5
; CHECK-NEXT:  %8 = OpLabel
; CHECK-NEXT:       OpReturn
               OpFunctionEnd
  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithoutChange);
}

TEST_F(StructurizeMergeExitBlockPassTest, MergingTwoEarlyReturns) {
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
                          %50 = OpConvergenceEntry
                                OpBranchConditional %false %6 %8
                           %6 = OpLabel
                                OpBranchConditional %false %7 %9
                           %7 = OpLabel
                                OpBranch %5
                           %8 = OpLabel
                                OpReturn
                           %9 = OpLabel
                                OpReturn

; CHECK:                  %50 = OpConvergenceEntry
; CHECK-NEXT:                   OpBranchConditional %false %6 [[merge:%[0-9]+]]

; CHECK-NEXT:              %6 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %7 [[merge]]

; CHECK-DAG:        [[merge]] = OpLabel
; CHECK-NEXT: [[tmp:%[0-9]+]] = OpPhi %uint %uint_0 %5 %uint_1 %6
; CHECK-NEXT:                   OpSwitch [[tmp]] %8 1 %9

; CHECK-NEXT:              %9 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-NEXT:              %8 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-NEXT:              %7 = OpLabel
; CHECK-NEXT:                   OpBranch %5
                                OpFunctionEnd
  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizeMergeExitBlockPassTest, MergingThreeEarlyReturns) {
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
                          %50 = OpConvergenceEntry
                                OpBranchConditional %false %6 %9
                           %6 = OpLabel
                                OpBranchConditional %false %7 %10
                           %7 = OpLabel
                                OpBranchConditional %false %8 %11
                           %8 = OpLabel
                                OpBranch %5
                           %9 = OpLabel
                                OpReturn
                          %10 = OpLabel
                                OpReturn
                          %11 = OpLabel
                                OpReturn
; CHECK-DAG:               %5 = OpLabel
; CHECK-NEXT:             %50 = OpConvergenceEntry
; CHECK-NEXT:                   OpBranchConditional %false %6 [[merge:%[0-9]+]]

; CHECK-DAG:               %6 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %7 [[merge]]

; CHECK-DAG:               %7 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %8 [[merge]]

; CHECK-DAG:        [[merge]] = OpLabel
; CHECK-NEXT: [[tmp:%[0-9]+]] = OpPhi %uint %uint_0 %5 %uint_1 %6 %uint_2 %7
; CHECK-NEXT:                   OpSwitch [[tmp]] %9 1 %10 2 %11

; CHECK-DAG:              %11 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:              %10 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:               %9 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:               %8 = OpLabel
; CHECK-NEXT:                   OpBranch %5
                                OpFunctionEnd
  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

TEST_F(StructurizeMergeExitBlockPassTest, NestedRegionEarlyExit) {
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
                          %50 = OpConvergenceEntry
                                OpBranchConditional %false %6 %11
                          %11 = OpLabel
                                OpReturn
                           %6 = OpLabel
                          %51 = OpConvergenceEntry
                                OpBranchConditional %false %10 %7
                          %10 = OpLabel
                                OpBranch %5
                           %7 = OpLabel
                                OpBranchConditional %false %8 %9
                           %8 = OpLabel
                                OpBranch %6
                           %9 = OpLabel
                                OpReturn
; CHECK-DAG:               %5 = OpLabel
; CHECK-NEXT:             %50 = OpConvergenceEntry
; CHECK-NEXT:                   OpBranchConditional %false %6 [[out_merge:%[0-9]+]]

; CHECK-DAG:               %6 = OpLabel
; CHECK-NEXT:             %51 = OpConvergenceEntry
; CHECK-NEXT:                   OpBranchConditional %false [[in_merge:%[0-9]+]] %7
; CHECK-DAG:               %7 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %8 [[in_merge]]
; CHECK-DAG:               %8 = OpLabel
; CHECK-NEXT:                   OpBranch %6

; CHECK-DAG:     [[in_merge]] = OpLabel
; CHECK-NEXT: [[tmp:%[0-9]+]] = OpPhi %uint %uint_0 %6 %uint_1 %7
; CHECK-NEXT:                   OpSwitch [[tmp]] %10 1 [[out_merge]]

; CHECK-DAG:    [[out_merge]] = OpLabel
; CHECK-NEXT: [[tmp:%[0-9]+]] = OpPhi %uint %uint_0 %5 %uint_1 [[in_merge]]
; CHECK-NEXT:                   OpSwitch [[tmp]] %11 1 %9

; CHECK-DAG:               %9 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:              %11 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:              %10 = OpLabel
; CHECK-NEXT:                   OpBranch %5
  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

// When If multiple exits lead to the same block, we could end-up generating a switch for which multiple cases
// have the same target. This should be simplified.
TEST_F(StructurizeMergeExitBlockPassTest, NestedRegionEarlyExitSimplifySwitch) {
  const std::string text =
      R"(
                                OpCapability Shader
                                OpMemoryModel Logical GLSL450
                                OpEntryPoint GLCompute %1 "main"
                                OpExecutionMode %1 LocalSize 1 1 1
                        %void = OpTypeVoid
                           %3 = OpTypeFunction %void
                        %bool = OpTypeBool
                       %false = OpConstantFalse %bool
                        %uint = OpTypeInt 32 0
                      %uint_0 = OpConstant %uint 0
                      %uint_1 = OpConstant %uint 1
                           %1 = OpFunction %void None %3
                           %9 = OpLabel
                          %10 = OpConvergenceEntry
                                OpBranchConditional %false %20 %11
                          %20 = OpLabel
                          %12 = OpConvergenceLoop
                                OpBranchConditional %false %30 %21
                          %30 = OpLabel
                                OpBranchConditional %false %31 %11
                          %31 = OpLabel
                                OpBranchConditional %false %20 %11
                          %21 = OpLabel
                                OpBranch %9
                          %11 = OpLabel
                                OpReturn

; CHECK-DAG:               %9 = OpLabel
; CHECK-NEXT:             %10 = OpConvergenceEntry
; CHECK-NEXT:                   OpBranchConditional %false %20 %11

; CHECK-DAG:              %20 = OpLabel
; CHECK-NEXT:             %12 = OpConvergenceLoop
; CHECK-NEXT:                   OpBranchConditional %false %30 [[exit_0:%[0-9]+]]

; CHECK-DAG:              %30 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %31 [[exit_0]]

; CHECK-DAG:              %31 = OpLabel
; CHECK-NEXT:                   OpBranchConditional %false %20 [[exit_0]]

; CHECK-DAG:       [[exit_0]] = OpLabel
; CHECK-NEXT: [[tmp:%[0-9]+]] = OpPhi %uint %uint_0 %20 %uint_1 %30 %uint_1 %31
; CHECK-NEXT:                   OpSwitch [[tmp]] %21 1 %11

; CHECK-DAG:              %11 = OpLabel
; CHECK-NEXT:                   OpReturn

; CHECK-DAG:              %21 = OpLabel
; CHECK-NEXT:                   OpBranch %9

  )";

  SetAssembleOptions(SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  auto result = SinglePassRunAndMatch<StructurizeMergeExitBlockPass>(text, /* do_validation= */ false);
  EXPECT_EQ(std::get<1>(result), Pass::Status::SuccessWithChange);
}

}  // namespace
}  // namespace opt
}  // namespace spvtools
