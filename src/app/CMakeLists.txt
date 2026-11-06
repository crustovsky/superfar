qt_add_executable(superfar
  main.cpp
  bench.cpp
  mainwindow.cpp
  tree.cpp
  keymap.cpp
  funcbar.cpp
  menu.cpp
  cmdline.cpp
  config.cpp
  places.cpp
  devaids.cpp
  help.cpp
  properties.cpp
  multirename.cpp
  syncpanel.cpp
  packpanel.cpp
  uris.cpp
  fm1.cpp
  viewer.cpp
  preview.cpp
  pagestrip.cpp
  pdfpages.cpp
  videoplay.cpp
  archivetext.cpp
  pane/columns.cpp
  pane/delegate.cpp
  jobs/bars.cpp
  jobs/panel.cpp
  jobs/strip.cpp
  pane/pane.cpp
  pane/tabbar.cpp
  pane/tab.cpp
  pane/tab_archive.cpp
  pane/listview.cpp
  pane/listmodel.cpp
  pane/listmodel_sort.cpp
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
string(TIMESTAMP SUPERFAR_BUILD_DATE "%Y-%m-%d" UTC)
target_compile_definitions(superfar PRIVATE SUPERFAR_VERSION="${PROJECT_VERSION}" SUPERFAR_BUILD_DATE="${SUPERFAR_BUILD_DATE}")

# The desktop entry gives the window its app id for the compositor and the
# portal; without it installed Qt logs a portal registration failure.
install(TARGETS superfar RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/superfar.desktop DESTINATION share/applications)
# Exec is the installed path: a launcher started by the compositor may
# not have ~/.local/bin on its PATH, and the entry has to work from
# whatever prefix it lands in.
install(CODE "
  set(entry \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share/applications/superfar.desktop\")
  file(READ \"\${entry}\" text)
  string(REPLACE \"Exec=superfar %U\" \"Exec=\${CMAKE_INSTALL_PREFIX}/bin/superfar %U\" text \"\${text}\")
  file(WRITE \"\${entry}\" \"\${text}\")
")
foreach(size IN LISTS ICON_SIZES)
  install(FILES ${ICON_DIR}/${size}x${size}/apps/superfar.png
          DESTINATION share/icons/hicolor/${size}x${size}/apps)
endforeach()
install(FILES ${ICON_DIR}/scalable/apps/superfar.svg DESTINATION share/icons/hicolor/scalable/apps)
