# core: filesystem layer. Links QtCore and GIO only; GIO is private so no
# GIO header can leak into app through this target.
qt_add_library(superfar_core STATIC
  location.cpp
  sort.cpp
  listing.cpp
  metadata.cpp
  space.cpp
  monitor.cpp
  dirsize.cpp
  apps.cpp
  reader.cpp
  image.cpp
  mounts.cpp
  bookmarks.cpp
  fileops.cpp
  jobs/job.cpp
  jobs/transfer.cpp
  jobs/remove.cpp
)
target_include_directories(superfar_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(superfar_core
  PUBLIC Qt6::Core
  PRIVATE PkgConfig::GIO PkgConfig::GLYCIN
)
