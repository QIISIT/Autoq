# Автотесты: собираются с -DAQ_BUILD_TESTS=ON
set(AQ_SRC ${SRC})

add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE aq_core)

# значки и логотип (нужен Qt Gui, запускается с QT_QPA_PLATFORM=offscreen)
add_executable(test_icons test_icons.cpp)
target_link_libraries(test_icons PRIVATE aq_icons aq_core)

# оверлей КПС (Qt Widgets, запускается с QT_QPA_PLATFORM=offscreen)
add_executable(test_overlay test_overlay.cpp
               ${AQ_SRC}/ui/overlay.cpp ${AQ_SRC}/ui/overlay.h
               ${AQ_SRC}/ui/style/theme.cpp ${AQ_SRC}/ui/style/theme.h
               ${AQ_SRC}/ui/style/styles.cpp ${AQ_SRC}/ui/style/styles.h
               ${AQ_SRC}/ui/style/keycapturebutton.h)
target_link_libraries(test_overlay PRIVATE aq_icons aq_core Qt6::Widgets)
qt_add_resources(test_overlay "aq_fonts_test"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/resources
    FILES ${AQ_FONTS})

add_executable(inputtool inputtool.cpp)
target_link_libraries(inputtool PRIVATE aq_core)

if(WIN32)
    add_executable(keylogger_win keylogger_win.cpp)
    target_link_libraries(keylogger_win PRIVATE user32)
    if(MINGW)
        foreach(t test_core test_icons test_overlay inputtool keylogger_win)
            target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
        endforeach()
    endif()
else()
    add_executable(keylogger_x11 keylogger_x11.cpp)
    target_link_libraries(keylogger_x11 PRIVATE X11)
endif()

# test_core проверяет, что у каждой строки интерфейса есть английский перевод
target_compile_definitions(test_core PRIVATE AQ_SOURCE_DIR="${AQ_SRC}")
