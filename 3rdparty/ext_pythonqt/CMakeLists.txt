
SET(EXTPREFIX_pythonqt "${EXTPREFIX}" )

ExternalProject_Add(
    ext_pythonqt
    DOWNLOAD_DIR ${EXTERNALS_DOWNLOAD_DIR}
    SVN_REPOSITORY http://svn.code.sf.net/p/pythonqt/code/trunk
    SVN_REVISION -r441

    CONFIGURE_COMMAND qmake-qt5 <SOURCE_DIR>/PythonQt.pro  #this has to be fixed for Win builds ${QT_QMAKE_EXECUTABLE}
    INSTALL_DIR ${EXTPREFIX_pythonqt}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${PREFIX_ext_pthreads} -DCMAKE_BUILD_TYPE=${GLOBAL_BUILD_TYPE} ${GLOBAL_PROFILE}
    UPDATE_COMMAND ""
    ALWAYS 0
)

