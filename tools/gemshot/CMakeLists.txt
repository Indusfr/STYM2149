# gemshot: scenes of the main window, rendered to PNG and compared with the captures of the prototype.
add_library(stym_gemshot_scenes STATIC Scenes.cpp PrototypeFont.cpp)
target_include_directories(stym_gemshot_scenes PUBLIC .)
target_link_libraries(stym_gemshot_scenes PUBLIC stym_pluginui stym_tools_common)
# The tooltip languages are read from languages/ of the sources, as the plugin reads them where they are installed.
target_compile_definitions(stym_gemshot_scenes PRIVATE STYM_VERSION="${PROJECT_VERSION}"
                           STYM_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/languages")
stym_warnings(stym_gemshot_scenes)

add_executable(gemshot main.cpp)
target_link_libraries(gemshot PRIVATE stym_gemshot_scenes)
stym_warnings(gemshot)
