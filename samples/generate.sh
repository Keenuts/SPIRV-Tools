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
