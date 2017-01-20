set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )
include_directories(     ${CMAKE_SOURCE_DIR}/sdk/tests )

include(KritaAddBrokenUnitTest)

macro_add_unittest_definitions()

########### next target ###############

krita_add_broken_unit_test(kis_clone_test.cpp ../../../../../sdk/tests/stroke_testing_utils.cpp
    TEST_NAME krita-plugins-KisCloneOpTest
    LINK_LIBRARIES kritaimage kritaui kritalibpaintop hdf5_hl hdf5 Qt5::Test)

krita_add_broken_unit_test(kis_brushop_test.cpp ../../../../../sdk/tests/stroke_testing_utils.cpp
    TEST_NAME krita-plugins-KisBrushOpTest
    LINK_LIBRARIES kritaimage kritaui kritalibpaintop Qt5::Test)

