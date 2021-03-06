/*
 *  Copyright (c) 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef KIS_PRESSURE_MIRROR_OPTION_WIDGET_H
#define KIS_PRESSURE_MIRROR_OPTION_WIDGET_H

#include "kis_curve_option_widget.h"

class QCheckBox;

class PAINTOP_EXPORT KisPressureMirrorOptionWidget : public KisCurveOptionWidget
{
    Q_OBJECT

public:
    KisPressureMirrorOptionWidget();

    void readOptionSetting(const KisPropertiesConfiguration* setting);

private Q_SLOTS:
    void horizontalMirrorChanged(bool mirror);
    void verticalMirrorChanged(bool mirror);

private:
    QCheckBox* m_horizontalMirror;
    QCheckBox* m_verticalMirror;
};

#endif // KIS_PRESSURE_RATE_OPTION_WIDGET_H
