add_library(shared INTERFACE)
target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(shared INTERFACE TZPL_SHARED_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# --- install targets ---

include(GNUInstallDirs)

install(FILES
    tzpl_plugin_abi.h
    tempo_ramp.hpp
    tzpl_simd.hpp
    tzpl_random.hpp
    tzpl_matrix_transform.hpp
    tzpl_voicer.hpp
    tzpl_fft.hpp
    synthdef_plugin_interface.hpp
    synthdef_types.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tzpl
)
