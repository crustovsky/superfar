qt_add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE superfar_core Qt6::Test)
target_compile_definitions(test_core PRIVATE SUPERFAR_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
add_test(NAME core COMMAND test_core)

qt_add_executable(test_jobs test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE superfar_core Qt6::Test)
add_test(NAME jobs COMMAND test_jobs)

# Config is app code with no widget dependency; test it against a temp ini.
qt_add_executable(test_config test_config.cpp ${CMAKE_SOURCE_DIR}/src/app/config.cpp ${CMAKE_SOURCE_DIR}/src/core/match.cpp)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_config PRIVATE Qt6::Core Qt6::Test)
add_test(NAME config COMMAND test_config)

qt_add_executable(test_hostile test_hostile.cpp)
target_link_libraries(test_hostile PRIVATE superfar_core Qt6::Test)
add_test(NAME hostile COMMAND test_hostile)

# The key table, the help, the column table and the mark store: app code
# without widgets.
qt_add_executable(test_app test_app.cpp ${CMAKE_SOURCE_DIR}/src/app/keymap.cpp ${CMAKE_SOURCE_DIR}/src/app/help.cpp
  ${CMAKE_SOURCE_DIR}/src/app/archivetext.cpp ${CMAKE_SOURCE_DIR}/src/app/pane/columns.cpp)
target_include_directories(test_app PRIVATE ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_app PRIVATE SUPERFAR_VERSION="test" SUPERFAR_BUILD_DATE="test")
target_link_libraries(test_app PRIVATE superfar_core Qt6::Gui Qt6::Test)
add_test(NAME app COMMAND test_app)

# A tab with its list over a temp folder, and the attributes editor, on
# the offscreen platform.
set(TAB_SOURCES pane/tab.cpp pane/tab_archive.cpp pane/tab_attributes.cpp pane/listmodel.cpp
    pane/listmodel_sort.cpp pane/listmodel_attrs.cpp pane/listview.cpp pane/delegate.cpp pane/columns.cpp
    ownercompletion.cpp attributesedit.cpp keymap.cpp menu.cpp bench.cpp uris.cpp)
list(TRANSFORM TAB_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/src/app/)
qt_add_executable(test_tab test_tab.cpp ${TAB_SOURCES})
target_include_directories(test_tab PRIVATE ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tab PRIVATE superfar_core Qt6::Widgets Qt6::Test)
add_test(NAME tab COMMAND test_tab)
set_tests_properties(tab PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The whole window over temp folders, on the offscreen platform: what
# only works when the tree, the panes, the strip and the bar are wired
# together (the tree's keys and drops, the tab strip's and the tree's
# menus, the column list, the function bar's layers).
qt_add_executable(test_window test_window.cpp)
target_link_libraries(test_window PRIVATE superfar_app Qt6::Test)
add_test(NAME window COMMAND test_window)
set_tests_properties(window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
