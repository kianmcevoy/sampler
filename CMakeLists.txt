cmake_minimum_required(VERSION 3.22)

project(
    # Project name
    sampler

    # Version number
    VERSION 0.1.0

    # Enable languages
    LANGUAGES C CXX
)

set(EXECUTABLE_NAME "sampler")

set(CMAKE_CXX_STANDARD 23)
if (APPLE)
    # On Mac, we need to wait for a new JUCE version that fixes the compilation issue
    set(CMAKE_CXX_STANDARD 20)
endif()


add_compile_definitions(SYSTEM_JUCE)

# Instrument directory
set(INSTRUMENT_DIR ${PROJECT_SOURCE_DIR}/instrument)
# Hardware directory
set(GUI_DIR ${PROJECT_SOURCE_DIR}/gui)
# System directory
set(SYSTEM_DIR ${PROJECT_SOURCE_DIR}/system)
# Interface directory
set(INTERFACE_DIR ${PROJECT_SOURCE_DIR}/interface)
# Background directory
set(BACKGROUND_DIR ${PROJECT_SOURCE_DIR}/background)
# ISL directory
set(ISL_DIR ${PROJECT_SOURCE_DIR}/isl/)

# JUCE framework
add_subdirectory(${SYSTEM_DIR}/JUCE)

set_directory_properties(PROPERTIES JUCE_COMPANY_NAME "Instruo")
set_directory_properties(PROPERTIES JUCE_COMPANY_WEBSITE "https://www.instruomodular.com/")

set(PLATFORM_IS_MACOS 0)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_IS_MACOS 1)
    message(STATUS "Configuring for MacOS")
    if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
        set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)
        foreach(macos_arch ${CMAKE_OSX_ARCHITECTURES})
            message(STATUS "Configuring for MacOS architechture ${macos_arch}")
        endforeach()
    endif()
endif()
set(PLATFORM_IS_WINDOWS 0)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    set(PLATFORM_IS_WINDOWS 1)
    message(STATUS "Configuring for Windows")
endif()
set(PLATFORM_IS_LINUX 0)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_IS_LINUX 1)
    message(STATUS "Configuring for Linux")
endif()
set(PLATFORM_IS_ANDROID 0)
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_IS_ANDROID 1)
    message(STATUS "Configuring for Android (ABI: ${ANDROID_ABI}, API: ${CMAKE_SYSTEM_VERSION})")
    # Android target lives under /home/kian/instruo/sampler/android/, driven
    # by Gradle which invokes this CMakeLists via externalNativeBuild.
endif()

set(TARGET_NAME ${PROJECT_NAME})
string(REPLACE " " "" EXECUTABLE_NAME_STRIPPED ${EXECUTABLE_NAME})

if (${PLATFORM_IS_ANDROID})
    # Android can only produce a Standalone (SHARED .so packaged in an APK
    # by Gradle). VST3 / AU are out of scope for the Android port.
    set(SAMPLER_FORMATS Standalone)
else()
    set(SAMPLER_FORMATS VST3 Standalone)
endif()

juce_add_plugin(${TARGET_NAME}
    PRODUCT_NAME "${EXECUTABLE_NAME}"
    VERSION ${PROJECT_VERSION}
    BUNDLE_ID "com.Instruo.${EXECUTABLE_NAME_STRIPPED}"
    BACKGROUND_AUDIO_ENABLED TRUE
    # ICON_BIG "${PROJECT_SOURCE_DIR}/gui/assets/branding/${EXECUTABLE_NAME} icon.png"
    FORMATS ${SAMPLER_FORMATS}
    PLUGIN_MANUFACTURER_CODE Inst # change this
    PLUGIN_CODE Prot # "PROTotyper"
    DESCRIPTION "JUCE-based prototyping framework for Instruo DSP projects"
    IS_SYNTH FALSE # may change this
    NEEDS_MIDI_INPUT TRUE # may change this
    NEEDS_MIDI_OUTPUT TRUE # may change this
)

