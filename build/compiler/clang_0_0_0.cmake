set(COMPILER_CFLAGS "-g -Wpointer-arith -Wformat=2 -Wconversion -Wextra -Wall -Werror -Wshadow -Wcast-qual -Wstrict-overflow=5 -Wuninitialized -Wmissing-prototypes -Wbad-function-cast -Wstrict-prototypes")
set(COMPILER_CXXFLAGS "-nostdinc++ -I/usr/include/c++/v1 -g -Wpointer-arith -Wformat=2 -Wconversion -Wextra -Wall -Werror -Wshadow -Wcast-qual -Wstrict-overflow=5 -Wuninitialized")
if("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
	set(LIB_PSS_VM_STACK_LIMIT 384)
endif("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
