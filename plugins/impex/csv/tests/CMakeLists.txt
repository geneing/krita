set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )
include_directories(     ${CMAKE_SOURCE_DIR}/sdk/tests )

macro_add_unittest_definitions()

########### next target ###############
set(kis_csv_test_SRCS kis_csv_test.cpp )

kde4_add_unit_test(kis_csv_test TESTNAME krita-plugins-formats-csv_test ${kis_csv_test_SRCS})

target_link_libraries(kis_csv_test   kritaui Qt5::Test)
