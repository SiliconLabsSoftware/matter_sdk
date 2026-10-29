add_executable(si91x-rcpd si91x_rcpd.c)
target_compile_options(si91x-rcpd PRIVATE -Wall -Wextra)
set_target_properties(si91x-rcpd PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

install(TARGETS si91x-rcpd RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
