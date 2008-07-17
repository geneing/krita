/*
 *  kis_control_frame.cc - part of Krita
 *
 *  Copyright (c) 1999 Matthias Elter  <elter@kde.org>
 *  Copyright (c) 2003 Patrick Julien  <freak@codepimps.org>
 *  Copyright (c) 2004 Sven Langkamp  <sven.langkamp@gmail.com>
 *  Copyright (c) 2006 Boudewijn Rempt <boud@valdyas.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.g
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_control_frame.h"

#include <stdlib.h>

#include <QApplication>
#include <QLayout>
#include <QTabWidget>
#include <QFrame>
#include <QWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>

#include <ktoolbar.h>
#include <kxmlguiwindow.h>
#include <kglobalsettings.h>
#include <kstandarddirs.h>
#include <klocale.h>
#include <kaction.h>
#include <kactioncollection.h>
#include <KoDualColorButton.h>
#include <KoAbstractGradient.h>
#include <KoResourceItemChooser.h>
#include <KoResourceServer.h>
#include <KoResourceServerAdapter.h>
#include <KoResourceServerProvider.h>

#include "kis_brush.h"
#include "kis_imagepipe_brush.h"
#include "kis_pattern.h"
#include "kis_resource_server_provider.h"
#include "kis_canvas_resource_provider.h"

#include "kis_resource_mediator.h"
#include "widgets/kis_iconwidget.h"
#include "kis_brush.h"
#include "kis_pattern.h"
#include "widgets/kis_brush_chooser.h"
#include "widgets/kis_gradient_chooser.h"
#include "kis_view2.h"
#include "widgets/kis_auto_brush_widget.h"
#include "kis_config.h"
#include "kis_paintop_box.h"
#include "kis_custom_brush.h"
#include "kis_custom_pattern.h"
#include "widgets/kis_pattern_chooser.h"
#ifdef HAVE_TEXT_BRUSH
#include "kis_text_brush.h"
#endif


KisControlFrame::KisControlFrame( KisView2 * view, const char* name )
    : QObject(view)
    , m_view(view)
    , m_brushWidget(0)
    , m_patternWidget(0)
    , m_gradientWidget(0)
    , m_brushChooserPopup(0)
    , m_patternChooserPopup(0)
    , m_gradientChooserPopup(0)
    , m_brushMediator(0)
    , m_patternMediator(0)
    , m_gradientMediator(0)
    , m_paintopBox(0)
{
    setObjectName(name);
    KisConfig cfg;
    m_font  = KGlobalSettings::generalFont();

    m_brushWidget = new KisIconWidget(view, "brushes");
    m_brushWidget->setText( i18n("Brush Shapes") );
    m_brushWidget->setToolTip( i18n("Brush Shapes") );
    // XXX: An action without a slot -- that's silly, what kind of action could we use here?
    KAction *action  = new KAction(i18n("&Brush"), this);
    view->actionCollection()->addAction("brushes", action );
    action->setDefaultWidget( m_brushWidget );

    m_patternWidget = new KisIconWidget(view, "patterns");
    m_patternWidget->setText( i18n("Fill Patterns") );
    m_patternWidget->setToolTip( i18n("Fill Patterns") );
    action  = new KAction(i18n("&Patterns"), this);
    view->actionCollection()->addAction("patterns", action );
    action->setDefaultWidget( m_patternWidget );

    m_gradientWidget = new KisIconWidget(view, "gradients");
    m_gradientWidget->setText( i18n("Gradients") );
    m_gradientWidget->setToolTip( i18n("Gradients") );
    action  = new KAction(i18n("&Gradients"), this);
    view->actionCollection()->addAction("gradients", action );
    action->setDefaultWidget( m_gradientWidget );

/**** Temporary hack to test the KoDualColorButton ***/
    KoDualColorButton * dual = new KoDualColorButton(view->resourceProvider()->fgColor(), view->resourceProvider()->fgColor(), view, view);
    action  = new KAction(i18n("&Painter's Tools"), this);
    view->actionCollection()->addAction("dual", action );
    action->setDefaultWidget( dual );
    connect(dual, SIGNAL(foregroundColorChanged(const KoColor &)), view->resourceProvider(), SLOT(slotSetFGColor(const KoColor &)));
    connect(dual, SIGNAL(backgroundColorChanged(const KoColor &)), view->resourceProvider(), SLOT(slotSetBGColor(const KoColor &)));
    connect(view->resourceProvider(), SIGNAL(sigFGColorChanged(const KoColor &)), dual, SLOT(setForegroundColor(const KoColor &)));
    dual->setFixedSize( 26, 26 );
