add_executable(ymimport main.cpp)
target_link_libraries(ymimport PRIVATE stym_tools_import stym_tools_common)
stym_warnings(ymimport)
