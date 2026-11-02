# Code shared by the development tools and the tests.
add_library(stym_tools_common STATIC Png.cpp ReadFile.cpp)
target_include_directories(stym_tools_common PUBLIC .)
target_compile_features(stym_tools_common PUBLIC cxx_std_20)
stym_warnings(stym_tools_common)

# Imports of tunes as patterns checked against the tune (ymimport, mymimport).
add_library(stym_tools_import STATIC ImportCheck.cpp)
target_include_directories(stym_tools_import PUBLIC .)
target_link_libraries(stym_tools_import PUBLIC stym_plugincore)
stym_warnings(stym_tools_import)
