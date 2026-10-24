add_executable(ymbench main.cpp)
target_link_libraries(ymbench PRIVATE ymengine)
stym_warnings(ymbench)
