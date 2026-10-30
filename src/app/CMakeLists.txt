qt_add_executable(superfar
  main.cpp
  bench.cpp
  mainwindow.cpp
  tree.cpp
  keymap.cpp
  funcbar.cpp
  cmdline.cpp
  viewer.cpp
  preview.cpp
  pane/delegate.cpp
  jobs/bars.cpp
  jobs/panel.cpp
  jobs/strip.cpp
  pane/pane.cpp
  pane/tab.cpp
  pane/listview.cpp
  pane/listmodel.cpp
)
target_include_directories(superfar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(superfar PRIVATE superfar_core Qt6::Widgets)

# The desktop entry gives the window its app id for the compositor and the
# portal; without it installed Qt logs a portal registration failure.
install(TARGETS superfar RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/superfar.desktop DESTINATION share/applications)
