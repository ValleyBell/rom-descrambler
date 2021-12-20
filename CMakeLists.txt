cmake_minimum_required(VERSION 3.0)
project(rom-descrambler)

#if(UNIX)
#    set(CURSES_NEED_WIDE TRUE)  # Unicode support currently implies NCurses
#endif()
find_package(Curses REQUIRED)

add_executable(${PROJECT_NAME}
    main.c
    descramble.c
    hex-output.c
    tui.c
    )

target_include_directories(${PROJECT_NAME} PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${CURSES_LIBRARIES})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${CURSES_COMPILE_DEFINITIONS})
