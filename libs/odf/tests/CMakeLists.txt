set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )

########### next target ###############

set(TestKoGenStyles_SRCS TestKoGenStyles.cpp )
kde4_add_unit_test(TestKoGenStyles TESTNAME libs-odf-TestKoGenStyles ${TestKoGenStyles_SRCS})
target_link_libraries(TestKoGenStyles kritaodf KF5::I18n  Qt5::Test)

########### next target ###############

set(TestOdfSettings_SRCS TestOdfSettings.cpp )
kde4_add_unit_test(TestOdfSettings TESTNAME libs-odf-TestOdfSettings ${TestOdfSettings_SRCS})
target_link_libraries(TestOdfSettings kritaodf KF5::I18n  Qt5::Test)

########### next target ###############

set(TestKoOdfLoadingContext_SRCS TestKoOdfLoadingContext.cpp )
kde4_add_unit_test(TestKoOdfLoadingContext TESTNAME libs-odf-TestKoOdfLoadingContext ${TestKoOdfLoadingContext_SRCS})
target_link_libraries(TestKoOdfLoadingContext kritaodf KF5::I18n  Qt5::Test)

########### next target ###############

set(xmlwritertest_SRCS TestXmlWriter.cpp )

kde4_add_unit_test(TestXmlWriter TESTNAME libs-odf-TestXmlWriter ${xmlwritertest_SRCS})

target_link_libraries(TestXmlWriter kritaodf Qt5::Test)

########### next target ###############

set(xmlreadertest_SRCS TestXmlReader.cpp )
kde4_add_unit_test(TestXmlReader TESTNAME libs-odf-TestXmlReader ${xmlreadertest_SRCS})
target_link_libraries(TestXmlReader kritaodf Qt5::Test)

########### next target ###############

set(xmlreadertestwithoutspaces_SRCS TestXmlReaderWithoutSpaces.cpp )
kde4_add_unit_test(TestXmlReaderWithoutSpaces TESTNAME libs-odf-TestXmlReaderWithoutSpaces ${xmlreadertestwithoutspaces_SRCS})
target_link_libraries(TestXmlReaderWithoutSpaces kritaodf Qt5::Test)

########### next target ###############

set(kodomtest_SRCS kodomtest.cpp )
kde4_add_unit_test(kodomtest TESTNAME libs-store-kodomtest ${kodomtest_SRCS})
target_link_libraries(kodomtest kritaodf Qt5::Test )

########### next target ###############

set(storageTest_SRCS TestStorage.cpp )
kde4_add_unit_test(TestStorage TESTNAME libs-store-storage ${storageTest_SRCS})
target_link_libraries(TestStorage kritaodf KF5::I18n  Qt5::Test)

########### next target ###############

set(TestKoUnit_SRCS TestKoUnit.cpp)
kde4_add_unit_test(TestKoUnit TESTNAME libs-kritaodf-TestKoUnit ${TestKoUnit_SRCS})
target_link_libraries(TestKoUnit kritaodf Qt5::Test)

########### next target ###############

set(TestNumberStyle_SRCS TestNumberStyle.cpp )
kde4_add_broken_unit_test(TestNumberStyle TESTNAME libs-odf-TestNumberStyle ${TestNumberStyle_SRCS})
target_link_libraries(TestNumberStyle kritaodf Qt5::Test)

########### next target ###############

set(TestKoElementReference_SRCS TestKoElementReference.cpp)
kde4_add_unit_test(TestKoElementReference TESTNAME libs-kritaodf-TestKoElementReference ${TestKoElementReference_SRCS})
target_link_libraries(TestKoElementReference kritaodf Qt5::Test)

########### end ###############
