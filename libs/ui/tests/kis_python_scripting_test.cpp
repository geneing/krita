
#include "PythonQt.h"
#include "PythonQt_QtAll.h"
#include "gui/PythonQtScriptingConsole.h"

#include "kis_python_scripting_test.h"

#include <KisDocument.h>
#include <QApplication>
#include "util.h"
#include "kis_types.h"


int main( int argc, char* argv[] )
{
    QApplication qapp(argc, argv);

    PythonQt::init(PythonQt::IgnoreSiteModule | PythonQt::RedirectStdOut);
    PythonQt_QtAll::init();

    PythonQtObjectPtr  mainContext = PythonQt::self()->getMainModule();
    PythonQtScriptingConsole console(NULL, mainContext);


    PythonQt::self()->addDecorators(new TypeDecorators());

    //qRegisterMetaType<KisImage>("KisImage");
    qRegisterMetaType<KisImageSP>("KisImageSP");
    qRegisterMetaType<KisImageWSP>("KisImageWSP");

    KisDocument *doc = KisPart::instance()->createDocument();
    doc->loadNativeFormat(QString(FILES_DATA_DIR) + QDir::separator() + "load_test.kra");
    KisImageSP image = doc->image();

    mainContext.addObject("doc", doc);
    mainContext.addObject("image", image.data());

    //mainContext.evalFile(":example.py");

    console.show();
    return qapp.exec();
}
