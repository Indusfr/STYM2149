add_executable(ym2wav main.cpp)
target_link_libraries(ym2wav PRIVATE ymio stym_tools_common)
stym_warnings(ym2wav)
