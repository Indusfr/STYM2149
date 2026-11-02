add_executable(mymimport main.cpp)
target_link_libraries(mymimport PRIVATE stym_tools_import stym_tools_common)
stym_warnings(mymimport)