/*******/
    m_brushWidget->setFixedSize( 26, 26 );
    m_patternWidget->setFixedSize( 26, 26 );
    m_gradientWidget->setFixedSize( 26, 26 );

    createBrushesChooser(m_view);
    createPatternsChooser(m_view);
    createGradientsChooser(m_view);

    m_brushWidget->setPopupWidget(m_brushChooserPopup);
    m_patternWidget->setPopupWidget(m_patternChooserPopup);
    m_gradientWidget->setPopupWidget(m_gradientChooserPopup);


    m_paintopBox = new KisPaintopBox( view, view, "paintopbox" );
    action  = new KAction(i18n("&Painter's Tools"), this);
    view->actionCollection()->addAction("paintops", action );
    action->setDefaultWidget( m_paintopBox );

}


void KisControlFrame::slotSetBrush(QTableWidgetItem *item)
{
    m_brushWidget->slotSetItem(*item);
}

void KisControlFrame::slotSetPattern(QTableWidgetItem *item)
{
    m_patternWidget->slotSetItem(*item);
}

void KisControlFrame::slotSetGradient(QTableWidgetItem *item)
{
    m_gradientWidget->slotSetItem(*item);
}

void KisControlFrame::slotBrushChanged(KisBrush * brush)
{
    KoResourceItem *item;
    if(brush) {
        if((item = m_brushMediator->itemFor(brush)))
        {
                slotSetBrush(item);
        } else {
                slotSetBrush( new KoResourceItem(brush) );
        }
    }
    else
        slotSetBrush(0);
}

void KisControlFrame::slotPatternChanged(KisPattern * pattern)
{
    KoResourceItem *item;
    if (pattern) {
        if ( (item = m_patternMediator->itemFor(pattern)) )
                slotSetPattern(item);
        else
                slotSetPattern( new KoResourceItem(pattern) );
    }
    else
        slotSetPattern(0);
}


void KisControlFrame::slotGradientChanged(KoAbstractGradient * gradient)
{
    KoResourceItem *item;
    if (gradient) {
        if ( (item = m_gradientMediator->itemFor(gradient)) )
                slotSetGradient(item);
        else
                slotSetGradient( new KoResourceItem(gradient) );
    }
    else {
        slotSetGradient(0);
    }
}

void KisControlFrame::createBrushesChooser(KisView2 * view)
{

    m_brushChooserPopup = new QWidget(m_brushWidget);
    m_brushWidget->setObjectName( "brush_chooser_popup" );

    QHBoxLayout * l = new QHBoxLayout(m_brushChooserPopup);
    l->setObjectName("brushpopuplayout");
    l->setMargin(2);
    l->setSpacing(2);

    m_brushesTab = new QTabWidget(m_brushChooserPopup);
    m_brushesTab->setObjectName("brushestab");
    m_brushesTab->setFocusPolicy(Qt::StrongFocus);
    m_brushesTab->setFont(m_font);
    m_brushesTab->setContentsMargins(1, 1, 1, 1);

    l->addWidget(m_brushesTab);

    KisAutoBrushWidget * m_autoBrushWidget = new KisAutoBrushWidget(0, "autobrush", i18n("Autobrush"));
    m_brushesTab->addTab( m_autoBrushWidget, i18n("Autobrush"));

    connect(m_autoBrushWidget,
            SIGNAL(activatedResource(KoResource*)),
            m_view->resourceProvider(),
            SLOT(slotBrushActivated( KoResource* )));

    m_brushChooser = new KisBrushChooser(m_brushesTab);
    m_brushesTab->addTab( m_brushChooser, i18n("Predefined Brushes"));

    KisCustomBrush* customBrushes = new KisCustomBrush(0, "custombrush",
            i18n("Custom Brush"), m_view);
    m_brushesTab->addTab( customBrushes, i18n("Custom Brush"));

    connect(customBrushes,
            SIGNAL(activatedResource(KoResource*)),
            m_view->resourceProvider(),
            SLOT(slotBrushActivated(KoResource*)));

#ifdef HAVE_TEXT_BRUSH
    KisTextBrush* textBrushes = new KisTextBrush(0, "textbrush",
            i18n("Text Brush")/*, m_view*/);
    m_brushesTab->addTab( textBrushes, i18n("Text Brush"));

    connect(textBrushes,
            SIGNAL(activatedResource(KoResource*)),
            m_view->resourceProvider(),
            SLOT(slotBrushActivated(KoResource*)));
#endif

    m_brushChooserPopup->setLayout(l);
    m_brushChooser->setFont(m_font);

    KoResourceServer<KisBrush>* rServer = KisResourceServerProvider::instance()->brushServer();
    KoResourceServerAdapter<KisBrush>* rServerAdapter;
    rServerAdapter = new KoResourceServerAdapter<KisBrush>(rServer);

    m_brushMediator = new KisResourceMediator( m_brushChooser, rServerAdapter, this);
    connect(m_brushMediator, SIGNAL(activatedResource(KoResource*)),
            m_view->resourceProvider(), SLOT(slotBrushActivated(KoResource*)));

    KisControlFrame::connect(view->resourceProvider(), SIGNAL(sigBrushChanged(KisBrush *)),
                             this, SLOT(slotBrushChanged( KisBrush *)));

    m_brushChooser->setCurrent( 0 );
    m_brushMediator->setActiveItem( m_brushChooser->currentItem() );

    m_autoBrushWidget->activate();
}

