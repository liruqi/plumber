include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "-g -Wpointer-arith -Wextra -Wall -Werror")
set(COMPILER_CXXFLAGS "-g -Wpointer-arith -Wextra -Wall -Werror")
if("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
	set(LIB_PSS_VM_STACK_LIMIT 128)
endif("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")