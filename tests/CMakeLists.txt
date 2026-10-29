qt_add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE superfar_core Qt6::Test)
add_test(NAME core COMMAND test_core)

qt_add_executable(test_jobs test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE superfar_core Qt6::Test)
add_test(NAME jobs COMMAND test_jobs)
