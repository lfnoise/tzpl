add_library(shared INTERFACE)
target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# --- install targets ---

include(GNUInstallDirs)

install(FILES
    jscs_plugin_abi.h
    jscs_simd.hpp
    jscs_random.hpp
    jscs_matrix_transform.hpp
    synthdef_plugin_interface.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jscs
)
