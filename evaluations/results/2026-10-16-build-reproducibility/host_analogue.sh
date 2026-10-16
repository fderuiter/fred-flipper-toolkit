#!/bin/sh
# Host analogue of the .fap packaging step (issue #17). Uses the host gcc/binutils,
# not the Flipper toolchain, so it shows the mechanism, not the SDK's hashes.
# fbt makes the .fap with: objcopy --strip-debug --strip-unneeded --add-gnu-debuglink=<debug elf>
# Usage: sh host_analogue.sh <empty scratch dir>
set -eu
S=${1:?scratch dir}
mkdir -p "$S/pathA/app" "$S/pathB/deeper/app" "$S/order"
printf 'int add(int a, int b) { return a + b; }\nint main(void) { return add(1, 2); }\n' > "$S/pathA/app/m.c"
cp "$S/pathA/app/m.c" "$S/pathB/deeper/app/m.c"
build() { d=$1; shift
  (cd "$d" && gcc -g -O2 -ffunction-sections -fdata-sections "$@" -c m.c -o m.o &&
   gcc -g -Wl,--build-id=none m.o -o app_d.elf &&
   objcopy --strip-debug --strip-unneeded --add-gnu-debuglink=app_d.elf app_d.elf app.fap &&
   objcopy --strip-debug --strip-unneeded app_d.elf nolink.fap); }
h() { sha256sum "$1" | cut -c1-16; }
build "$S/pathA/app"; build "$S/pathB/deeper/app"
echo "two paths, default flags:      $(h "$S/pathA/app/app.fap") $(h "$S/pathB/deeper/app/app.fap")"
echo "  .gnu_debuglink A: $(readelf -x .gnu_debuglink "$S/pathA/app/app.fap" | grep 0x)"
echo "  .gnu_debuglink B: $(readelf -x .gnu_debuglink "$S/pathB/deeper/app/app.fap" | grep 0x)"
echo "  same, no debuglink:          $(h "$S/pathA/app/nolink.fap") $(h "$S/pathB/deeper/app/nolink.fap")"
build "$S/pathA/app" "-ffile-prefix-map=$S/pathA/app=."
build "$S/pathB/deeper/app" "-ffile-prefix-map=$S/pathB/deeper/app=."
echo "two paths, -ffile-prefix-map:  $(h "$S/pathA/app/app.fap") $(h "$S/pathB/deeper/app/app.fap")"
build "$S/pathA/app"; a=$(h "$S/pathA/app/app.fap"); rm -f "$S/pathA/app/"*.o "$S/pathA/app/"*.elf "$S/pathA/app/"*.fap
build "$S/pathA/app"; echo "same path, two clean builds:  $a $(h "$S/pathA/app/app.fap")"
cd "$S/order"
printf 'int f1(int x) { return x * 3; }\n' > a.c
printf 'int f1(int);\nint main(void) { return f1(2); }\n' > b.c
gcc -g -O2 -ffunction-sections -c a.c -o a.o; gcc -g -O2 -ffunction-sections -c b.c -o b.o
for o in "a.o b.o" "b.o a.o"; do
  # shellcheck disable=SC2086
  gcc -g -Wl,--build-id=none $o -o d.elf; objcopy --strip-debug --strip-unneeded d.elf x.fap
  echo "link order $o:           $(h x.fap)"
done
