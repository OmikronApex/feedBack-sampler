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

target_sources(feedback-sampler-plugin
    PRIVATE
        plugin_processor.cpp
        plugin_editor.cpp
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
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
