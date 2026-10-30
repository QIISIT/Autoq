cmake_minimum_required(VERSION 3.19)
project(AutoQ VERSION 1.18.3 LANGUAGES CXX)
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
option(AQ_TEST_BUILD "Тестовая сборка не для релиза: к версии добавляется «test»" OFF)
if(AQ_TEST_BUILD)
    add_compile_definitions(AQ_TEST_BUILD)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Код под одну ОС (src/platform): в сборку — только своей системы
if(WIN32)
    set(PLATFORM_CORE
        ${SRC}/platform/windows/clicker_win.cpp
        ${SRC}/platform/windows/framesync_win.cpp
        ${SRC}/platform/windows/hotkey_win.cpp
        ${SRC}/platform/windows/winmatch_win.cpp
        ${SRC}/platform/windows/framewatch_win.cpp)
    set(PLATFORM_APP
        ${SRC}/platform/windows/autostart_win.cpp
        ${SRC}/platform/windows/updater_win.cpp)
elseif(APPLE)
    set(PLATFORM_CORE
        ${SRC}/platform/macos/clicker_mac.cpp
        ${SRC}/platform/macos/framesync_mac.cpp
        ${SRC}/platform/macos/hotkey_mac.cpp
        ${SRC}/platform/macos/winmatch_mac.cpp
        ${SRC}/platform/framewatch_none.cpp
        ${SRC}/platform/macos/macsupport.mm     ${SRC}/platform/macos/macsupport.h)
    set(PLATFORM_APP
        ${SRC}/platform/macos/autostart_mac.cpp
        ${SRC}/platform/macos/updater_mac.cpp
        ${SRC}/platform/macos/macperm.cpp       ${SRC}/platform/macos/macperm.h)
else()
    find_package(X11 REQUIRED)
    # «По кадрам»: кадры окна игры через расширение Present (Arch — в libxcb,
    # Debian/Ubuntu — libxcb-present-dev). Нет — AutoQ собирается, но без «По кадрам»
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XCB_PRESENT IMPORTED_TARGET xcb xcb-present)
    endif()
    set(PLATFORM_CORE
        ${SRC}/platform/linux/clicker_x11.cpp
        ${SRC}/platform/linux/framesync_x11.cpp
        ${SRC}/platform/linux/hotkey_x11.cpp
        ${SRC}/platform/linux/keys_x11.cpp
        ${SRC}/platform/linux/winmatch_x11.cpp  ${SRC}/platform/linux/winmatch_x11.h)
    if(XCB_PRESENT_FOUND)
        list(APPEND PLATFORM_CORE ${SRC}/platform/linux/framewatch_x11.cpp)
    else()
        message(WARNING "xcb-present не найден: «По кадрам» будет недоступно (нужен libxcb-present-dev)")
        list(APPEND PLATFORM_CORE ${SRC}/platform/framewatch_none.cpp)
    endif()
    set(PLATFORM_APP
        ${SRC}/platform/linux/autostart_linux.cpp
        ${SRC}/platform/linux/updater_linux.cpp)
endif()

# Ядро без интерфейса (только Qt Core): клавиши, игры, окно игры, кликер, хоткей. Его же — тесты
add_library(aq_core STATIC
    ${SRC}/core/i18n.cpp        ${SRC}/core/i18n.h          ${SRC}/core/i18n_en.cpp
    ${SRC}/core/keys.cpp        ${SRC}/core/keys.h
    ${SRC}/core/games.cpp       ${SRC}/core/games.h
    ${SRC}/core/appsettings.cpp ${SRC}/core/appsettings.h
    ${SRC}/core/winmatch.cpp    ${SRC}/core/winmatch.h
    ${SRC}/core/clicker.cpp     ${SRC}/core/clicker.h       ${SRC}/core/clicker_os.h
    ${SRC}/core/hotkey.cpp      ${SRC}/core/hotkey.h
    ${SRC}/core/sounds.cpp      ${SRC}/core/sounds.h
    ${SRC}/core/framesync.cpp   ${SRC}/core/framesync.h
    ${SRC}/core/framewatch.cpp  ${SRC}/core/framewatch.h    ${SRC}/core/framewatch_os.h
    ${SRC}/core/cardlayout.cpp  ${SRC}/core/cardlayout.h
    ${SRC}/core/updates.cpp     ${SRC}/core/updates.h
    ${SRC}/core/tips.cpp        ${SRC}/core/tips.h
    ${PLATFORM_CORE})
