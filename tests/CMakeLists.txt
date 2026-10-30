qt_add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE superfar_core Qt6::Test)
add_test(NAME core COMMAND test_core)

qt_add_executable(test_jobs test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE superfar_core Qt6::Test)
add_test(NAME jobs COMMAND test_jobs)

# Config is app code with no widget dependency; test it against a temp ini.
qt_add_executable(test_config test_config.cpp ${CMAKE_SOURCE_DIR}/src/app/config.cpp)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(test_config PRIVATE Qt6::Core Qt6::Test)
add_test(NAME config COMMAND test_config)

qt_add_executable(test_hostile test_hostile.cpp)
target_link_libraries(test_hostile PRIVATE superfar_core Qt6::Test)
add_test(NAME hostile COMMAND test_hostile)
