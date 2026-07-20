juce_add_plugin(feedback-sampler-plugin
    PRODUCT_NAME "feedBack Sampler"          # OQ-2: working name placeholder
    COMPANY_NAME "feedBack"
    BUNDLE_ID com.feedback.feedback-sampler
    PLUGIN_MANUFACTURER_CODE Fdbk
    PLUGIN_CODE Fbsm
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
)

# Rubik variable font (weights 300-900) + OFL license, embedded as binary
# data. License text lives at ui/fonts/OFL.txt for the NFR6 audit (7.4).
juce_add_binary_data(fbsampler-fonts
    HEADER_NAME FbFontsData.h
    NAMESPACE FbFonts
    SOURCES
        ui/fonts/Rubik-wght.ttf
)
set_target_properties(fbsampler-fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(FBSAMPLER_UI_SOURCES
    ui/fb_look_and_feel.cpp
    ui/fb_knob.cpp
    ui/library_filter.cpp
    ui/library_browser_pane.cpp
    ui/control_card_model.cpp
    ui/header_bar.cpp
    ui/instrument_view_pane.cpp
    ui/error_copy.cpp
    ui/drop_action.cpp
)

# Depends on PluginProcessor — plugin + tests only, never the gallery.
set(FBSAMPLER_UI_PROCESSOR_SOURCES
    ui/settings_overlay.cpp
)

target_sources(feedback-sampler-plugin
    PRIVATE
        plugin_processor.cpp
        plugin_editor.cpp
        ${FBSAMPLER_UI_SOURCES}
        ${FBSAMPLER_UI_PROCESSOR_SOURCES}
)

target_compile_definitions(feedback-sampler-plugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(feedback-sampler-plugin
    PRIVATE
        sampler-core
        fbsampler-fonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Component gallery: throwaway dev target (Story 3.1) — links the plugin UI
# sources directly, never the plugin target. OFF by default; CI skips it.
option(FBSAMPLER_BUILD_GALLERY "Build the component gallery dev app" OFF)
if(FBSAMPLER_BUILD_GALLERY)
    juce_add_gui_app(fbsampler-gallery
        PRODUCT_NAME "fbsampler-gallery"
    )
    target_sources(fbsampler-gallery
        PRIVATE
            gallery_main.cpp
            ${FBSAMPLER_UI_SOURCES}
    )
    target_compile_definitions(fbsampler-gallery
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(fbsampler-gallery
        PRIVATE
            sampler-core
            fbsampler-fonts
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