target_link_libraries(aq_core PUBLIC Qt6::Core)
target_include_directories(aq_core PUBLIC ${SRC})

# Значки (логотип, иконки игр, интерфейс) — рисуются кодом
add_library(aq_icons STATIC ${SRC}/ui/style/icons.cpp ${SRC}/ui/style/icons.h)
target_link_libraries(aq_icons PUBLIC Qt6::Gui aq_core)

set(APP_SOURCES
    ${SRC}/main.cpp
    ${SRC}/appinfo.h
    ${SRC}/services/updater.cpp           ${SRC}/services/updater.h    ${SRC}/services/updater_os.h
    ${SRC}/services/autostart.h
    ${SRC}/ui/mainwindow.cpp              ${SRC}/ui/mainwindow.h
    ${SRC}/ui/miniwindow.cpp              ${SRC}/ui/miniwindow.h
    ${SRC}/ui/overlay.cpp                 ${SRC}/ui/overlay.h
    ${SRC}/ui/onboarding.cpp              ${SRC}/ui/onboarding.h
    ${SRC}/ui/actionseditor.cpp           ${SRC}/ui/actionseditor.h
    ${SRC}/ui/board.cpp                   ${SRC}/ui/board.h
    ${SRC}/ui/dialogs/settingsdialog.cpp  ${SRC}/ui/dialogs/settingsdialog.h
    ${SRC}/ui/dialogs/gameeditdialog.cpp  ${SRC}/ui/dialogs/gameeditdialog.h
    ${SRC}/ui/dialogs/groupdialog.cpp     ${SRC}/ui/dialogs/groupdialog.h
    ${SRC}/ui/dialogs/hotkeydialog.cpp    ${SRC}/ui/dialogs/hotkeydialog.h
    ${SRC}/ui/dialogs/keypicker.cpp       ${SRC}/ui/dialogs/keypicker.h
    ${SRC}/ui/dialogs/windowpicker.cpp    ${SRC}/ui/dialogs/windowpicker.h
    ${SRC}/ui/dialogs/colorpicker.cpp     ${SRC}/ui/dialogs/colorpicker.h
    ${SRC}/ui/style/theme.cpp             ${SRC}/ui/style/theme.h
    ${SRC}/ui/style/styles.cpp            ${SRC}/ui/style/styles.h
    ${SRC}/ui/style/styleui.cpp           ${SRC}/ui/style/styleui.h
    ${SRC}/ui/style/animations.cpp        ${SRC}/ui/style/animations.h
    ${SRC}/ui/style/widgets.cpp           ${SRC}/ui/style/widgets.h
    ${SRC}/ui/style/keycapturebutton.h
    ${PLATFORM_APP})
if(WIN32)
    enable_language(RC)
    # иконка exe, манифест и сведения о версии
    list(APPEND APP_SOURCES packaging/windows/app.rc)
elseif(APPLE)
    # значок программы (.icns в Contents/Resources)
    list(APPEND APP_SOURCES packaging/macos/AutoQ.icns)
    set_source_files_properties(packaging/macos/AutoQ.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# WIN32 = GUI-приложение без чёрного окна консоли, MACOSX_BUNDLE = AutoQ.app
# (на Linux оба игнорируются). Программа — в корне папки сборки: build/AutoQ
qt_add_executable(AutoQ WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(AutoQ PRIVATE aq_core aq_icons Qt6::Widgets)

# Встроенные шрифты (лицензия SIL OFL — тексты лицензий в resources/fonts)
set(AQ_FONTS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Unbounded-Bold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Unbounded-Black.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Onest-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Onest-SemiBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/JetBrainsMono-Bold.ttf
)
qt_add_resources(AutoQ "aq_fonts"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/resources
    FILES ${AQ_FONTS})

if(WIN32)
    target_link_libraries(aq_core PUBLIC winmm dwmapi advapi32 tdh)   # advapi32, tdh — ETW («По кадрам»)
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
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.qisit.autoq
        MACOSX_BUNDLE_BUNDLE_NAME AutoQ
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE AutoQ.icns
        MACOSX_BUNDLE_COPYRIGHT "AutoQ — created by Qisit")
else()
    target_link_libraries(aq_core PUBLIC X11 Xtst Xi)
    if(XCB_PRESENT_FOUND)
        target_link_libraries(aq_core PUBLIC PkgConfig::XCB_PRESENT)
    endif()
endif()

if(AQ_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS AutoQ RUNTIME DESTINATION bin BUNDLE DESTINATION .)
