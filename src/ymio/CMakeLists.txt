# ymio: file formats. Reading YM2/YM3/YM3b/YM5/YM6 (LHA-compressed or not) and frame player with timer effects;
# reading SNDH files through the AtariAudio Atari machine, Ice-packed or not; standard MIDI files;
# WAV and ZIP exports, written as the render goes; writing VBL, YM3, YM6 and SNDH dumps;
# reading XM modules, MOD modules, S3M modules and Oktalyzer modules; reading and writing maxYMiser songs and voicesets and
# YM-Tracker songs; reading and writing TTrak modules and instruments.
find_package(Threads REQUIRED)
add_library(ymio STATIC
  src/ByteSink.cpp
  src/Gzip.cpp
  src/Ice.cpp
  src/Lzh.cpp
  src/LzhPack.cpp
  src/MaxYm.cpp
  src/MaxYmPlayer.cpp
  src/Mod.cpp
  src/Okt.cpp
  src/S3m.cpp
  src/Smf.cpp
  src/Sndh.cpp
  src/SndhWriter.cpp
  src/TTrak.cpp
  src/TTrakPlayer.cpp
  src/Wav.cpp
  src/Xm.cpp
  src/YmFile.cpp
  src/YmPlayer.cpp
  src/YmTracker.cpp
  src/YmWriter.cpp
  src/Zip.cpp
  src/Ym2Drums.cpp)
target_include_directories(ymio PUBLIC include)
target_link_libraries(ymio PUBLIC ymcore ymengine Threads::Threads PRIVATE stym_atariaudio_tables stym_atariaudio_machine)
set_target_properties(ymio PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(ymio)
