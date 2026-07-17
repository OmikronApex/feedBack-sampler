# Build-time enforcement of the core/shell rule (AD-6):
# sampler-core may link JUCE non-GUI modules internally, but must never
# depend on a GUI module. Configure fails hard if one shows up.

function(fbsampler_assert_no_gui_modules target)
    set(_forbidden juce_gui_basics juce_gui_extra juce_graphics)

    get_target_property(_link_libs ${target} LINK_LIBRARIES)
    get_target_property(_iface_libs ${target} INTERFACE_LINK_LIBRARIES)
    set(_all_libs "")
    if(_link_libs)
        list(APPEND _all_libs ${_link_libs})
    endif()
    if(_iface_libs)
        list(APPEND _all_libs ${_iface_libs})
    endif()

    foreach(_lib IN LISTS _all_libs)
        foreach(_gui IN LISTS _forbidden)
            if(_lib MATCHES "${_gui}")
                message(FATAL_ERROR
                    "AD-6 violation: '${target}' links GUI module '${_lib}'. "
                    "sampler-core must stay free of JUCE GUI dependencies.")
            endif()
        endforeach()
    endforeach()
endfunction()
