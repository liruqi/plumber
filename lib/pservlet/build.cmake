set(TYPE static-library)
set(LOCAL_CFLAGS "-fPIC")
set(LOCAL_LIBS plumber)
set(INSTALL "yes")
install_includes("${SOURCE_PATH}/include" "include/pservlet" "*.h")
install_plumber_headers("include/pservlet")
