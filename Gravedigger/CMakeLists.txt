cmake_minimum_required(VERSION 3.22)
project(Gravedigger VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static MSVC runtime: the plugin works without installing VC++ redistributables
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        master
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ---- fonts (SIL Open Font License, from github.com/google/fonts). Falls back to system fonts if offline.
set(GD_FONT_DIR ${CMAKE_BINARY_DIR}/fonts)
file(MAKE_DIRECTORY ${GD_FONT_DIR})
function(gd_get_font url name)
    set(dst ${GD_FONT_DIR}/${name})
    if(NOT EXISTS ${dst})
        file(DOWNLOAD ${url} ${dst} STATUS st TLS_VERIFY ON)
        list(GET st 0 code)
        if(NOT code EQUAL 0)
            message(WARNING "Font ${name} could not be downloaded, using system font instead")
            file(WRITE ${dst} "x")
        endif()
    endif()
endfunction()
gd_get_font("https://raw.githubusercontent.com/google/fonts/main/ofl/unifrakturmaguntia/UnifrakturMaguntia-Book.ttf" Fraktur.ttf)
gd_get_font("https://raw.githubusercontent.com/google/fonts/main/ofl/oswald/Oswald%5Bwght%5D.ttf" Oswald.ttf)
gd_get_font("https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/IBMPlexMono-Medium.ttf" PlexMono.ttf)

set(GD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GD_FORMATS AU)
endif()

juce_add_plugin(Gravedigger
    COMPANY_NAME "Gravedigger Records"
    BUNDLE_ID com.gravedigger.plugin
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Grvd
    PLUGIN_CODE Gdg1
    FORMATS ${GD_FORMATS}
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    PRODUCT_NAME "Gravedigger")

# sample library: Versilian Community Sample Library (VCSL), CC0 1.0
file(GLOB GD_LIBRARY_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Library/*.ogg ${CMAKE_CURRENT_SOURCE_DIR}/Library/library.json)

juce_add_binary_data(GdData SOURCES
    ${GD_FONT_DIR}/Fraktur.ttf
    ${GD_FONT_DIR}/Oswald.ttf
    ${GD_FONT_DIR}/PlexMono.ttf
    ${GD_LIBRARY_FILES})

target_sources(Gravedigger PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Gravedigger PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_OGGVORBIS=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Gravedigger
    PRIVATE
        GdData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