void KisControlFrame::createPatternsChooser(KisView2 * view)
{
    m_patternChooserPopup = new QWidget(m_patternWidget);
    m_patternChooserPopup->setObjectName( "pattern_chooser_popup" );
    QHBoxLayout * l2 = new QHBoxLayout(m_patternChooserPopup);
    l2->setObjectName("patternpopuplayout");
    l2->setMargin(2);
    l2->setSpacing(2);

    m_patternsTab = new QTabWidget(m_patternChooserPopup);
    m_patternsTab->setObjectName("patternstab");
    m_patternsTab->setFocusPolicy(Qt::NoFocus);
    m_patternsTab->setFont(m_font);
    m_patternsTab->setContentsMargins(1, 1, 1, 1);
    l2->addWidget( m_patternsTab );

    KisPatternChooser * chooser = new KisPatternChooser(m_patternChooserPopup);
    chooser->setFont(m_font);
    m_patternsTab->addTab(chooser, i18n("Patterns"));

    KisCustomPattern* customPatterns = new KisCustomPattern(0, "custompatterns",
            i18n("Custom Pattern"), m_view);
    customPatterns->setFont(m_font);
    m_patternsTab->addTab( customPatterns, i18n("Custom Pattern"));


    KoResourceServer<KisPattern>* rServer = KisResourceServerProvider::instance()->patternServer();
    KoResourceServerAdapter<KisPattern>* rServerAdapter;
    rServerAdapter = new KoResourceServerAdapter<KisPattern>(rServer);

    m_patternMediator = new KisResourceMediator( chooser, rServerAdapter, view);

    connect( m_patternMediator, SIGNAL(activatedResource(KoResource*)),
             view->resourceProvider(), SLOT(slotPatternActivated(KoResource*)));

    connect(customPatterns, SIGNAL(activatedResource(KoResource*)),
            view->resourceProvider(), SLOT(slotPatternActivated(KoResource*)));

    connect( view->resourceProvider(), SIGNAL(sigPatternChanged(KisPattern *)),
             this, SLOT(slotPatternChanged( KisPattern *)));

    chooser->setCurrent( 0 );
    m_patternMediator->setActiveItem( chooser->currentItem() );
}


void KisControlFrame::createGradientsChooser(KisView2 * view)
{
    m_gradientChooserPopup = new QWidget(m_gradientWidget);
    m_gradientChooserPopup->setObjectName( "gradient_chooser_popup" );
    QHBoxLayout * l2 = new QHBoxLayout(m_gradientChooserPopup);
    l2->setObjectName("gradientpopuplayout");
    l2->setMargin(2);
    l2->setSpacing(2);

    m_gradientTab = new QTabWidget(m_gradientChooserPopup);
    m_gradientTab->setObjectName("gradientstab");
    m_gradientTab->setFocusPolicy(Qt::NoFocus);
    m_gradientTab->setFont(m_font);
    m_gradientTab->setContentsMargins(1, 1, 1, 1);
    l2->addWidget( m_gradientTab);

    m_gradientChooser = new KisGradientChooser(m_view, m_gradientChooserPopup);
    m_gradientChooser->setFont(m_font);
    m_gradientTab->addTab( m_gradientChooser, i18n("Gradients"));

    KoResourceServer<KoAbstractGradient>* rServer = KoResourceServerProvider::instance()->gradientServer();
    KoResourceServerAdapter<KoAbstractGradient>* rServerAdapter;
    rServerAdapter = new KoResourceServerAdapter<KoAbstractGradient>(rServer);

    m_gradientMediator = new KisResourceMediator( m_gradientChooser, rServerAdapter, view);
    connect(m_gradientMediator, SIGNAL(activatedResource(KoResource*)),
            view->resourceProvider(), SLOT(slotGradientActivated(KoResource*)));

    connect(view->resourceProvider(), SIGNAL(sigGradientChanged(KoAbstractGradient *)),
            this, SLOT(slotGradientChanged( KoAbstractGradient *)));

    m_gradientChooser->setCurrent( 0 );
    m_gradientMediator->setActiveItem( m_gradientChooser->currentItem() );
}

#include "kis_control_frame.moc"

