#ifndef KIS_PYTHON_SCRIPTING_TEST_H
#define KIS_PYTHON_SCRIPTING_TEST_H

#include <QObject>
#include "kis_types.h"
#include "kis_image.h"


class TypeDecorators : public QObject
{
  Q_OBJECT
public Q_SLOTS:
    KisImage* data(KisImageWSP *o) { return o->data(); }
    KisImage* data(KisImageSP *o) { return o->data(); }
    KisPaintDevice* data(KisPaintDeviceSP *o) { return o->data(); }
};
#endif
