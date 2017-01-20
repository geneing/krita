/*
 *  copyright (c) 2006,2010 Cyrille Berger <cberger@cberger.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_random_accessor.h"


#include <kis_debug.h>


const quint32 KisRandomAccessor2::CACHESIZE = 4; // Define the number of tiles we keep in cache

KisRandomAccessor2::KisRandomAccessor2(KisTiledDataManager *ktm, qint32 x, qint32 y, qint32 offsetX, qint32 offsetY, bool writable) :
        m_ktm(ktm),
        m_tilesCacheSize(0),
        m_pixelSize(m_ktm->pixelSize()),
        m_writable(writable),
        m_offsetX(offsetX),
        m_offsetY(offsetY)
{
    Q_ASSERT(ktm != 0);

    m_offsetScale[0] = m_pixelSize;
    m_offsetScale[1] = m_pixelSize*KisTileData::WIDTH;
    m_offsetScale[2] = m_offsetScale[3] = 0;

    m_lastX = x;
    m_lastY = y;
    x -= m_offsetX;
    y -= m_offsetY;

    KisTileInfo firstTile;
    fetchTileData(xToCol(x), yToRow(y), firstTile);
    m_tilesCache.push_front(firstTile);
    moveTo(x, y);
}

KisRandomAccessor2::~KisRandomAccessor2()
{
    for (auto& tile : m_tilesCache) {
        unlockTile(tile.tile.data());
        unlockTile(tile.oldtile.data());
    }
}

void KisRandomAccessor2::moveTo(qint32 x, qint32 y)
{
    m_lastX = x;
    m_lastY = y;

    x -= m_offsetX;
    y -= m_offsetY;
    Vc::int32_v pos;
    pos[0] = x; pos[2] = -x;
    pos[1] = y; pos[3] = -y;

    //shortcircuit case if we are still the current tile
    {
        //pos:  x, y, -x, -y
        //area: x1, y1, -x2, -y2

        KisTileInfo& m_currentTile = m_tilesCache.front();

        //d: x-x1, y-y1, x2-x, y2-y
        Vc::int32_v d = pos - m_currentTile.area;
        if( Vc::all_of(d>=0) ) {
            Vc::int32_v offs = d*m_offsetScale;

            offset = offs[0]+offs[1];
            return;
        }
    }

    // Look in the cache if the tile if the data is available
//    for (uint i = 0; i < m_tilesCacheSize; i++) {

//        qint32 dx1 = x - m_tilesCache[i]->area_x1;
//        qint32 dx2 = m_tilesCache[i]->area_x2 - x;
//        qint32 dy1 = y - m_tilesCache[i]->area_y1;
//        qint32 dy2 = m_tilesCache[i]->area_y2 - y;

//        if (((dx1>=0) & (dx2>=0)) && ((dy1>=0) & (dy2>=0))) {
//            m_currentTile = m_tilesCache[i];
//            offset = (dx1 + dy1 * KisTileData::WIDTH)*m_pixelSize;

//            if (i > 0) {
//                memmove(m_tilesCache + 1, m_tilesCache, i * sizeof(KisTileInfo*));
//                m_tilesCache[0] = m_currentTile;
//            }
//            return;
//        }
//    }

    // The tile wasn't in cache
    if (m_tilesCacheSize == KisRandomAccessor2::CACHESIZE) { // Remove last element of cache
        unlockTile(m_tilesCache.back().tile.data());
        unlockTile(m_tilesCache.back().oldtile.data());
        m_tilesCache.splice(m_tilesCache.begin(), m_tilesCache, std::prev(m_tilesCache.end()));
    } else {
        KisTileInfo newTile;
        m_tilesCache.push_front(newTile);
        m_tilesCacheSize++;
    }

    //memmove(m_tilesCache + 1, m_tilesCache, (KisRandomAccessor2::CACHESIZE - 1) * sizeof(KisTileInfo*));

    quint32 col = xToCol(x);
    quint32 row = yToRow(y);
    fetchTileData(col, row, m_tilesCache.front());
    offset = (x - m_tilesCache.front().area[0] + (y - m_tilesCache.front().area[1]) * KisTileData::WIDTH)*m_pixelSize;
}

const quint8* KisRandomAccessor2::oldRawData() const
{
#ifdef DEBUG
    if (!m_ktm->hasCurrentMemento()) warnTiles << "Accessing oldRawData() when no transaction is in progress.";
#endif
    return m_tilesCache.front().oldData + offset;
}

void KisRandomAccessor2::fetchTileData(qint32 col, qint32 row, KisRandomAccessor2::KisTileInfo& kti)
{
    kti.tile = m_ktm->getTile(col, row, m_writable);
    lockTile(kti.tile.data());

    kti.data = kti.tile->data();

    kti.area[0] = col * KisTileData::HEIGHT;
    kti.area[1] = row * KisTileData::WIDTH;
    kti.area[2] = -(kti.area[0] + KisTileData::HEIGHT - 1);
    kti.area[3] = -(kti.area[1] + KisTileData::WIDTH - 1);

    // set old data
    kti.oldtile = m_ktm->getOldTile(col, row);
    lockOldTile(kti.oldtile.data());
    kti.oldData = kti.oldtile->data();
}

qint32 KisRandomAccessor2::numContiguousColumns(qint32 x) const
{
    return m_ktm->numContiguousColumns(x - m_offsetX, 0, 0);
}

qint32 KisRandomAccessor2::numContiguousRows(qint32 y) const
{
    return m_ktm->numContiguousRows(y - m_offsetY, 0, 0);
}

qint32 KisRandomAccessor2::rowStride(qint32 x, qint32 y) const
{
    return m_ktm->rowStride(x - m_offsetX, y - m_offsetY);
}

qint32 KisRandomAccessor2::x() const
{
    return m_lastX;
}

qint32 KisRandomAccessor2::y() const
{
    return m_lastY;
}
