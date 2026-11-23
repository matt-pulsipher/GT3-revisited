# Developer-only differential test harness: runs isolated functions of
# the user's own CORE.GT3 in an R5900 interpreter to check the reimplementation.
# Never part of the shipped game. Included by the top-level CMakeLists.txt.
option(GT3R_BUILD_DIFFHARNESS "Build the developer differential test harness" ON)
file(GLOB GT3R_DIFF_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
if(GT3R_BUILD_DIFFHARNESS AND GT3R_DIFF_SOURCES)
  add_executable(gt3r_diffharness ${GT3R_DIFF_SOURCES})
  target_include_directories(gt3r_diffharness PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  target_link_libraries(gt3r_diffharness PRIVATE gt3r_engine ${CMAKE_DL_LIBS}) # (dlopen: GT3R_FLOAT_ORACLE)
endif()
