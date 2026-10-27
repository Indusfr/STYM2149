# pluginui: the plugin's main window drawn with gemui, without JUCE (STYM2149_SPEC.md §6). The JUCE editor
# (src/plugin) and the capture tool (tools/gemshot) both use it.
add_library(stym_pluginui STATIC
  src/AppIcon.cpp
  src/ArpWindow.cpp
  src/BackgroundJob.cpp
  src/CmdBox.cpp
  src/CurveWindow.cpp
  src/DeskIcons.cpp
  src/EchoWindow.cpp
  src/StepSoundWindow.cpp
  src/HelpWindow.cpp
  src/IconData.cpp
  src/KbdWindow.cpp
  src/KitWindow.cpp
  src/Layout.cpp
  src/MainWindow.cpp
  src/MixerWindow.cpp
  src/ModulationWindow.cpp
  src/PatchesWindow.cpp
  src/InstrumentsWindow.cpp
  src/SequencesPanel.cpp
  src/LfoPanel.cpp
  src/DigidrumWindow.cpp
  src/DmaWindow.cpp
  src/FileMenu.cpp
  src/FujiData.cpp
  src/RecorderWindow.cpp
  src/ScopeWindow.cpp
  src/SeqGrid.cpp
  src/SeqPopups.cpp
  src/SeqWindow.cpp
  src/SettingsWindow.cpp
  src/SongWindow.cpp
  src/TipCatalog.cpp
  src/TipLanguage.cpp
  src/Widgets.cpp
  src/YmPlayerWindow.cpp)
target_include_directories(stym_pluginui PUBLIC include)
target_link_libraries(stym_pluginui PUBLIC gemui stym_plugincore)
target_compile_features(stym_pluginui PUBLIC cxx_std_20)
set_target_properties(stym_pluginui PROPERTIES POSITION_INDEPENDENT_CODE ON)
stym_warnings(stym_pluginui)
