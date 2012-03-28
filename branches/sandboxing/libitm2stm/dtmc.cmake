# Use this file like this:
# cmake -C libitm2stm/dtmc.cmake

# Define the ABI compatibility
set(rstm_itm2stm "DTMC" CACHE STRING "")

# Define LLVM toolchain
set(CMAKE_C_COMPILER "llvm-gcc" CACHE INTERNAL "" FORCE )
set(CMAKE_CXX_COMPILER "llvm-g++" CACHE INTERNAL "" FORCE )
set(CMAKE_LINKER "llvm-ld" CACHE INTERNAL "" FORCE )
# Following are not required
#set(CMAKE_AR "llvm-ar" CACHE INTERNAL "" FORCE )
#set(CMAKE_RANLIB "llvm-ranlib" CACHE INTERNAL "" FORCE )

# Changing only the way C++ static library are built (libtanger-stm.a needs regular build)
set(CMAKE_CXX_CREATE_STATIC_LIBRARY
    "<CMAKE_LINKER> -link-as-library -o <TARGET> <OBJECTS> " CACHE INTERNAL "" FORCE )

#set(CMAKE_C_CREATE_STATIC_LIBRARY "llvm-ld" CACHE INTERNAL "" FORCE )

# LLVM <=2.9 has problem with DWARF so force Release mode
set(CMAKE_BUILD_TYPE "Release" CACHE INTERNAL "" FORCE)

