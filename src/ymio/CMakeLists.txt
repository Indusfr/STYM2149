# ymio: file formats. Reading YM2/YM3/YM3b/YM5/YM6 (LHA-compressed or not) and frame player with timer effects;
# reading SNDH files through the AtariAudio Atari machine, Ice-packed or not; standard MIDI files;
# WAV and ZIP exports, written as the render goes; writing VBL, YM3, YM6 and SNDH dumps;
# reading XM modules.
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
