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

for f in $(ls *.o); do
  echo "Testing $f."
  basename="$(echo $f | cut -d '.' -f 1)"

  #spirv-opt --structurize --skip-validation $f -o /tmp/$basename.scfg.o > /dev/null
  ./run-steps.sh $f $basename.scfg.o
  spirv-cfg /tmp/$basename.scfg.o | dot -Tpng > "/tmp/$basename.scfg.png"
  spirv-val /tmp/$basename.scfg.o
done

