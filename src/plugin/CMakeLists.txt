# STYM2149 plugin (M3): formats per system (D-059), identity and codes (D-060; author and copyright, D-248; vendor PRISM,
# D-334).
if(APPLE)
  set(STYM_FORMATS AU VST3 AAX Standalone)
elseif(WIN32)
  set(STYM_FORMATS VST3 AAX Standalone)
else()
  set(STYM_FORMATS VST3 LV2 Standalone)
endif()

juce_add_plugin(STYM2149
  PRODUCT_NAME "STYM2149"
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "PRISM"
  COMPANY_COPYRIGHT "Copyright © 2026 Indus / PRISM"
  COMPANY_WEBSITE "https://github.com/Indusfr/STYM2149"
  DESCRIPTION "Atari ST YM2149F synthesizer. Programming: Pascal 'Indus' Nowak (with Claude's help)"
  BUNDLE_ID io.github.indusfr.stym2149
  PLUGIN_MANUFACTURER_CODE Inds
  PLUGIN_CODE Stym
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
  LV2URI https://github.com/Indusfr/STYM2149
  # Application icon (D-218): JUCE builds the standalone application's .ico and .icns from the drawn 16 px icon and the
  # 256 px one (the 32 px drawing enlarged); packaging/icon/STYM2149.ico, for the installer, holds the four sizes as
  # drawn (tools/icon/make_ico.py).
  ICON_SMALL "${PROJECT_SOURCE_DIR}/packaging/icon/STYM2149-16.png"
  ICON_BIG "${PROJECT_SOURCE_DIR}/packaging/icon/STYM2149-256.png"
  FORMATS ${STYM_FORMATS})

# Build identifier shown in Fuji > Information (D-211): the CI passes its run number and the commit it builds; a local
# build is "dev", without commit.
set(STYM_BUILD_NUMBER "dev" CACHE STRING "Build number shown in Fuji > Information (the CI run number)")
set(STYM_BUILD_COMMIT "" CACHE STRING "Commit of the build shown in Fuji > Information")
string(SUBSTRING "${STYM_BUILD_COMMIT}" 0 7 STYM_BUILD_COMMIT_SHORT)
configure_file(BuildId.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildId.cpp @ONLY)

target_sources(STYM2149 PRIVATE PluginProcessor.cpp PluginEditor.cpp JucePainter.cpp KeyboardLayout.cpp
                                StandaloneApp.cpp ${CMAKE_CURRENT_BINARY_DIR}/BuildId.cpp)
# Keyboard layout of the system (D-215): Carbon's input sources on macOS, Xlib loaded at run time on Linux.
if(APPLE)
  target_link_libraries(STYM2149 PRIVATE "-framework Carbon")
elseif(UNIX)
  target_link_libraries(STYM2149 PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(STYM2149 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(STYM2149 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_ASIO=$<IF:$<PLATFORM_ID:Windows>,1,0>
  JUCE_STRICT_REFCOUNTEDPOINTER=1
  # The standalone application of StandaloneApp.cpp: its window's close box asks as File > Quit (D-439).
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_link_libraries(STYM2149
  PRIVATE
    stym_plugincore
    stym_pluginui
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags)

# Project warnings on its own code only: the JUCE modules compiled into the target keep the JUCE settings, and their
# headers are system headers for the project's code.
set_source_files_properties(PluginProcessor.cpp PluginEditor.cpp JucePainter.cpp KeyboardLayout.cpp
                            PROPERTIES COMPILE_OPTIONS "${STYM_WARNING_OPTIONS}")
foreach(module IN ITEMS juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra
                        juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors
                        juce_audio_processors_headless juce_audio_utils juce_audio_plugin_client)
  if(TARGET ${module})
    set_target_properties(${module} PROPERTIES SYSTEM TRUE)
  endif()
endforeach()

clap_juce_extensions_plugin(TARGET STYM2149
  CLAP_ID "io.github.indusfr.stym2149"
  CLAP_FEATURES instrument synthesizer)
