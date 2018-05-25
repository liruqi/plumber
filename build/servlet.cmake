#message("--------------------------------------------------------")
file(GLOB_RECURSE servlet_cmakes RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/${SERVLET_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/${SERVLET_DIR}/*build.cmake")
foreach(servlet_cmake ${servlet_cmakes})
	get_filename_component(servlet_path ${servlet_cmake} DIRECTORY)
	get_filename_component(servlet_dir ${servlet_path} DIRECTORY)
	get_filename_component(servlet ${servlet_path} NAME)
	set(NAMESPACE "${servlet_dir}")
	string(REPLACE "/" "." servlet_logical_name ${NAMESPACE}/${servlet})
	string(REPLACE "/" "_" servlet_config_name ${NAMESPACE}/${servlet})
	set(INSTALL "no")
	if(NOT "${build_${servlet_config_name}}" STREQUAL "no")
		set(SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${SERVLET_DIR}/${servlet_dir}/${servlet})
		set(SOURCE "")
		set(LOCAL_CFLAGS )
		set(LOCAL_CXXFLAGS )
		set(LOCAL_LIBS )
		set(LOCAL_INCLUDE )
		set(LOCAL_SOURCE )
		include(${CMAKE_SOURCE_DIR}/${SERVLET_DIR}/${servlet_cmake})
		if(NOT "${build_${servlet_config_name}}" STREQUAL "no")
			set(package_status "${package_status} -Dbuild_${servlet_config_name}=yes")
			aux_source_directory(${SOURCE_PATH} SOURCE)
			list(APPEND LOCAL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}/pservlet/include")
			list(APPEND LOCAL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/${SERVLET_DIR}/${servlet_dir}/${servlet}/include")
			list(APPEND LOCAL_LIBS pservlet)
			foreach(source_file ${SOURCE})
				if(${source_file} MATCHES ".*\\.c$")
					set_source_files_properties(${source_file} PROPERTIES COMPILE_FLAGS "${CFLAGS} ${LOCAL_CFLAGS}")
				elseif(${source_file} MATCHES ".*\\.cpp$")
					set_source_files_properties(${source_file} PROPERTIES COMPILE_FLAGS "${CXXFLAGS} ${LOCAL_CXXFLAGS}")
				endif(${source_file} MATCHES ".*\\.c$")
			endforeach(source_file ${LOCAL_SOURCE})
			add_library(${servlet_logical_name} SHARED ${SOURCE} ${LOCAL_SOURCE})
			set_target_properties(${servlet_logical_name} PROPERTIES 
			                      OUTPUT_NAME ${servlet}
			                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/servlet/${NAMESPACE})
			target_include_directories(${servlet_logical_name} PUBLIC ${LOCAL_INCLUDE})
			target_link_libraries(${servlet_logical_name} ${LOCAL_LIBS})
			if("${INSTALL}" STREQUAL "yes")
				install(TARGETS ${servlet_logical_name} DESTINATION lib/plumber/servlet/${NAMESPACE})
			endif("${INSTALL}" STREQUAL "yes")
			set(build_${servlet_config_name} "yes")
			file(GLOB_RECURSE servlet_tests RELATIVE "${SOURCE_PATH}/test" "${SOURCE_PATH}/test/*/servlet-def.pss")
			if(NOT "${servlet_tests}" STREQUAL "")
				set(servlet_test_name ${NAMESPACE}/${servlet})
				foreach(case_dir ${servlet_tests})
					get_filename_component(servlet_test_case ${case_dir} DIRECTORY)
					add_test("servlet_${servlet_config_name}_${servlet_test_case}" 
						     python ${CMAKE_CURRENT_BINARY_DIR}/servlet-test-driver.py ${servlet_test_name} ${servlet_test_case})
					set_tests_properties("servlet_${servlet_config_name}_${servlet_test_case}"
										  PROPERTIES ENVIRONMENT
										  ASAN_OPTIONS=detect_odr_violation=0)
				endforeach(case_dir in ${servlet_tests})
			endif(NOT "${servlet_tests}" STREQUAL "")
		else(NOT "${build_${servlet_config_name}}" STREQUAL "no")
			set(package_status "${package_status} -Dbuild_${servlet_config_name}=no")
			set(build_${servlet_config_name} "no")
			set(INSTALL no)
		endif(NOT "${build_${servlet_config_name}}" STREQUAL "no")
		compile_protocol_type_files("${servlet}" "${CMAKE_CURRENT_SOURCE_DIR}/${SERVLET_DIR}/${servlet_dir}/${servlet}/protocol")
	endif(NOT "${build_${servlet_config_name}}" STREQUAL "no")
	append_pakage_configure(${servlet_logical_name} servlet ${build_${servlet_config_name}} ${INSTALL})
endforeach(servlet_cmake ${servlet_cmakes})
set(PYSERVLET_INCLUDE ${CMAKE_INSTALL_PREFIX}/include/pservlet)
set(PYSERVLET_LIBPATH ${CMAKE_INSTALL_PREFIX}/lib)

if("${SYSNAME}" STREQUAL "Darwin")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/misc/servlet.mk.darwin.in" 
                   "${CMAKE_CURRENT_BINARY_DIR}/servlet.mk")
else("${SYSNAME}" STREQUAL "Darwin")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/misc/servlet.mk.in" 
                   "${CMAKE_CURRENT_BINARY_DIR}/servlet.mk")
endif("${SYSNAME}" STREQUAL "Darwin")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/servlet.mk" DESTINATION lib/plumber/)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/misc/servlet.cmake" DESTINATION lib/plumber/cmake/)

if("${MEMCHECK_TEST}" STREQUAL "yes" AND NOT "${VALGRIND_PROGRAM}" STREQUAL "VALGRIND_PROGRAM-NOTFOUND")
	set(SERVLET_VALGRIND_PARAM )
	list(APPEND SERVLET_VALGRIND_PARAM ${VALGRIND_PROGRAM})
	list(APPEND SERVLET_VALGRIND_PARAM --errors-for-leak-kinds=definite,possible,indirect)
	list(APPEND SERVLET_VALGRIND_PARAM --leak-check=full)
	list(APPEND SERVLET_VALGRIND_PARAM --error-exitcode=1)
endif("${MEMCHECK_TEST}" STREQUAL "yes" AND NOT "${VALGRIND_PROGRAM}" STREQUAL "VALGRIND_PROGRAM-NOTFOUND")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test/servlet-test-driver.py.in"
	           "${CMAKE_CURRENT_BINARY_DIR}/servlet-test-driver.py")

