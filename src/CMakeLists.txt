set(c10t_SOURCES level.cpp)
set(c10t_SOURCES ${c10t_SOURCES} image.cpp)
set(c10t_SOURCES ${c10t_SOURCES} color.cpp)
set(c10t_SOURCES ${c10t_SOURCES} blocks.cpp)
set(c10t_SOURCES ${c10t_SOURCES} common.cpp)
set(c10t_SOURCES ${c10t_SOURCES} players.cpp)
set(c10t_SOURCES ${c10t_SOURCES} world.cpp)
set(c10t_SOURCES ${c10t_SOURCES} fileutils.cpp)
set(c10t_SOURCES ${c10t_SOURCES} utf8.cpp)
set(c10t_SOURCES ${c10t_SOURCES} warps.cpp)
set(c10t_SOURCES ${c10t_SOURCES} nbt/nbt.cpp)

add_library(c10t-lib EXCLUDE_FROM_ALL ${c10t_SOURCES})
add_executable(c10t main.cpp ${c10t_SOURCES})
add_executable(c10t-debug EXCLUDE_FROM_ALL main.cpp ${c10t_SOURCES})
add_executable(nbt-inspect EXCLUDE_FROM_ALL nbt/nbt_inspect.cpp nbt/nbt.cpp)

set_target_properties(c10t PROPERTIES COMPILE_FLAGS "-O3 -Wall")
set_target_properties(c10t-debug PROPERTIES COMPILE_FLAGS "-Wall -pedantic -g")
set_target_properties(c10t-lib PROPERTIES COMPILE_FLAGS "-O3 -Wall  -pedantic -g")
set_target_properties(nbt-inspect PROPERTIES COMPILE_FLAGS "-O3 -Wall -pedantic -g")

target_link_libraries(c10t ${c10t_LIBRARIES})
target_link_libraries(c10t-debug ${c10t_LIBRARIES})
target_link_libraries(nbt-inspect ${ZLIB_LIBRARIES})
