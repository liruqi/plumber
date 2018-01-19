find_file(LIBRE2_INCLUDE_DIR "re2/re2.h" HINTS ${LIBRE2_PREFIX})
find_library(LIBRE2_LIBRAYIES NAMES "${CMAKE_SHARED_LIBRARY_PREFIX}re2${CMAKE_SHARED_LIBRARY_SUFFIX}" HINTS ${LIBRE2_PREFIX})
if("${LIBRE2_INCLUDE_DIR}" STREQUAL "LIBRE2_INCLUDE_DIR-NOTFOUND" OR "${LIBRE2_LIBRAYIES}" STREQUAL "LIBRE2_LIBRAYIES-NOTFOUND")
	message("libre2 not found, dataflow.regex servlet has been disabled")
	set(build_dataflow_regex "no")
else("${LIBRE2_INCLUDE_DIR}" STREQUAL "LIBRE2_INCLUDE_DIR-NOTFOUND" OR "${LIBRE2_LIBRAYIES}" STREQUAL "LIBRE2_LIBRAYIES-NOTFOUND")
	get_filename_component(LIBRE2_INCLUDE_DIR ${LIBRE2_INCLUDE_DIR} DIRECTORY)
	get_filename_component(LIBRE2_INCLUDE_DIR ${LIBRE2_INCLUDE_DIR} DIRECTORY)
	list(APPEND LOCAL_LIBS ${LIBRE2_LIBRAYIES} proto pstd)
	list(APPEND LOCAL_INCLUDE ${LIBRE2_INCLUDE_DIR})
	message("${LIBRE2_LIBRAYIES}")
	set(INSTALL yes)
endif("${LIBRE2_INCLUDE_DIR}" STREQUAL "LIBRE2_INCLUDE_DIR-NOTFOUND" OR "${LIBRE2_LIBRAYIES}" STREQUAL "LIBRE2_LIBRAYIES-NOTFOUND")
