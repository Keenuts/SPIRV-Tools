.PRECIOUS: %.o %.png %.spv %.truth.o %.truth.png
.PHONY: clean help %

%.truth.o: %.hlsl
	dxc -T cs_6_0 -E main -fcgl -Vd -spirv $^ -Fo $@

%.truth.o: %.txtspv
	spirv-as $^ -o $@

%.truth.png: %.truth.o
	spirv-cfg $^ -o - | dot -Tpng > $@

%.o: %.truth.o
	spirv-opt --strip-headers --skip-validation $^ -o $@

%.spv: %.o
	spirv-dis $^ > $@

%.png: %.o
	spirv-cfg $^ -o - | dot -Tpng > $@

%: %.png %.spv %.o %.truth.png ;

help:
	@ echo "To build files for <filename>, run 'make <filename>'"

clean:
	$(RM) *.o *.spv *.png
