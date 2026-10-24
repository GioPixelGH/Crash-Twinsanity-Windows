# The native build (macOS, Linux, Windows with LLVM-MinGW's clang): the game's C++ for the host with src/platform/native/ as its platform layer.
#
#   cmake -S native -B build/native/cmake -G Ninja && cmake --build build/native/cmake
#
# The retail data comes from the split as C++ (native/tools/convert_data.py writes build/native/data.cpp; this build runs it).
# See native/NATIVE.md.
cmake_minimum_required(VERSION 3.24)
project(TwinsanityNative VERSION 1.0.16 LANGUAGES CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# C++26 like the PS2 build's gnu++26 (CMake doesn't know AppleClang has it and would fall back to C++23)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Line tables only: Apple clang 21 with full debug info (-g) drops the asm labels (RETAIL) of explicit specializations' members
# (game/pools.h's), whose callers then don't link. File and line in backtraces is what debugging the port needs
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -gline-tables-only -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The movies' MPEG-2 pictures (src/platform/native/movie/): FFmpeg's libavcodec and libavutil. Our own LGPL 2.1, decode-only
# build (native/tools/build_ffmpeg.sh, in build/native/ffmpeg/<os>-<arch>) when it's there, else the system's pkg-config one
# (Homebrew's is GPL 3: an app carrying it can't be shared under the LGPL). See native/third_party/ffmpeg/README.md
if(WIN32)
    # Windows: a prebuilt LGPL shared FFmpeg (BtbN's FFmpeg-Builds "win64-lgpl-shared": include/, lib/*.dll.a, bin/*.dll), no
    # pkg-config. native/WINDOWS.md
    # Our own decode-only LGPL 2.1 build (native/tools/build_ffmpeg.sh in Git Bash) when it's there, as on the other systems
    set(TWIN_FFMPEG_OWN ${ROOT}/build/native/ffmpeg/windows-x86_64)
    if(EXISTS ${TWIN_FFMPEG_OWN}/include/libavcodec/avcodec.h)
        set(TWIN_FFMPEG_DEFAULT ${TWIN_FFMPEG_OWN})
    else()
        set(TWIN_FFMPEG_DEFAULT "")
    endif()
    set(TWIN_FFMPEG_DIR "${TWIN_FFMPEG_DEFAULT}" CACHE PATH "The FFmpeg build to link (include/, lib/libavcodec.dll.a, bin/avcodec-*.dll)")
    if(NOT EXISTS "${TWIN_FFMPEG_DIR}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "FFmpeg: set TWIN_FFMPEG_DIR to an LGPL shared build (native/WINDOWS.md)")
    endif()
    add_library(TwinFFmpeg INTERFACE)
    target_include_directories(TwinFFmpeg INTERFACE "${TWIN_FFMPEG_DIR}/include")
    target_link_libraries(TwinFFmpeg INTERFACE "${TWIN_FFMPEG_DIR}/lib/libavcodec.dll.a" "${TWIN_FFMPEG_DIR}/lib/libavutil.dll.a")
    add_library(PkgConfig::FFMPEG ALIAS TwinFFmpeg)
    file(GLOB TWIN_FFMPEG_DLLS "${TWIN_FFMPEG_DIR}/bin/avcodec-*.dll" "${TWIN_FFMPEG_DIR}/bin/avutil-*.dll"
         "${TWIN_FFMPEG_DIR}/bin/swresample-*.dll")
    message(STATUS "FFmpeg: ${TWIN_FFMPEG_DIR}")
else()
find_package(PkgConfig REQUIRED)
if(APPLE)
    set(TWIN_FFMPEG_OS macos)
else()
    string(TOLOWER ${CMAKE_SYSTEM_NAME} TWIN_FFMPEG_OS)
endif()
list(LENGTH CMAKE_OSX_ARCHITECTURES TWIN_ARCHITECTURES)
if(TWIN_ARCHITECTURES EQUAL 1)
    set(TWIN_FFMPEG_ARCH ${CMAKE_OSX_ARCHITECTURES})
else()
    set(TWIN_FFMPEG_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()
if(TWIN_FFMPEG_ARCH STREQUAL "aarch64")
    set(TWIN_FFMPEG_ARCH arm64)
endif()
set(TWIN_FFMPEG_DIR ${ROOT}/build/native/ffmpeg/${TWIN_FFMPEG_OS}-${TWIN_FFMPEG_ARCH} CACHE PATH
    "The FFmpeg build to link (native/tools/build_ffmpeg.sh's install folder); the system's when it has no lib/pkgconfig")
if(EXISTS ${TWIN_FFMPEG_DIR}/lib/pkgconfig/libavcodec.pc)
    set(TWIN_FFMPEG_CHOICE ${TWIN_FFMPEG_DIR})
    # Only ours: pkg-config looks nowhere else
    set(ENV{PKG_CONFIG_LIBDIR} ${TWIN_FFMPEG_DIR}/lib/pkgconfig)
    set(ENV{PKG_CONFIG_PATH} "")
else()
    set(TWIN_FFMPEG_CHOICE system)
    message(WARNING "FFmpeg: no LGPL build in ${TWIN_FFMPEG_DIR} (native/tools/build_ffmpeg.sh makes it); using the "
        "system's, which may be GPL (Homebrew's is GPL 3, with x264 and x265): fine to play, not to share the app")
endif()
# pkg-config's answer and the libraries' paths are cached: found again when the choice changed
if(NOT "${TWIN_FFMPEG_CHOSEN}" STREQUAL "${TWIN_FFMPEG_CHOICE}")
    set(FFMPEG_FOUND "" CACHE INTERNAL "")
    get_cmake_property(TWIN_CACHED CACHE_VARIABLES)
    foreach(variable ${TWIN_CACHED})
        if(variable MATCHES "^pkgcfg_lib_FFMPEG_")
            unset(${variable} CACHE)
        endif()
    endforeach()
    set(TWIN_FFMPEG_CHOSEN ${TWIN_FFMPEG_CHOICE} CACHE INTERNAL "")
endif()
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET GLOBAL libavcodec libavutil)
message(STATUS "FFmpeg: ${TWIN_FFMPEG_CHOICE} (libavcodec ${FFMPEG_libavcodec_VERSION})")
endif()

# What the native tools that run clang themselves (data_types.py, convert_data.py, make_stubs.py) take of this build: its
# compiler, the libraries' include folders and import libraries (native/tools/host.py)
set(TWIN_HOST_INCLUDES "")
set(TWIN_HOST_LIBS "")
if(TARGET SDL3::Headers)
    get_target_property(TWIN_SDL_INCLUDES SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
    if(TWIN_SDL_INCLUDES)
        list(APPEND TWIN_HOST_INCLUDES ${TWIN_SDL_INCLUDES})
    endif()
endif()
if(WIN32)
    list(APPEND TWIN_HOST_INCLUDES "${TWIN_FFMPEG_DIR}/include")
    foreach(property IMPORTED_IMPLIB IMPORTED_IMPLIB_RELEASE IMPORTED_IMPLIB_RELWITHDEBINFO IMPORTED_IMPLIB_NOCONFIG)
        get_target_property(TWIN_SDL_IMPLIB SDL3::SDL3-shared ${property})
        if(TWIN_SDL_IMPLIB)
            break()
        endif()
    endforeach()
    if(TWIN_SDL_IMPLIB)
        list(APPEND TWIN_HOST_LIBS ${TWIN_SDL_IMPLIB})
    endif()
    list(APPEND TWIN_HOST_LIBS "${TWIN_FFMPEG_DIR}/lib/libavcodec.dll.a" "${TWIN_FFMPEG_DIR}/lib/libavutil.dll.a")
endif()
# Separated by | (a CMake list's ; would split the command's argument, and Windows' paths have colons)
string(REPLACE ";" "|" TWIN_HOST_INCLUDES_ENV "${TWIN_HOST_INCLUDES}")
string(REPLACE ";" "|" TWIN_HOST_LIBS_ENV "${TWIN_HOST_LIBS}")
set(TWIN_TOOL_ENV ${CMAKE_COMMAND} -E env "TWIN_CLANG=${CMAKE_CXX_COMPILER}" "TWIN_HOST_INCLUDES=${TWIN_HOST_INCLUDES_ENV}"
    "TWIN_HOST_LIBS=${TWIN_HOST_LIBS_ENV}")

# The game's C++, and the decomp's own C++ of the retail C library and maths (portable, kept for their exact results)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${ROOT}/src/game/*.cpp)
set(CORE_SOURCES ${ROOT}/src/debug.cpp ${ROOT}/src/gcc2.cpp ${ROOT}/src/main.cpp)
file(STRINGS ${ROOT}/native/reused_ps2.txt REUSED_PS2 REGEX "^src/")
list(TRANSFORM REUSED_PS2 PREPEND ${ROOT}/)
set(RETAIL_SOURCES ${REUSED_PS2})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT}/native/reused_ps2.txt)
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS ${ROOT}/src/platform/native/*.cpp)
# The version the log's first line names (entry.cpp alone, so a new version compiles one file again)
set_source_files_properties(${ROOT}/src/platform/native/entry.cpp PROPERTIES
    COMPILE_DEFINITIONS "TWIN_PORT_VERSION=\"${PROJECT_VERSION}\"")

# The retail data as C++, made again when the split, the converter or the C++'s declarations change
set(DATA_CPP ${ROOT}/build/native/data.cpp)
file(GLOB DATA_INPUTS CONFIGURE_DEPENDS ${ROOT}/asm/data/*.s ${ROOT}/include/*.h ${ROOT}/include/*/*.h)
# Windows: every function taking floats with its integers declared RETAIL_N32 (System V's: include/abi.h's CallEabi)
set(TWIN_CHECK_CONVENTIONS)
if(WIN32)
    set(TWIN_CHECK_CONVENTIONS COMMAND ${Python3_EXECUTABLE} native/tools/check_conventions.py)
