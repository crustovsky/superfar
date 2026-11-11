# core: filesystem layer. Links QtCore and GIO only; GIO is private so no
# GIO header can leak into app through this target.
qt_add_library(superfar_core STATIC
  location.cpp
  sort.cpp
  match.cpp
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
  finder.cpp
  thumbnails.cpp
  rename.cpp
  compare.cpp
  child.cpp
  archive.cpp
  pages.cpp
  video.cpp
  jobs/extract.cpp
  jobs/pack.cpp
  jobs/attributes.cpp
  fileops.cpp
  unixattrs.cpp
  jobs/job.cpp
  jobs/transfer.cpp
  jobs/remove.cpp
  jobs/undo.cpp
  jobs/queue.cpp
  undo.cpp
)
target_include_directories(superfar_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(superfar_core
  PUBLIC Qt6::Core
  PRIVATE PkgConfig::GIO PkgConfig::GLYCIN
)
