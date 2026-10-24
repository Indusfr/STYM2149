# ymcore: YM2149F chip at clk/8, measured mixing table, timestamped writes, output chain.
# Pure C++20, no dependency (apart from the AtariAudio table header).
add_library(ymcore STATIC
  src/Ym2149.cpp
  src/Tables.cpp
  src/SincResampler.cpp
  src/ChipRenderer.cpp
  src/BoardFilters.cpp
  src/TickStreamOutput.cpp)
target_include_directories(ymcore PUBLIC include)
target_link_libraries(ymcore PRIVATE stym_atariaudio_tables)
target_compile_features(ymcore PUBLIC cxx_std_20)
set_target_properties(ymcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(ymcore)
