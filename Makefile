
CCFLAGS = -std=c11 -g -Wall -Weverything \
-Wconditional-uninitialized \
-Wenum-conversion \
-Wimplicit-fallthrough \
-Winit-self \
-Wmissing-field-initializers \
-Wno-bad-function-cast \
-Wno-declaration-after-statement \
-Wno-double-promotion \
-Wno-error=deprecated-declarations \
-Wno-error=incompatible-pointer-types-discards-qualifiers \
-Wno-error=shorten-64-to-32 \
-Wno-error=unused-but-set-variable \
-Wno-error=unused-function \
-Wno-error=unused-label \
-Wno-error=unused-variable \
-Wno-gnu-empty-initializer  \
-Wno-gnu-statement-expression \
-Wno-implicit-float-conversion \
-Wno-missing-prototypes \
-Wno-missing-variable-declarations \
-Wno-padded \
-Wno-pointer-sign \
-Wno-sign-conversion \
-Wno-unused-command-line-argument \
-Wnullable-to-nonnull-conversion  \
-Wshadow \
-Wstrict-prototypes \
-Wuninitialized \
-Wzero-length-array \
-I build \
-I lib/raylib-4.5.0_macos/include -L lib/raylib-4.5.0_macos/lib -l raylib -framework Cocoa -framework IOKit
# -Wfloat-conversion

run: build/ShapeUp
	./build/ShapeUp

profile: CCFLAGS += -O3
profile: build/ShapeUp

sanitize: CCFLAGS += -g -fsanitize=undefined,address
sanitize: run

debug: build/ShapeUp
	lldb -o "run" ./build/ShapeUp

build/shaders.h: src/*.fs Makefile build
	(cat src/shader_base.fs; printf '\0') > build/shader_base.fs
	(cat src/shader_prefix.fs; printf '\0') > build/shader_prefix.fs
	(cat src/slicer_body.fs; printf '\0') > build/slicer_body.fs
	(cat src/selection.fs; printf '\0') > build/selection.fs
	cd build && xxd -i shader_base.fs shaders.h
	cd build && xxd -i shader_prefix.fs >> shaders.h
	cd build && xxd -i slicer_body.fs >> shaders.h
	cd build && xxd -i selection.fs >> shaders.h

build/ShapeUp: src/* Makefile build/shaders.h build
	$(CC) $(CCFLAGS) src/pinchSwizzle.m src/main.c -o build/ShapeUp

make_the_bug: build
	$(CC) $(CCFLAGS) $(BUG_FILE) -o build/bug
	./build/bug

build:
	mkdir -p build

bug1: BUG_FILE=bugs/render_scale_bug.c
bug1: make_the_bug

bug2: BUG_FILE=bugs/keyevents.c
bug2: make_the_bug

bug2: BUG_FILE=bugs/keyevents.c
bug2: make_the_bug

bug3: BUG_FILE=bugs/exportpng.c
bug3: make_the_bug

bug4: BUG_FILE=bugs/textpadding.c
bug4: make_the_bug

bug5: BUG_FILE=bugs/retina_scale.c
bug5: make_the_bug

bug6: BUG_FILE=bugs/gamepad.c
bug6: make_the_bug

clean:
	rm -rf build