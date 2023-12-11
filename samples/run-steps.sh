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

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <input-file>"
  exit 1
fi

FILE=$1

if [ "$#" -lt 2 ]; then
  OUTPUT="/tmp/output"
else
  OUTPUT=$2
fi

rm -f /tmp/step_*

echo 'Running structurize-pre-headers.'
spirv-opt $FILE       -o /tmp/step_1 --structurize-pre-headers --skip-validation
spirv-cfg /tmp/step_1 -o - | dot -Tpng > /tmp/step_1.png

echo 'Running structurize-split-convergent-operation.'
spirv-opt /tmp/step_1 -o /tmp/step_2 --structurize-split-convergent-operation --skip-validation
spirv-cfg /tmp/step_2 -o - | dot -Tpng > /tmp/step_2.png

echo 'Running structurize-merge-back-edge.'
spirv-opt /tmp/step_2 -o /tmp/step_3 --structurize-merge-back-edge --skip-validation
spirv-cfg /tmp/step_3 -o - | dot -Tpng > /tmp/step_3.png

echo 'Running structurize-merge-exit-block.'
spirv-opt /tmp/step_3 -o /tmp/step_4 --structurize-merge-exit-block --skip-validation
spirv-cfg /tmp/step_4 -o - | dot -Tpng > /tmp/step_4.png

echo 'Running structurize-identify-loops.'
spirv-opt /tmp/step_4 -o /tmp/step_5 --structurize-identify-loops --skip-validation
spirv-cfg /tmp/step_5 -o - | dot -Tpng > /tmp/step_5.png

echo 'Running structurize-identify-selection-with-merge.'
spirv-opt /tmp/step_5 -o /tmp/step_6 --structurize-identify-selection-with-merge --skip-validation
spirv-cfg /tmp/step_6 -o - | dot -Tpng > /tmp/step_6.png

echo 'Running structurize-identify-selection-without-merge.'
spirv-opt /tmp/step_6 -o /tmp/step_7 --structurize-identify-selection-without-merge --skip-validation
spirv-cfg /tmp/step_7 -o - | dot -Tpng > /tmp/step_7.png

echo "done, writting output to $OUTPUT"
cp /tmp/step_7 "$OUTPUT"