endif()
add_custom_command(
    OUTPUT ${DATA_CPP}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/data_types.py
    ${TWIN_CHECK_CONVENTIONS}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/convert_data.py
    WORKING_DIRECTORY ${ROOT}
    DEPENDS ${ROOT}/native/tools/data_types.py ${ROOT}/native/tools/convert_data.py ${ROOT}/native/tools/check_conventions.py
            ${DATA_INPUTS} ${GAME_SOURCES}
            ${CORE_SOURCES} ${RETAIL_SOURCES} ${PLATFORM_SOURCES} ${ROOT}/native/reused_ps2.txt
    COMMENT "Converting the retail data"
    VERBATIM)

# Everything compiled once into objects; the stand-ins for what the platform layer doesn't do yet (weak, native/tools/
# make_stubs.py) are made from what the objects leave undefined, then the program is linked
# The script commands' arguments as the files have them (native/tools/script_layouts.py)
set(SCRIPT_LAYOUTS_CPP ${ROOT}/build/native/scriptlayouts.cpp)
add_custom_command(
    OUTPUT ${SCRIPT_LAYOUTS_CPP}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/script_layouts.py
    WORKING_DIRECTORY ${ROOT}
    DEPENDS ${ROOT}/native/tools/script_layouts.py ${ROOT}/native/tools/convert_data.py ${DATA_CPP}
    COMMENT "Laying out the script commands' arguments"
    VERBATIM)

