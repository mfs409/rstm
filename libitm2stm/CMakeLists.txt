#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

# Configures the itm2stm library build.
include (AppendFlags)

# Add some additional compiler flags.
append_cxx_flags(-fno-rtti -fno-exceptions -fno-strict-aliasing
  -Wno-invalid-offsetof -Wno-unused-function)

if (CMAKE_COMPILER_IS_GNUCXX AND CMAKE_SYSTEM_NAME MATCHES "Darwin")
  append_cxx_flags(-Wno-unknown-pragmas)
endif ()

# This macro enables to change TM-ABI defs (see libitm.h)
# Default for ICC
if (rstm_itm2stm STREQUAL "DTMC")
  append_cxx_flags(-emit-llvm -D_ITM_DTMC)
elseif (rstm_itm2stm STREQUAL "GCC")
  append_cxx_flags(-D_ITM_GCC)
else ()
  append_cxx_flags(-D_ITM_ICC)
endif ()


set(cppsources
  BlockOperations.cpp
  Scope.cpp
  Transaction.cpp
  libitm-5.1,5.cpp
  libitm-5.2.cpp
  libitm-5.3.cpp
  libitm-5.4.cpp
  libitm-5.7.cpp
  libitm-5.8.cpp
  libitm-5.9.cpp
  libitm-5.10.cpp
  libitm-5.11.cpp
  libitm-5.12.cpp
  libitm-5.13,14.cpp
  libitm-5.15.cpp
  libitm-5.16.cpp
  libitm-5.17.cpp)

set(asmsources
  arch/x86/_ITM_beginTransaction.S
  arch/x86/_ITM_beginTransaction_no_td.S
  arch/x86/checkpoint_restore.S
  arch/x86_64/_ITM_beginTransaction.S
  arch/x86_64/_ITM_beginTransaction_no_td.S
  arch/x86_64/checkpoint_restore.S)

if (itm2stm_enable_assert_on_irrevocable)
  append_list_property(SOURCE libitm-5.11.cpp COMPILE_DEFINITIONS
    ITM2STM_ASSERT_ON_IRREVOCABLE)
endif ()
  
# Hack because preprocessed asm files aren't supported in cmake-2.8.4, so we
# pretend they're C files. They also need the additional include directory.
set_source_files_properties(
  ${asmsources} PROPERTIES
  LANGUAGE C
  COMPILE_FLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/arch)

# Generic DTMC rules for 32/64 bits 
macro(dtmc arch)
    add_library(tanger-stm
      ${arch_path}/_ITM_beginTransaction_no_td.S
      ${arch_path}/checkpoint_restore.S)
    append_property(TARGET tanger-stm COMPILE_FLAGS -m${arch} -I${arch_path})

    add_library(itm2stm${arch}
      ${cppsources}
      dtmc.cpp)
    add_dependencies(itm2stm${arch} stm${arch} tanger-stm)
    append_property(TARGET itm2stm${arch} COMPILE_FLAGS -m${arch} -I${arch_path})
    # Merge itm2stm and stm in one library
    add_custom_command(TARGET itm2stm${arch}
                       POST_BUILD
                       COMMAND ${CMAKE_LINKER} -link-as-library -o libtanger-stm.bc libitm2stm${arch}.a ../libstm/libstm${arch}.a 
                       )
    # Copying DTMC required files
    add_custom_command(TARGET itm2stm${arch} 
                       POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/libtanger-stm.public-symbols ${CMAKE_CURRENT_BINARY_DIR}/libtanger-stm.public-symbols)
    add_custom_command(TARGET itm2stm${arch}
                       POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/libtanger-stm.support ${CMAKE_CURRENT_BINARY_DIR}/libtanger-stm.support)
endmacro(dtmc)

# Generic GCC rules for 32/64 bits 
macro(gcc arch)
  add_library(itm2stm${arch} STATIC
    ${cppsources}
    gcc.cpp
    ${arch_path}/_ITM_beginTransaction_no_td.S
    ${arch_path}/checkpoint_restore.S)
  add_library(itm2stm${arch}shared SHARED
    ${cppsources}
    gcc.cpp
    ${arch_path}/_ITM_beginTransaction_no_td.S
    ${arch_path}/checkpoint_restore.S)
  append_property(TARGET itm2stm${arch} COMPILE_FLAGS -m${arch} -I${arch_path})
  append_property(TARGET itm2stm${arch} LINK_FLAGS -m${arch})
  append_property(TARGET itm2stm${arch}shared COMPILE_FLAGS -m${arch} -I${arch_path})
  append_property(TARGET itm2stm${arch}shared LINK_FLAGS -m${arch})
  target_link_libraries(itm2stm${arch} stm${arch})
endmacro(gcc)

# TODO for GCC (and ICC), must merge the two library in one (at least more convenient)

if (rstm_build_32-bit)
  set(arch_path ${CMAKE_CURRENT_SOURCE_DIR}/arch/x86)

  # For Intel STM Compiler
  if (rstm_itm2stm STREQUAL "ICC")
    add_library(itm2stm32
      ${cppsources}
      icc.cpp
      ${arch_path}/_ITM_beginTransaction.S
      ${arch_path}/checkpoint_restore.S)
    append_property(TARGET itm2stm32 COMPILE_FLAGS -m32 -I${arch_path})
    target_link_libraries(itm2stm32 stm32)

  # For DTMC 
  elseif (rstm_itm2stm STREQUAL "DTMC")
    dtmc(32)
  # For GCC 
  elseif (rstm_itm2stm STREQUAL "GCC")
    gcc(32)
  endif ()
endif ()

if (rstm_build_64-bit)
  set(arch_path ${CMAKE_CURRENT_SOURCE_DIR}/arch/x86_64)
  # For Intel STM Compiler
  if (rstm_itm2stm STREQUAL "ICC")
    add_library(itm2stm64
      ${cppsources}
      icc.cpp
      ${arch_path}/_ITM_beginTransaction.S
      ${arch_path}/checkpoint_restore.S)
    append_property(TARGET itm2stm64 COMPILE_FLAGS -m64 -I${arch_path})
    target_link_libraries(itm2stm64 stm64)
  # For DTMC 
  elseif (rstm_itm2stm STREQUAL "DTMC")
    dtmc(64)
  # For GCC 
  elseif (rstm_itm2stm STREQUAL "GCC")
    gcc(64)
  endif ()
endif ()
