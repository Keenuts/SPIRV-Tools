#!/bin/bash

#set -e

for f in $(ls *.o); do
  echo "Testing $f."
  basename="$(echo $f | cut -d '.' -f 1)"

  spirv-opt --structurize --skip-validation $f -o /tmp/$basename.scfg.o > /dev/null
  spirv-val /tmp/$basename.scfg.o
  spirv-cfg /tmp/$basename.scfg.o | dot -Tpng > "/tmp/$basename.scfg.png"
done