# VU1's programs translated to C++ from the game's microcode (native/tools/vu1_translate.py; native/GRAPHICS.md, "VU1
# translation")
set(VU1_PROGRAMS_CPP ${ROOT}/build/native/vu1programs.cpp)
foreach(shard RANGE 7)
    list(APPEND VU1_PROGRAMS_CPP ${ROOT}/build/native/vu1programs_${shard}.cpp)
endforeach()
add_custom_command(
    OUTPUT ${VU1_PROGRAMS_CPP}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/vu1_translate.py ${ROOT}/build/native/vu1programs.cpp
    WORKING_DIRECTORY ${ROOT}
    DEPENDS ${ROOT}/native/tools/vu1_translate.py ${ROOT}/native/tools/vudecode.py ${ROOT}/assets/vutext.textbin.bin
            ${ROOT}/asm/data/vudata.data.s ${ROOT}/src/platform/native/graphics/renderer/vuprograms.cpp
    COMMENT "Translating VU1's programs"
    VERBATIM)
# VU0's microprogram sets the same way (vu1_translate.py --vu0)
set(VU0_PROGRAMS_CPP ${ROOT}/build/native/vu0programs.cpp ${ROOT}/build/native/vu0programs_0.cpp ${ROOT}/build/native/vu0programs_1.cpp)
add_custom_command(
    OUTPUT ${VU0_PROGRAMS_CPP}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/vu1_translate.py --vu0 ${ROOT}/build/native/vu0programs.cpp
    WORKING_DIRECTORY ${ROOT}
    DEPENDS ${ROOT}/native/tools/vu1_translate.py ${ROOT}/native/tools/vudecode.py ${ROOT}/assets/vutext.textbin.bin
    COMMENT "Translating VU0's programs"
    VERBATIM)

