set(package_status "" )

macro(build_tool_dir dir)
	file(GLOB targets RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/${dir} "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*")
	foreach(target ${targets})
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}/build.cmake")
			if(NOT "${build_${target}}" STREQUAL "no")
				if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}")
					set(SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target})
					set(SOURCE "")
					set(LOCAL_CFLAGS )
					set(LOCAL_CXXFLAGS )
					set(LOCAL_LIBS )
					set(LOCAL_INCLUDE )
					set(LOCAL_SOURCE  )
					set(PACKAGE_CONF_INSTALL_PATH )
					set(LOCAL_SOURCE_FILES )
					list(APPEND LOCAL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}/include")
					set(INSTALL "no")
					set(TYPE "binary")
					set(build_${target} "yes")
					include("${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}/build.cmake")
					if(build_${target} STREQUAL "yes")
						set(package_status "${package_status} -Dbuild_${target}=yes")
						aux_source_directory(${SOURCE_PATH} SOURCE)
						foreach(local_source_dir ${LOCAL_SOURCE})
							aux_source_directory(${SOURCE_PATH}/${local_source_dir} LOCAL_SOURCE_FILES)
							set(SOURCE ${SOURCE} ${LOCAL_SOURCE_FILES})
						endforeach(local_source_dir ${LOCAL_SOURCE})
						foreach(source_file ${SOURCE})
							if(${source_file} MATCHES ".*\\.c$")
								set_source_files_properties(${source_file} PROPERTIES COMPILE_FLAGS "${CFLAGS} ${LOCAL_CFLAGS}")
							elseif(${source_file} MATCHES ".*\\.cpp$")
								set_source_files_properties(${source_file} PROPERTIES COMPILE_FLAGS "${CXXFLAGS} ${LOCAL_CXXFLAGS}")
							endif(${source_file} MATCHES ".*\\.c$")
						endforeach(source_file ${SOURCE})
						if(EXISTS "${SOURCE_PATH}/package_config.h.in")
							set(package_config_path "${CMAKE_CURRENT_BINARY_DIR}/${dir}/${target}")
							file(MAKE_DIRECTORY ${package_config_path})
							configure_file("${SOURCE_PATH}/package_config.h.in" "${package_config_path}/package_config.h")
							list(APPEND LOCAL_INCLUDE "${package_config_path}")
							if(NOT "x${PACKAGE_CONF_INSTALL_PATH}" STREQUAL "x")
								install_includes("${package_config_path}" "${PACKAGE_CONF_INSTALL_PATH}" "package_config.h")
							endif(NOT "x${PACKAGE_CONF_INSTALL_PATH}" STREQUAL "x")
						endif(EXISTS "${SOURCE_PATH}/package_config.h.in")
						compile_protocol_type_files("${target}" "${SOURCE_PATH}/protocol")
						if("${TYPE}" STREQUAL "binary")
							set(install_destination bin)
							add_executable(${target} ${SOURCE})
							target_link_libraries(${target} ${EXEC_LIBS})
						elseif("${TYPE}" STREQUAL "static-library")
							set(install_destination lib)
							add_library(${target} ${SOURCE})
						elseif("${TYPE}" STREQUAL "shared-library")
							set(install_destination lib)
							add_library(${target} SHARED ${SOURCE})
						endif("${TYPE}" STREQUAL "binary")
						target_include_directories(${target} PUBLIC ${LOCAL_INCLUDE})
						target_link_libraries(${target} ${LOCAL_LIBS} ${GLOBAL_LIBS})
						if("${INSTALL}" STREQUAL "yes")
							install(TARGETS ${target} DESTINATION ${install_destination})
						endif("${INSTALL}" STREQUAL "yes")


						if("${TYPE}" STREQUAL "static-library" OR "${TYPE}" STREQUAL "shared-library")
							add_custom_target(copy_package_${target}_test_data ALL)
							file(GLOB_RECURSE DataFile RELATIVE "${SOURCE_PATH}/test" "${SOURCE_PATH}/test/*.in")
							foreach(input ${DataFile})
								get_filename_component(data_dir ${input} DIRECTORY)
								get_filename_component(data_name ${input} NAME_WE)
								file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/package/${target})
								add_custom_command(TARGET copy_package_${target}_test_data PRE_BUILD
												   COMMAND ${CMAKE_COMMAND} -E 
												   copy ${SOURCE_PATH}/test/${input}
														${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/package/${target})
							endforeach(input ${DataFile})

							file(GLOB_RECURSE unit_tests RELATIVE "${SOURCE_PATH}/test" "${SOURCE_PATH}/test/*.c")
							foreach(test ${unit_tests})
								get_filename_component(test_name ${test} NAME_WE)
								set(test_name "package_${target}_${test_name}")
								set(outdir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_DIR}/package/${target})
								set(source ${SOURCE_PATH}/test/${test})
								file(MAKE_DIRECTORY ${outdir})
								set_source_files_properties(${source} PROPERTIES COMPILE_FLAGS ${CFLAGS})
								add_executable(${test_name} ${source})
								set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${outdir})
								target_compile_definitions(${test_name} PRIVATE TESTDIR=\"${outdir}\" TESTING_CODE=1)
								target_include_directories(${test_name} PUBLIC "${SOURCE_PATH}/include" 
																			   "${CMAKE_CURRENT_SOURCE_DIR}/${TOOL_DIR}/testenv/include" 
																			   ${TEST_INCLUDE})
								target_link_libraries(${test_name} testenv plumber dl ${target} ${TEST_LIBS} ${GLOBAL_LIBS} ${EXEC_LIBS})
								add_test(${test_name} ${outdir}/${test_name})
								set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
							endforeach(test)
						endif("${TYPE}" STREQUAL "static-library" OR "${TYPE}" STREQUAL "shared-library")
					else(build_${target} STREQUAL "yes")
						set(package_status "${package_status} -Dbuild_${target}=no")
					endif(build_${target} STREQUAL "yes")
				endif(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}")
			else(NOT "${build_${target}}" STREQUAL "no")
				set(package_status "${package_status} -Dbuild_${target}=no")
				set(INSTALL no)
				set(TYPE undetermined)
			endif(NOT "${build_${target}}" STREQUAL "no")
			append_pakage_configure(${target} ${TYPE} ${build_${target}} ${INSTALL})
		endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${target}/build.cmake")
	endforeach(target ${targets})
endmacro(build_tool_dir)

build_tool_dir(${LIB_DIR})
build_tool_dir(${TOOL_DIR})
