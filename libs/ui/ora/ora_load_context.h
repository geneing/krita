/*
 *  Copyright (c) 2007 Cyrille Berger <cberger@cberger.net>
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _ORA_LOAD_CONTEXT_H_
#define _ORA_LOAD_CONTEXT_H_

#include <kis_open_raster_load_context.h>
#include <kritaui_export.h>

class KoStore;

class KRITAUI_EXPORT OraLoadContext : public KisOpenRasterLoadContext
{
public:
    OraLoadContext(KoStore* _store);
    virtual ~OraLoadContext();
    virtual KisImageWSP loadDeviceData(const QString & fileName);
    virtual QDomDocument loadStack();


private:

    KoStore* m_store;
};

#endif