# The files defining explicit specializations' members keep line tables only in every build (see above)
file(GLOB SPECIALIZING_SOURCES ${ROOT}/src/game/pools.cpp)
foreach(source ${GAME_SOURCES} ${CORE_SOURCES})
    file(STRINGS ${source} specializes REGEX "^template <>")
    if(specializes)
        set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS -gline-tables-only)
    endif()
endforeach()

add_library(game OBJECT ${GAME_SOURCES} ${CORE_SOURCES} ${RETAIL_SOURCES} ${PLATFORM_SOURCES} ${DATA_CPP} ${SCRIPT_LAYOUTS_CPP}
            ${VU1_PROGRAMS_CPP} ${VU0_PROGRAMS_CPP})
# The hardware's emulation (the GS, the hardware renderer, the EE's DMA, VIF and VU) optimised across its files (ThinLTO): its
# small functions (a pixel's address, a format's size) are called from one file into another for every primitive and texel. The
# game's own C++ is left as it is compiled
option(TWIN_EMULATION_LTO "Optimise the hardware's emulation across its files (ThinLTO: measured no faster)" OFF)
if(TWIN_EMULATION_LTO)
    file(GLOB TWIN_EMULATION_SOURCES CONFIGURE_DEPENDS ${ROOT}/src/platform/native/graphics/gs/*.cpp
         ${ROOT}/src/platform/native/graphics/hw/*.cpp ${ROOT}/src/platform/native/graphics/ee/*.cpp)
    set_property(SOURCE ${TWIN_EMULATION_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -flto=thin)
    set(TWIN_LTO_LINK -flto=thin)
endif()
set(STUBS_CPP ${ROOT}/build/native/stubs.cpp)
# The objects' list in a file (@file): it's longer than Windows' command lines
set(STUBS_OBJECTS ${CMAKE_CURRENT_BINARY_DIR}/stub_objects.txt)
file(GENERATE OUTPUT ${STUBS_OBJECTS} CONTENT "$<JOIN:$<TARGET_OBJECTS:game>,
>
")
add_custom_command(
    OUTPUT ${STUBS_CPP}
    COMMAND ${TWIN_TOOL_ENV} ${Python3_EXECUTABLE} native/tools/make_stubs.py ${STUBS_CPP} @${STUBS_OBJECTS}
    WORKING_DIRECTORY ${ROOT}
    DEPENDS game $<TARGET_OBJECTS:game> ${ROOT}/native/tools/make_stubs.py
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMENT "Making the stand-ins for what the platform layer doesn't do yet")
# Windows: the RETAIL_N32 functions' addresses, for the calls through the retail vtables and pointers to go each function's way
# (native/tools/sysv_functions.py, include/abi.h's CallEabi)
set(TWIN_SYSV_CPP)
if(WIN32)
    set(TWIN_SYSV_CPP ${ROOT}/build/native/sysv_functions.cpp)
    file(GLOB_RECURSE TWIN_SYSV_INPUTS CONFIGURE_DEPENDS ${ROOT}/include/*.h ${ROOT}/src/*.h ${ROOT}/src/*.cpp)
    add_custom_command(
        OUTPUT ${TWIN_SYSV_CPP}
        COMMAND ${Python3_EXECUTABLE} native/tools/sysv_functions.py ${TWIN_SYSV_CPP}
        WORKING_DIRECTORY ${ROOT}
        DEPENDS ${ROOT}/native/tools/sysv_functions.py ${TWIN_SYSV_INPUTS}
        COMMENT "Listing the functions the System V convention calls"
        VERBATIM)
endif()
add_executable(twinsanity $<TARGET_OBJECTS:game> ${STUBS_CPP} ${TWIN_SYSV_CPP})

foreach(target game twinsanity)
target_include_directories(${target} PRIVATE ${ROOT}/native/include ${ROOT}/include ${ROOT}/src/platform/native)
target_compile_definitions(${target} PRIVATE TWIN_NATIVE)
# What the PS2 build's flags keep of the retail code's behaviour: no fused multiply-adds (the R5900 has none the game's code
# uses), no assumptions about aliasing, null checks or signed overflow the retail compiler didn't make. A float made an integer
# out of the integer's range saturates, as the R5900's conversion and arm64's do (x86-64's own gives 0x80000000 either way):
# -fno-strict-float-cast-overflow makes the conversions clang's saturating ones (the same instruction on arm64). The retail code
# makes every float an integer with one cvt.w.s (0x7FFFFFFF from 2^31 up, 0x80000000 from -2^31 down) and the game's C++ only
# ever makes it an s32, so its results are the R5900's. It also makes a negative float made unsigned 0 (cvt.w.s's bits wrap)
# and clamps one past a narrower type, but only the platform's code has those, on values in range. Out of range in play (the
# level sweep with -fsanitize=float-cast-overflow): cameras' check intervals of +-2^32 clock units (High Seas Hi-Jinks, the
# school's roof: never polled, so unused) and a sprite's wobble angle (its base never set: the heap's leftovers). WINDOWS.md
target_compile_options(${target} PRIVATE
    -std=gnu++2c -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv
    -ffp-contract=off -fno-strict-float-cast-overflow -Wall -Wno-invalid-offsetof -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
    -Wno-unknown-pragmas -Wno-missing-braces -Wno-mismatched-tags)
target_link_libraries(${target} PRIVATE SDL3::SDL3 OpenGL::GL PkgConfig::FFMPEG)
if(WIN32)
    # MinGW lays bitfields out the Microsoft way by default; the game's (and its files') are GCC's, as on every other host
    # (native/tools/host.py gives the layout tools the same flag)
    target_compile_options(${target} PRIVATE -mno-ms-bitfields)
    # x86-64-v2 (SSE4.2, POPCNT: what Windows 11 needs of a processor anyway): floor and ceil inline (SSE4.1's ROUNDSS), the
    # rest as before. No FMA (that's v3), and the float arithmetic stays IEEE's, so the results don't change
    target_compile_options(${target} PRIVATE -march=x86-64-v2)
endif()
endforeach()
if(WIN32)
    # The executable below 2 GB at a fixed place: the low memory's emulation moves an instruction's displacement by
    # g_NativeLowMemory's address, which has to fit 32 bits (src/platform/native/lowmemory_win32.cpp). The C++ runtime is
    # linked in; SDL, FFmpeg and OpenGL stay DLLs
    target_link_options(twinsanity PRIVATE ${TWIN_LTO_LINK} -static -Wl,--image-base=0x10000000 -Wl,--disable-dynamicbase
                        -Wl,--disable-high-entropy-va -Wl,--stack,8388608)
    set_target_properties(twinsanity PROPERTIES OUTPUT_NAME "CrashTwinsanity")
    # A windowed program (no console; the log goes to the terminal that started it, else to log.txt in the settings folder,
    # src/platform/native/entry.cpp), or a console one
    option(TWIN_WINDOWS_CONSOLE "Windows: a console program rather than a windowed one" OFF)
    if(NOT TWIN_WINDOWS_CONSOLE)
        set_target_properties(twinsanity PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    # timeBeginPeriod (src/platform/native/entry.cpp)
    target_link_libraries(twinsanity PRIVATE winmm)
    # The program's icon (native/icon/AppIcon.ico, 16 to 256 pixels, drawn by native/tools/draw_icon.py) and its version
    # information, as resources (SDL's window takes icon 1 too)
    set(TWIN_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon/AppIcon.ico)
    enable_language(RC)
    set(TWIN_RC_ICON "")
    if(EXISTS ${TWIN_ICON})
        set(TWIN_RC_ICON "1 ICON \"${TWIN_ICON}\"")
    endif()
    set(TWIN_RC ${CMAKE_CURRENT_BINARY_DIR}/twinsanity.rc)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/windows.rc.in ${TWIN_RC} @ONLY)
    target_sources(twinsanity PRIVATE ${TWIN_RC})
    # (windres doesn't say which files a resource script reads: a new icon would leave the old one in)
    if(TWIN_RC_ICON)
        set_source_files_properties(${TWIN_RC} PROPERTIES OBJECT_DEPENDS ${TWIN_ICON})
    endif()
    # Next to the executable: SDL's and FFmpeg's DLLs and the UI's fonts (overlay.cpp looks in fonts/ beside it)
    set(TWIN_UI_FONTS ${ROOT}/native/third_party/fonts/LuckiestGuy-Regular.ttf ${ROOT}/native/third_party/fonts/Fredoka-SemiBold.ttf
        ${ROOT}/native/third_party/fonts/Fredoka-Bold.ttf)
    add_custom_command(TARGET twinsanity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:twinsanity> ${TWIN_FFMPEG_DLLS}
                $<TARGET_FILE_DIR:twinsanity>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:twinsanity>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TWIN_UI_FONTS} $<TARGET_FILE_DIR:twinsanity>/fonts
        COMMAND_EXPAND_LISTS)
endif()
# Linux: the executable's symbols in its dynamic table, which is where glibc's backtrace_symbols finds the names for a fatal
# signal's backtrace (native/tools/level_sweep.py reads them)
target_link_options(twinsanity PRIVATE $<$<PLATFORM_ID:Linux>:-rdynamic>)
if(APPLE)
    # A Mac app: "Crash Twinsanity.app", with the icon native/tools/make_icon.py makes from the game's own art when it's there
    set(ICON ${ROOT}/build/native/icon/AppIcon.icns)
    set_target_properties(twinsanity PROPERTIES
        OUTPUT_NAME "Crash Twinsanity"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Crash Twinsanity"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.twinsanity.native"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE AppIcon)
    if(EXISTS ${ICON})
        target_sources(twinsanity PRIVATE ${ICON})
        set_source_files_properties(${ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
    # The UI's fonts with their licences (native/third_party/fonts/README.md), in Resources/fonts
    set(UI_FONTS ${ROOT}/native/third_party/fonts/LuckiestGuy-Regular.ttf ${ROOT}/native/third_party/fonts/Fredoka-SemiBold.ttf
        ${ROOT}/native/third_party/fonts/Fredoka-Bold.ttf ${ROOT}/native/third_party/fonts/LICENSE-LuckiestGuy.txt
        ${ROOT}/native/third_party/fonts/OFL-Fredoka.txt ${ROOT}/native/third_party/fonts/README.md)
    target_sources(twinsanity PRIVATE ${UI_FONTS})
    set_source_files_properties(${UI_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
endif()

# The release: the app built, made standalone (its libraries in Contents/Frameworks, their licences, signed for this Mac) and zipped:
# build/native/dist/Crash Twinsanity.app and its .zip. `cmake --build build/native/cmake --target release`
if(APPLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(release
        COMMAND ${Python3_EXECUTABLE} ${ROOT}/native/tools/bundle_app.py --app $<TARGET_BUNDLE_DIR:twinsanity> --zip
        DEPENDS twinsanity
        WORKING_DIRECTORY ${ROOT}
        COMMENT "Making the standalone Crash Twinsanity.app (build/native/dist)"
        VERBATIM)
endif()
