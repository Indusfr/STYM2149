# plugincore: the plugin core without JUCE (parameters, state, MIDI, engine driver) (M3).
add_library(stym_plugincore STATIC
  src/AudioExport.cpp
  src/Parameters.cpp
  src/Mapping.cpp
  src/State.cpp
  src/Driver.cpp
  src/EngineStatus.cpp
  src/Digidrum.cpp
  src/DigidrumBanks.cpp
  src/DmaBanks.cpp
  src/DmaSample.cpp
  src/Json.cpp
  src/Patches.cpp
  src/ProjectFiles.cpp
  src/Instruments.cpp
  src/MidiFiles.cpp
  src/MidiLearn.cpp
  src/Offline.cpp
  src/RegisterExport.cpp
  src/RegisterFiles.cpp
  src/XmImport.cpp
  src/YmTune.cpp)
target_include_directories(stym_plugincore PUBLIC include)
target_link_libraries(stym_plugincore PUBLIC ymengine ymseq ymio)
target_compile_features(stym_plugincore PUBLIC cxx_std_20)
set_target_properties(stym_plugincore PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(stym_plugincore)