set(APP_TARGET ${TARGET_NAME}_Standalone)
set(VST3_TARGET ${TARGET_NAME}_VST3)

# On Android the Standalone target is a SHARED library loaded by Java via
# `System.loadLibrary("juce_jni")` (see Java.java in juce_core). Override
# the output name so the resulting .so matches JUCE's hardcoded load name.
#
# Also force default symbol visibility on the Standalone target so the
# `juce_CreateApplication` extern "C" entry point declared by JUCE's
# Standalone plugin client is exported from the .so. JUCE's Android event
# bootstrap (juce_juceEventsAndroidStartApp in juce_Messaging_android.cpp)
# does a runtime `dlsym("juce_CreateApplication")` on the loaded .so; with
# JUCE's default -fvisibility=hidden the symbol is hidden and the lookup
# silently returns null — so the app process starts but the JUCE main
# window is never constructed, presenting a blank screen.
if (${PLATFORM_IS_ANDROID})
    set_target_properties(${APP_TARGET} PROPERTIES OUTPUT_NAME "juce_jni")
    # Force `juce_CreateApplication` to be exported despite JUCE's per-source
    # -fvisibility=hidden. JUCE's Android event bootstrap
    # (juce_juceEventsAndroidStartApp in juce_Messaging_android.cpp) does a
    # runtime `dlsym("juce_CreateApplication")` on the .so; if the symbol is
    # hidden, the lookup returns null and the JUCE app never starts. We tell
    # JUCE to skip its built-in definition (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP)
    # and provide our own in system/src/android_create_application.cpp with
    # an explicit `__attribute__((visibility("default")))`.
    #
    # The macro must be defined on BOTH the shared-code target (where our
    # replacement lives) and the Standalone wrapper target (where the JUCE
    # module file lives) so JUCE skips its own definition in both compilation
    # units and we don't end up with a duplicate symbol.
    target_compile_definitions(${TARGET_NAME} PUBLIC
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    # The replacement lives in the shared-code static archive; the linker
    # would drop it via dead-code elimination because nothing inside the
    # Standalone wrapper references it directly. `--undefined` (a.k.a.
    # `-u`) tells the linker to treat the symbol as if it were used, which
    # pulls the .o out of the archive into the final .so.
    target_link_options(${APP_TARGET} PRIVATE
        "LINKER:--undefined=juce_CreateApplication")
endif()

file(GLOB TARGET_SRCS
    ${SYSTEM_DIR}/src/*.cpp
    ${INSTRUMENT_DIR}/src/*.cpp
    ${GUI_DIR}/src/*.cpp
    ${INTERFACE_DIR}/src/*.cpp
    ${ISL_DIR}/src/igui/*.cpp
)
target_sources(${TARGET_NAME} PRIVATE ${TARGET_SRCS})

target_include_directories(${TARGET_NAME} PRIVATE
    # Project include
    ${PROJECT_SOURCE_DIR}/include
    ${SYSTEM_DIR}/include
    ${INSTRUMENT_DIR}/include
    ${GUI_DIR}/include
    ${INTERFACE_DIR}/include
    ${BACKGROUND_DIR}/include
    ${ISL_DIR}/include
)

target_compile_definitions(${TARGET_NAME}
    PUBLIC DONT_SET_USING_JUCE_NAMESPACE=1
    Sample=float
    SOFTWARE_VERSION_STRING="${PROJECT_VERSION}"
    SOFTWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SOFTWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SOFTWARE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if (${PLATFORM_IS_ANDROID})
    # JUCE's JNI bindings for the Android Activity lifecycle (appOnResume,
    # appNewIntent) live behind JUCE_PUSH_NOTIFICATIONS_ACTIVITY. Without
    # this define the C++ side never registers the native methods declared
    # in com.rmsl.juce.JuceActivity, and the app crashes on first resume
    # with UnsatisfiedLinkError. The class path here mirrors the value used
    # in JUCE's AudioPluginHost example.
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            JUCE_PUSH_NOTIFICATIONS=1
            JUCE_PUSH_NOTIFICATIONS_ACTIVITY="com/rmsl/juce/JuceActivity"
    )
endif()
juce_generate_juce_header(${TARGET_NAME})

set(PROJECT_VERSION_STRING "v${PROJECT_VERSION}")
message(STATUS "Project version is ${PROJECT_VERSION_STRING}")

message(STATUS "CMake build type is ${CMAKE_BUILD_TYPE}")
set(BUILD_TARGET_BUNDLE_PATH_BASE "${PROJECT_SOURCE_DIR}/build/${TARGET_NAME}_artefacts/${CMAKE_BUILD_TYPE}")

set(VST3_BUILD_PATH "${BUILD_TARGET_BUNDLE_PATH_BASE}/VST3")
set(APP_BUILD_PATH "${BUILD_TARGET_BUNDLE_PATH_BASE}/Standalone")

set(VST3_BUNDLE_NAME "${EXECUTABLE_NAME}.vst3")
set(APP_BUNDLE_NAME "${EXECUTABLE_NAME}.app")
set(BUNDLE_NAMES
    "${VST3_BUNDLE_NAME}"
    "${APP_BUNDLE_NAME}"
)

set(VST3_BUNDLE_PATH "${VST3_BUILD_PATH}/${VST3_BUNDLE_NAME}")
set(APP_BUNDLE_PATH "${APP_BUILD_PATH}/${APP_BUNDLE_NAME}")

set(VST3_CONTENTS_PATH "${VST3_BUNDLE_PATH}/Contents")
set(APP_CONTENTS_PATH "${APP_BUNDLE_PATH}/Contents")

if (${PLATFORM_IS_MACOS})
    set(BINARY_FOLDER_NAME "MacOS")
    set(VST3_BINARY_NAME "${EXECUTABLE_NAME}.so")
    set(APP_BINARY_NAME "${EXECUTABLE_NAME}")
endif()

if (${PLATFORM_IS_WINDOWS})
    set(BINARY_FOLDER_NAME "x86_64-win")
    set(VST3_BINARY_NAME "${EXECUTABLE_NAME}.dll")
    set(APP_BINARY_NAME "${EXECUTABLE_NAME}.exe")
endif()

if (${PLATFORM_IS_LINUX})
    set(BINARY_FOLDER_NAME "x86_64-linux")
    set(VST3_BINARY_NAME "${EXECUTABLE_NAME}.so")
    set(APP_BINARY_NAME "${EXECUTABLE_NAME}")
endif()

set(VST3_BINARY_FOLDER_PATH "${VST3_CONTENTS_PATH}/${BINARY_FOLDER_NAME}")
set(APP_BINARY_FOLDER_PATH "${APP_CONTENTS_PATH}/${BINARY_FOLDER_NAME}")

set(VST3_BINARY_PATH "${VST3_BINARY_FOLDER_PATH}/${VST3_BINARY_NAME}")
set(APP_BINARY_PATH "${APP_BINARY_FOLDER_PATH}/${APP_BINARY_NAME}")

if (${PLATFORM_IS_WINDOWS} OR ${PLATFORM_IS_LINUX})
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_PATH}"
    )
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_CONTENTS_PATH}"
    )
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BINARY_FOLDER_PATH}"
    )

    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${APP_BUILD_PATH}/${APP_BINARY_NAME}" "${APP_BINARY_PATH}"
    )
endif()

if (NOT ${PLATFORM_IS_ANDROID})
    set(VST3_RESOURCES_PATH "${VST3_CONTENTS_PATH}/Resources")
    set(APP_RESOURCES_PATH "${APP_CONTENTS_PATH}/Resources")
    add_custom_command(TARGET ${VST3_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_RESOURCES_PATH}"
    )
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCES_PATH}"
    )

    set(VST3_GUI_RESOURCES_PATH "${VST3_RESOURCES_PATH}/gui")
    set(APP_GUI_RESOURCES_PATH "${APP_RESOURCES_PATH}/gui")
    add_custom_command(TARGET ${VST3_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_GUI_RESOURCES_PATH}"
    )
    add_custom_command(TARGET ${APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_GUI_RESOURCES_PATH}"
    )

    set(RESOURCES_TO_COPY
        "gui/assets"
        "gui/fonts"
    )
    foreach(resource ${RESOURCES_TO_COPY})
        add_custom_command(TARGET ${VST3_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_RESOURCES_PATH}/${resource}"
        )
        add_custom_command(TARGET ${APP_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCES_PATH}/${resource}"
        )

        add_custom_command(TARGET ${VST3_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/${resource}" "${VST3_RESOURCES_PATH}/${resource}"
        )
        add_custom_command(TARGET ${APP_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/${resource}" "${APP_RESOURCES_PATH}/${resource}"
        )
    endforeach()
endif()

# Links to all necessary dependencies. The present ones are recommended by JUCE.
# If you use one of the additional modules, like the DSP module, you need to specify it here.
target_link_libraries(${TARGET_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_osc
        juce::juce_animation
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# These definitions are recommended by JUCE.
target_compile_definitions(${TARGET_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
)

# Enables all warnings and treats warnings as errors.
# This needs to be set up only for your projects, not 3rd party
if (MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /Wall )
    # Silences some deprecation warnings that (hopefully) will be fixed in a future release of JUCE.
    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -w -O3)#Wall -Wextra -Wpedantic)
endif()

# In Visual Studio this command provides a nice grouping of source files in "filters".
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/..)


set(BUILD_RELEASE_PACKAGES 0)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(BUILD_RELEASE_PACKAGES 1)
endif()

if (BUILD_RELEASE_PACKAGES)
    message(STATUS "Configuring release packages")

    set(CODESIGN_IDENTITY "Instruo Limited")

    set(PACKAGE_TARGET InstallerPackage)
    add_custom_target(${PACKAGE_TARGET} ALL)
    add_dependencies(${PACKAGE_TARGET} ${APP_TARGET} ${VST3_TARGET})

    if (${PLATFORM_IS_MACOS})
        # Codesigning stuff
        message(STATUS "Using Apple codesigning identity '${CODESIGN_IDENTITY}'")

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --remove-signature "${APP_BUNDLE_PATH}"
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --remove-signature "${VST3_BUNDLE_PATH}"
        )

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --sign "${CODESIGN_IDENTITY}" --timestamp --options runtime "${APP_BUNDLE_PATH}"
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --sign "${CODESIGN_IDENTITY}" --timestamp "${VST3_BUNDLE_PATH}"
        )
    endif()

    set(INSTALLER_PARENT_PATH "${PROJECT_SOURCE_DIR}/build/Installer")
    set(INSTALLER_FOLDER_PATH "${INSTALLER_PARENT_PATH}/${EXECUTABLE_NAME}")

    add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${INSTALLER_PARENT_PATH}/*"
        COMMENT "Removing existing Installer Package"
    )
    add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_FOLDER_PATH}"
        COMMENT "Building Installer Package"
    )

    foreach(bundle ${BUNDLE_NAMES})
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_FOLDER_PATH}/${bundle}"
        )
    endforeach()

    if (${PLATFORM_IS_MACOS})
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ditto "${VST3_BUNDLE_PATH}" ${INSTALLER_FOLDER_PATH}/${VST3_BUNDLE_NAME}
            COMMENT "Copying VST3 bundle to Installer..."
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ditto "${APP_BUNDLE_PATH}" ${INSTALLER_FOLDER_PATH}/${APP_BUNDLE_NAME}
            COMMENT "Copying App bundle to Installer..."
        )
    else()
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${VST3_BUNDLE_PATH}" ${INSTALLER_FOLDER_PATH}/${VST3_BUNDLE_NAME}
            COMMENT "Copying VST3 bundle to Installer..."
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_BUNDLE_PATH}" ${INSTALLER_FOLDER_PATH}/${APP_BUNDLE_NAME}
            COMMENT "Copying App bundle to Installer..."
        )
    endif()

    if (${PLATFORM_IS_MACOS})
        # Create aliases to /Applications and /Library/Audio/Plug-ins/VST3
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ln -s "/Applications" "${INSTALLER_FOLDER_PATH}/Applications"
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ln -s "/Library/Audio/Plug-Ins/VST3" "${INSTALLER_FOLDER_PATH}/VST3 Library"
        )
    endif()

    if (${PLATFORM_IS_MACOS})
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/installers/readmes/readme_macos.txt" "${INSTALLER_FOLDER_PATH}/README.txt"
        )
    endif()
    if (${PLATFORM_IS_WINDOWS})
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/installers/readmes/readme_windows.txt" "${INSTALLER_FOLDER_PATH}/README.txt"
        )
    endif()
    if (${PLATFORM_IS_LINUX})
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/installers/readmes/readme_linux.txt" "${INSTALLER_FOLDER_PATH}/README.txt"
        )
    endif()

    set(INSTALLER_NAME "${EXECUTABLE_NAME_STRIPPED}-${PROJECT_VERSION_STRING}")
    if (${PLATFORM_IS_MACOS})
        set (INSTALLER_NAME "${INSTALLER_NAME}-macOS-universal")
    endif()
    if (${PLATFORM_IS_WINDOWS})
        set (INSTALLER_NAME "${INSTALLER_NAME}-windows")
    endif()
    if (${PLATFORM_IS_LINUX})
        set (INSTALLER_NAME "${INSTALLER_NAME}-linux")
    endif()

    if (${PLATFORM_IS_MACOS})
        foreach(bundle ${BUNDLE_NAMES})
            add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
                COMMAND codesign --verify --deep --strict "${INSTALLER_FOLDER_PATH}/${bundle}"
            )
        endforeach()

        set(INSTALLER_DMG_PATH "${INSTALLER_PARENT_PATH}/${INSTALLER_NAME}.dmg")
        set(INSTALLER_ARCHIVE_PATH "${INSTALLER_DMG_PATH}.zip")

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND hdiutil create -srcFolder "${INSTALLER_FOLDER_PATH}" -o "${INSTALLER_DMG_PATH}"
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --sign "${CODESIGN_IDENTITY}" --timestamp -i "Instruo ${EXECUTABLE_NAME}" "${INSTALLER_DMG_PATH}"
        )
        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND codesign --verify --deep --strict "${INSTALLER_DMG_PATH}"
        )

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ditto -c -k "${INSTALLER_DMG_PATH}" "${INSTALLER_ARCHIVE_PATH}"
        )

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND xcrun notarytool submit "${INSTALLER_ARCHIVE_PATH}" --keychain-profile "notarytool-password" --wait
        )
    endif()

    if (${PLATFORM_IS_WINDOWS})
        set(INSTALLER_ARCHIVE_PATH "${INSTALLER_PARENT_PATH}/${INSTALLER_NAME}")

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${INSTALLER_FOLDER_PATH}" "${INSTALLER_ARCHIVE_PATH}"
        )
    endif()

    if (${PLATFORM_IS_LINUX})
        set(INSTALLER_ARCHIVE_PATH "${INSTALLER_PARENT_PATH}/${INSTALLER_NAME}.tar.gz")

        add_custom_command(TARGET ${PACKAGE_TARGET} POST_BUILD
            COMMAND cd "${INSTALLER_FOLDER_PATH}" && tar czf "${INSTALLER_ARCHIVE_PATH}" "*"
        )
    endif()
endif()
