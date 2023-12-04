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

for f in $(ls *.hlsl); do
  echo "Processing $f."
  basename="$(echo $f | cut -d '.' -f 1)"

  dxc -T cs_6_0 -E main -fcgl -spirv $f > /tmp/base.spv
  spirv-as /tmp/base.spv -o /tmp/base.o
  spirv-cfg /tmp/base.o | dot -Tpng > "$basename.truth.png"
  spirv-opt --strip-headers --skip-validation /tmp/base.o -o /tmp/stripped.o
  cp /tmp/stripped.o "$basename.o"
  spirv-dis /tmp/stripped.o > "$basename.spv"
  spirv-cfg /tmp/stripped.o | dot -Tpng > "$basename.png"
done

for f in $(ls *.txtspv); do
  echo "Processing $f."
  basename="$(echo $f | cut -d '.' -f 1)"

  spirv-as $f -o /tmp/base.o
  spirv-cfg /tmp/base.o | dot -Tpng > "$basename.truth.png"
  spirv-opt --strip-headers --skip-validation /tmp/base.o -o /tmp/stripped.o
  cp /tmp/stripped.o "$basename.o"
  spirv-dis /tmp/stripped.o > "$basename.spv"
  spirv-cfg /tmp/stripped.o | dot -Tpng > "$basename.png"
done
