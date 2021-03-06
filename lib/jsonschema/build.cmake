find_package(PkgConfig)
set(PACKAGE_CONF_INSTALL_PATH "include/jsonschema")

if("${RAPIDJSON_DIR}" STREQUAL "RAPIDJSON_DIR-NOTFOUND")
	set(build_jsonschema "no")
endif("${RAPIDJSON_DIR}" STREQUAL "RAPIDJSON_DIR-NOTFOUND")

if("${build_jsonschema}" STREQUAL "yes")
	set(TYPE shared-library)
	list(APPEND LOCAL_CFLAGS "-fPIC")
	list(APPEND LOCAL_CXXFLAGS "-fPIC")
	list(APPEND LOCAL_INCLUDE "${RAPIDJSON_DIR}")
	set(LOCAL_CXXFLAGS "${LOCAL_CFLAGS} -DUSE_RAPIDJSON")
	set(INSTALL "yes")
	install_includes("${SOURCE_PATH}/include" "include/jsonschema" "*.h")
	install_includes("${CMAKE_SOURCE_DIR}/include/" "include/jsonschema" "error.h")
	install_includes("${CMAKE_SOURCE_DIR}/include/utils/" "include/jsonschema/utils" "static_assertion.h")
	install_includes("${CMAKE_SOURCE_DIR}/include/utils/" "include/jsonschema/utils" "log_macro.h")
	install_plumber_headers("include/jsonschema")
endif("${build_jsonschema}" STREQUAL "yes")
