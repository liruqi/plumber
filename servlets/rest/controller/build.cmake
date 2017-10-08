find_package(PkgConfig)
pkg_check_modules(PC_UUID uuid)
if("${PC_UUID_FOUND}" STREQUAL "1")
	find_library(UUID_LIBRARIES NAMES ${PC_UUID_LIBRARIES} PATHS ${PC_UUID_LIBRARY_DIRS})
	set(LOCAL_INCLUDE ${PC_UUID_INCLUDE_DIRS})
	set(LOCAL_LIBS pstd proto ${UUID_LIBRARIES})
else("${PC_UUID_FOUND}" STREQUAL "1")
	message("Missing libuuid, restcon servlet disabled")
	set(build_rest_controller "no")
endif("${PC_UUID_FOUND}" STREQUAL "1")
set(INSTALL yes)
