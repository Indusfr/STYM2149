# ymseq: sequencer model (patterns, edits, Song, undo) (M5).
add_library(ymseq STATIC
  src/Tables.cpp
  src/Pattern.cpp
  src/Edit.cpp
  src/Euclid.cpp
  src/Song.cpp
  src/Project.cpp
  src/Timing.cpp
  src/Commands.cpp)
target_include_directories(ymseq PUBLIC include)
target_link_libraries(ymseq PUBLIC ymengine)
target_compile_features(ymseq PUBLIC cxx_std_20)
set_target_properties(ymseq PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(ymseq)
