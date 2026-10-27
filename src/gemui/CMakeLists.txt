# gemui: GEM widgets drawn pixel by pixel into a framebuffer, without JUCE (STYM2149_SPEC.md §6).
add_library(gemui STATIC
  src/Alert.cpp
  src/Color.cpp
  src/Controls.cpp
  src/Desktop.cpp
  src/Font.cpp
  src/FontData.cpp
  src/FontDataSmall.cpp
  src/Menu.cpp
  src/Painter.cpp
  src/Scroll.cpp
  src/Surface.cpp
  src/Widget.cpp)
target_include_directories(gemui PUBLIC include)
target_compile_features(gemui PUBLIC cxx_std_20)
set_target_properties(gemui PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(gemui)
