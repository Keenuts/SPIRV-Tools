# Copyright (c) 2023 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash

set -e

FILE=$1

echo 'Running structurize-pre-headers.'
spirv-opt $FILE       -o /tmp/step_1 --structurize-pre-headers --skip-validation
echo 'Running structurize-split-convergent-operation.'
spirv-opt /tmp/step_1 -o /tmp/step_2 --structurize-split-convergent-operation --skip-validation
echo 'Running structurize-merge-back-edge.'
spirv-opt /tmp/step_2 -o /tmp/step_3 --structurize-merge-back-edge --skip-validation
echo 'Running structurize-merge-exit-block.'
spirv-opt /tmp/step_3 -o /tmp/step_4 --structurize-merge-exit-block --skip-validation
echo 'Running structurize-identify-loops.'
spirv-opt /tmp/step_4 -o /tmp/step_5 --structurize-identify-loops --skip-validation

spirv-cfg /tmp/step_1 -o - | dot -Tpng > /tmp/step_1.png
spirv-cfg /tmp/step_2 -o - | dot -Tpng > /tmp/step_2.png
spirv-cfg /tmp/step_3 -o - | dot -Tpng > /tmp/step_3.png
spirv-cfg /tmp/step_4 -o - | dot -Tpng > /tmp/step_4.png
spirv-cfg /tmp/step_5 -o - | dot -Tpng > /tmp/step_5.png
