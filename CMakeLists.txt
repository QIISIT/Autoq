cmake_minimum_required(VERSION 3.19)
project(AutoQ VERSION 1.16.2 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)     # macsupport.mm — AppKit (NSWorkspace, NSWindow)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)   # исходники в UTF-8 (русские строки и словарь)
endif()

option(AQ_BUILD_TESTS "Собрать автотесты" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Ядро без интерфейса: клавиши, база игр, распознавание окна, кликер, хоткей
set(CORE_SOURCES
    i18n.cpp        i18n.h          i18n_en.cpp
    keys.cpp        keys.h
    games.cpp       games.h
    appsettings.cpp
    winmatch.cpp    winmatch.h
    clicker.cpp     clicker.h
    hotkey.cpp      hotkey.h
    sounds.cpp      sounds.h
    framesync.cpp   framesync.h
    cardlayout.cpp  cardlayout.h
)
if(APPLE)
    list(APPEND CORE_SOURCES macsupport.mm macsupport.h)
endif()
add_library(aq_core STATIC ${CORE_SOURCES})
target_link_libraries(aq_core PUBLIC Qt6::Core)
target_include_directories(aq_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Значки (логотип, иконки игр, интерфейс) — рисуются кодом
add_library(aq_icons STATIC icons.cpp icons.h)
target_link_libraries(aq_icons PUBLIC Qt6::Gui aq_core)
target_include_directories(aq_icons PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(APP_SOURCES
    main.cpp
    appinfo.h
    mainwindow.cpp      mainwindow.h
    miniwindow.cpp      miniwindow.h
    gameeditdialog.cpp  gameeditdialog.h
    settingsdialog.cpp  settingsdialog.h
    colorpicker.cpp     colorpicker.h
    keypicker.cpp       keypicker.h
    windowpicker.cpp    windowpicker.h
    overlay.cpp         overlay.h
    widgets.cpp         widgets.h
    board.cpp           board.h
    theme.cpp           theme.h
    styles.cpp          styles.h
    styleui.cpp         styleui.h
    autostart.cpp       autostart.h
    animations.cpp      animations.h
    actionseditor.cpp   actionseditor.h
    onboarding.cpp      onboarding.h
    hotkeydialog.cpp    hotkeydialog.h
    keycapturebutton.h
)
if(WIN32)
    enable_language(RC)
    # иконка exe, манифест (права администратора) и сведения о версии
    list(APPEND APP_SOURCES windows/app.rc)
endif()
if(APPLE)
    # окно с разрешениями macOS и значок программы (.icns в Contents/Resources)
    list(APPEND APP_SOURCES macperm.cpp macperm.h macos/AutoQ.icns)
    set_source_files_properties(macos/AutoQ.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# WIN32 = GUI-приложение без чёрного окна консоли, MACOSX_BUNDLE = AutoQ.app
# (на Linux оба игнорируются)
qt_add_executable(AutoQ WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(AutoQ PRIVATE aq_core aq_icons Qt6::Widgets)

# Встроенные шрифты (лицензия SIL OFL — тексты лицензий в resources/fonts)
qt_add_resources(AutoQ "aq_fonts"
    PREFIX "/"
    BASE resources
    FILES
        resources/fonts/Unbounded-Bold.ttf
        resources/fonts/Unbounded-Black.ttf
        resources/fonts/Onest-Regular.ttf
        resources/fonts/Onest-SemiBold.ttf
        resources/fonts/JetBrainsMono-Medium.ttf
        resources/fonts/JetBrainsMono-Bold.ttf
)

if(WIN32)
    target_link_libraries(aq_core PUBLIC winmm dwmapi)
    target_compile_definitions(aq_core PUBLIC UNICODE _UNICODE)
    if(MINGW)
        # один самодостаточный exe — без libstdc++/libgcc/winpthread DLL
        target_link_options(AutoQ PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(APPLE)
    target_link_libraries(aq_core PUBLIC "-framework ApplicationServices" "-framework AppKit"
                                         "-framework CoreFoundation")
    set_target_properties(AutoQ PROPERTIES
        OUTPUT_NAME AutoQ
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.qisit.autoq
        MACOSX_BUNDLE_BUNDLE_NAME AutoQ
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE AutoQ.icns
        MACOSX_BUNDLE_COPYRIGHT "AutoQ — created by Qisit")
elseif(UNIX)
    find_package(X11 REQUIRED)
    target_link_libraries(aq_core PUBLIC X11 Xtst Xi)
endif()

if(AQ_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS AutoQ RUNTIME DESTINATION bin BUNDLE DESTINATION .)
