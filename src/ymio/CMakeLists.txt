# ymio: file formats. Reading YM2/YM3/YM3b/YM5/YM6 (LHA-compressed or not) and frame player with timer effects
# (M2); reading SNDH files through the AtariAudio Atari machine, Ice-packed or not (M7, D-159); standard MIDI files
# (M7, D-170); WAV and ZIP exports, written as the render goes (M7, A4); writing VBL, YM3, YM6 and SNDH dumps (M7, A5);
# reading XM modules (M7, B5).
find_package(Threads REQUIRED)
add_library(ymio STATIC
  src/ByteSink.cpp
  src/Ice.cpp
  src/Lzh.cpp
  src/LzhPack.cpp
  src/Smf.cpp
  src/Sndh.cpp
  src/SndhWriter.cpp
  src/Wav.cpp
  src/Xm.cpp
  src/YmFile.cpp
  src/YmPlayer.cpp
  src/YmWriter.cpp
  src/Zip.cpp
  src/Ym2Drums.cpp)
target_include_directories(ymio PUBLIC include)
target_link_libraries(ymio PUBLIC ymcore ymengine Threads::Threads PRIVATE stym_atariaudio_tables stym_atariaudio_machine)
set_target_properties(ymio PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(ymio)
