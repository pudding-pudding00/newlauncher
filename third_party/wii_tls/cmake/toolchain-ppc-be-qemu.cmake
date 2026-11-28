# Big-endian 32-bit PowerPC (the Wii's byte order) on a Linux host, run under qemu-user:
#   apt install gcc-powerpc-linux-gnu qemu-user
#   cmake -S . -B build-be -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-ppc-be-qemu.cmake
#   cmake --build build-be && ctest --test-dir build-be -L be
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ppc)
set(CMAKE_C_COMPILER powerpc-linux-gnu-gcc)
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-ppc -L /usr/powerpc-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /usr/powerpc-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
