qt_add_executable(superfar
  main.cpp
  bench.cpp
  mainwindow.cpp
  tree.cpp
  keymap.cpp
  funcbar.cpp
  cmdline.cpp
  config.cpp
  places.cpp
  devaids.cpp
  help.cpp
  properties.cpp
  multirename.cpp
  syncpanel.cpp
  uris.cpp
  fm1.cpp
  viewer.cpp
  preview.cpp
  pane/delegate.cpp
  jobs/bars.cpp
  jobs/panel.cpp
  jobs/strip.cpp
  pane/pane.cpp
  pane/tab.cpp
  pane/tab_archive.cpp
  pane/listview.cpp
  pane/listmodel.cpp
)
target_include_directories(superfar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The window icon for a build that is not installed (the theme lookup
# finds nothing then); the installed hicolor set is what launchers use.
set(ICON_DIR ${CMAKE_SOURCE_DIR}/data/icons/hicolor)
set(ICON_SIZES 16 22 24 32 48 64 128 256)
set(ICON_FILES)
foreach(size IN LISTS ICON_SIZES)
  list(APPEND ICON_FILES ${ICON_DIR}/${size}x${size}/apps/superfar.png)
endforeach()
qt_add_resources(superfar "icons" PREFIX "/icons" BASE ${ICON_DIR} FILES ${ICON_FILES})
target_link_libraries(superfar PRIVATE superfar_core Qt6::Widgets Qt6::DBus)

# The desktop entry gives the window its app id for the compositor and the
# portal; without it installed Qt logs a portal registration failure.
install(TARGETS superfar RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/superfar.desktop DESTINATION share/applications)
foreach(size IN LISTS ICON_SIZES)
  install(FILES ${ICON_DIR}/${size}x${size}/apps/superfar.png
          DESTINATION share/icons/hicolor/${size}x${size}/apps)
endforeach()
install(FILES ${ICON_DIR}/scalable/apps/superfar.svg DESTINATION share/icons/hicolor/scalable/apps)
