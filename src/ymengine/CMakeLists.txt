# ymengine: MFP timers, timer effects, voices, 3-channel / dual-chip / 16-channel modes (M2), playback of the sequencer
# tracks (M5).
add_library(ymengine STATIC
  src/Mfp68901.cpp
  src/VirtualChannel.cpp
  src/DrumKit.cpp
  src/Pitch.cpp
  src/Engine.cpp
  src/Sequence.cpp
  src/Sequencer.cpp)
target_include_directories(ymengine PUBLIC include)
target_link_libraries(ymengine PUBLIC ymcore)
target_compile_features(ymengine PUBLIC cxx_std_20)
set_target_properties(ymengine PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(ymengine)
# Bit-for-bit parity with the prototype (JavaScript, no FMA): no contraction of a·b + c into an FMA (clang enables
# it by default on arm64). MSVC does not contract with /fp:precise.
if(NOT MSVC)
  target_compile_options(ymengine PRIVATE -ffp-contract=off)
endif()
