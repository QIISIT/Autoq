cmake_minimum_required(VERSION 3.19)
project(AutoQ VERSION 1.18.1 LANGUAGES CXX)
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

# Ядро без интерфейса: клавиши, база игр, распознавание окна, кликер, хоткей
set(CORE_SOURCES
    src/core/i18n.cpp        src/core/i18n.h          src/core/i18n_en.cpp
    src/core/keys.cpp        src/core/keys.h
    src/core/games.cpp       src/core/games.h
    src/core/appsettings.cpp
    src/core/winmatch.cpp    src/core/winmatch.h
    src/core/clicker.cpp     src/core/clicker.h
    src/core/hotkey.cpp      src/core/hotkey.h
    src/core/sounds.cpp      src/core/sounds.h
    src/core/framesync.cpp   src/core/framesync.h
    src/core/framewatch.cpp  src/core/framewatch.h
    src/core/cardlayout.cpp  src/core/cardlayout.h
    src/core/updates.cpp     src/core/updates.h
    src/core/tips.cpp        src/core/tips.h
)
if(APPLE)
    list(APPEND CORE_SOURCES src/platform/macos/macsupport.mm src/platform/macos/macsupport.h)
endif()
add_library(aq_core STATIC ${CORE_SOURCES})
target_link_libraries(aq_core PUBLIC Qt6::Core)
target_include_directories(aq_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Значки (логотип, иконки игр, интерфейс) — рисуются кодом
add_library(aq_icons STATIC src/ui/style/icons.cpp src/ui/style/icons.h)
target_link_libraries(aq_icons PUBLIC Qt6::Gui aq_core)

set(APP_SOURCES
    src/main.cpp
    src/appinfo.h
    src/ui/mainwindow.cpp              src/ui/mainwindow.h
    src/ui/miniwindow.cpp              src/ui/miniwindow.h
    src/ui/overlay.cpp                 src/ui/overlay.h
    src/ui/onboarding.cpp              src/ui/onboarding.h
    src/ui/actionseditor.cpp           src/ui/actionseditor.h
    src/ui/board.cpp                   src/ui/board.h
    src/ui/dialogs/settingsdialog.cpp  src/ui/dialogs/settingsdialog.h
    src/ui/dialogs/gameeditdialog.cpp  src/ui/dialogs/gameeditdialog.h
    src/ui/dialogs/groupdialog.cpp     src/ui/dialogs/groupdialog.h
    src/ui/dialogs/hotkeydialog.cpp    src/ui/dialogs/hotkeydialog.h
    src/ui/dialogs/keypicker.cpp       src/ui/dialogs/keypicker.h
    src/ui/dialogs/windowpicker.cpp    src/ui/dialogs/windowpicker.h
    src/ui/dialogs/colorpicker.cpp     src/ui/dialogs/colorpicker.h
    src/ui/style/theme.cpp             src/ui/style/theme.h
    src/ui/style/styles.cpp            src/ui/style/styles.h
    src/ui/style/styleui.cpp           src/ui/style/styleui.h
    src/ui/style/animations.cpp        src/ui/style/animations.h
    src/ui/style/widgets.cpp           src/ui/style/widgets.h
    src/ui/style/keycapturebutton.h
    src/services/updater.cpp           src/services/updater.h
    src/services/autostart.cpp         src/services/autostart.h
)
if(WIN32)
    enable_language(RC)
    # иконка exe, манифест (права администратора) и сведения о версии
    list(APPEND APP_SOURCES packaging/windows/app.rc)
endif()
if(APPLE)
    # окно с разрешениями macOS и значок программы (.icns в Contents/Resources)
    list(APPEND APP_SOURCES src/platform/macos/macperm.cpp src/platform/macos/macperm.h
                            packaging/macos/AutoQ.icns)
    set_source_files_properties(packaging/macos/AutoQ.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# WIN32 = GUI-приложение без чёрного окна консоли, MACOSX_BUNDLE = AutoQ.app
# (на Linux оба игнорируются)
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
elseif(UNIX)
    find_package(X11 REQUIRED)
    target_link_libraries(aq_core PUBLIC X11 Xtst Xi)
    # «По кадрам»: кадры окна игры через расширение Present (Arch — в libxcb,
    # Debian/Ubuntu — libxcb-present-dev). Нет — AutoQ собирается, но без «По кадрам»
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XCB_PRESENT IMPORTED_TARGET xcb xcb-present)
    endif()
    if(XCB_PRESENT_FOUND)
        target_link_libraries(aq_core PUBLIC PkgConfig::XCB_PRESENT)
        target_compile_definitions(aq_core PRIVATE AQ_XCB_PRESENT)
    else()
        message(WARNING "xcb-present не найден: «По кадрам» будет недоступно (нужен libxcb-present-dev)")
    endif()
endif()

if(AQ_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS AutoQ RUNTIME DESTINATION bin BUNDLE DESTINATION .)
