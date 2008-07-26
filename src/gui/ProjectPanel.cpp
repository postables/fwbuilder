/*

                          Firewall Builder

                 Copyright (C) 2008 NetCitadel, LLC

  Author:  alek@codeminders.com

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "../../config.h"
#include "global.h"
#include "utils.h"
#include "utils_no_qt.h"

#include "ProjectPanel.h"

#include <ui_rcsfilesavedialog_q.h>

#include "ObjectTreeView.h"
#include "ObjectManipulator.h"
#include "FWObjectClipboard.h"
#include "FWBTree.h"
#include "FWBSettings.h"
#include "FWObjectPropertiesFactory.h"
#include "upgradePredicate.h"
#include "listOfLibraries.h"
#include "ObjConflictResolutionDialog.h"
#include "RuleSetView.h"
#include "RCSFilePreview.h"
#include "ObjectEditor.h"
#include "LibExportDialog.h"
#include "findDialog.h"
#include "FindObjectWidget.h"
#include "FindWhereUsedWidget.h"
#include "longTextDialog.h"
#include "FWWindow.h"
#include "RCS.h"
#include "filePropDialog.h"

#include "fwbuilder/FWReference.h"
#include "fwbuilder/Policy.h"
#include "fwbuilder/NAT.h"
#include "fwbuilder/Routing.h"
#include "fwbuilder/Tools.h"
#include "fwbuilder/FWObjectDatabase.h"
#include "fwbuilder/FWException.h"
#include "fwbuilder/Management.h"
#include "fwbuilder/RuleElement.h"
#include "fwbuilder/Library.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/Interface.h"
#include "fwbuilder/RuleSet.h"
#include "fwbuilder/Interval.h"
#include "fwbuilder/FWObject.h"
#include "fwbuilder/Rule.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <algorithm>

#include <QMdiSubWindow>
#include <QMdiArea>


using namespace Ui;
using namespace libfwbuilder;

#define DEFAULT_H_SPLITTER_POSITION 250
#define DEFAULT_V_SPLITTER_POSITION 450


void ProjectPanel::initMain(FWWindow *main)
{
    mainW = main;
    closing = false ;

    // mdiWindow changes state several times right after it is opened,
    // but we call saveState to store splitter position and its geometry
    // when state changed. Flag "ready" is false after ProjectPanel is created
    // and until FWWindow decides that ProjectPanel is ready for operation.
    // Do not load or save state if flag ready is false.
    ready = false;

    enableAvtoSaveState=true ;
    oldState=-1;
    if (st->getInfoStyle()!=0) m_panel->oi->show();
    else m_panel->oi->hide();
    
    findObjectWidget = new FindObjectWidget(
        m_panel->auxiliaryPanel, "findObjectWidget" );
    findObjectWidget->setFocusPolicy( Qt::NoFocus );
    m_panel->auxiliaryPanel->layout()->addWidget( findObjectWidget );
    connect( findObjectWidget, SIGNAL( close() ),
             this, SLOT( closeAuxiliaryPanel() ) );

    findWhereUsedWidget = new FindWhereUsedWidget(
        m_panel->auxiliaryPanel, "findWhereUsedWidget");
    findWhereUsedWidget->setFocusPolicy( Qt::NoFocus );
    m_panel->auxiliaryPanel->layout()->addWidget( findWhereUsedWidget );
    findWhereUsedWidget->hide();
    connect( findWhereUsedWidget, SIGNAL( close() ),
             this, SLOT( closeAuxiliaryPanel() ) );
    connect( m_panel->infoStyleButton, SIGNAL( clicked() ),
             this, SLOT( changeInfoStyle() ) );
    connect(m_panel->mainSplitter, SIGNAL(splitterMoved(int,int)),
            this,SLOT(splitterMoved(int,int)));
    connect(m_panel->objInfoSplitter, SIGNAL(splitterMoved(int,int)),
            this,SLOT(splitterMoved(int,int)));

    m_panel->auxiliaryPanel->hide();
    initOE();
    initFD();
}

ProjectPanel::ProjectPanel(QWidget *parent): 
    QWidget(parent), // , Qt::WindowSystemMenuHint|Qt::Window),
    mainW(0),
    rcs(0),
    addOnLibs(new listOfLibraries()), 
    objectTreeFormat(new FWBTree),
    systemFile(true),
    safeMode(false),
    editingStandardLib(false),
    editingTemplateLib(false),
    ruleSetRedrawPending(false),
    objdb(0),
    editorOwner(0), 
    oe(0),
    fd(0),
    shownInInfo(0),
    autosaveTimer(new QTimer(static_cast<QObject*>(this))), ruleSetTabIndex(0),
    visibleFirewall(0),
    visibleRuleSet(0),
    lastFirewallIdx(-2),
    changingTabs(false),
    noFirewalls(tr("No firewalls defined")),
    m_panel(0),
    findObjectWidget(0), 
    findWhereUsedWidget(0)
{
    if (fwbdebug)
        qDebug("ProjectPanel constructor");
    m_panel = new Ui::ProjectPanel_q();
    m_panel->setupUi(this);
    firstTimeNormal = st->getInt("Window/maximized");
}


ProjectPanel::~ProjectPanel()
{
    if (rcs) delete rcs;
    delete m_panel;
}

void ProjectPanel::info(libfwbuilder::FWObject *obj, bool forced)
{
    if (obj==NULL)
        return ;
    if (st->getInfoStyle()!=0 && (shownInInfo!=obj || forced))
    {
        m_panel->oi->clear();

        QString s="";
        if (st->getInfoStyle()==2)
        {
            s=FWObjectPropertiesFactory::getObjectPropertiesDetailed(obj)
                + QString("<hr height=\"0\">");
            m_panel->oi->setText(s);
        }

        s=QString::fromUtf8(obj->getComment().c_str());
        m_panel->oi->append(s);
        m_panel->oi->moveCursor(QTextCursor::Start);

        shownInInfo = obj;
    }

//    unselectRules();
}

void ProjectPanel::setStartupFileName(const QString &fn) 
{ 
    startupFileName = fn; 
}

RuleElement* ProjectPanel::getRE(Rule* r, int col )
{
    string ret;
    switch (col)
    {
        case 0: ret=RuleElementSrc::TYPENAME; break;//Object
        case 1: ret=RuleElementDst::TYPENAME; break;//Object
        case 2: ret=RuleElementSrv::TYPENAME; break;//Object
        case 3: ret=RuleElementItf::TYPENAME; break;//Object
        case 4: ret=RuleElementInterval::TYPENAME; break;//Time
        default: return NULL;
    }

    return RuleElement::cast( r->getFirstByType(ret) );
}

/*
 * info styles go like this:
 * 0 - collapsed
 * 1 - opened
 * 2 - opened, more information
 *  etc.
 *
 */
void ProjectPanel::changeInfoStyle()
{
    shownInInfo = NULL;
    switch (st->getInfoStyle())
    {
    case 0:
        st->setInfoStyle(1);
        m_panel->oi->show();
        break;
    case 1:
        st->setInfoStyle(2);
        m_panel->oi->show();
        break;
    case 2:
        st->setInfoStyle(0);
        m_panel->oi->hide();
        break;
    }

/* TODO: now need to send signal to the dialog telling it to change
 * according to the style
 */

    info();
}

void ProjectPanel::restoreRuleSetTab()
{
    if (fwbdebug) qDebug("ProjectPanel::()");
    m_panel->ruleSets->setCurrentIndex(ruleSetTabIndex);

}

void ProjectPanel::loadObjects()
{
    m_panel->om->loadObjects();
}

void ProjectPanel::loadObjects(libfwbuilder::FWObjectDatabase *db)
{
    m_panel->om->loadObjects(db);
}

void ProjectPanel::clearObjects()
{
    m_panel->om->clearObjects();
}



void ProjectPanel::initOE()
{
    oe  = new ObjectEditor((QWidget*)m_panel->objectEditorStack, this);
    oe->setCloseButton(m_panel->closeObjectEditorButton);
    oe->setApplyButton(m_panel->applyObjectEditorButton);
    oe->hide();
}

void ProjectPanel::initFD()
{
    fd  = new findDialog(this, this);
    fd->hide();
}

void ProjectPanel::clearFirewallTabs()
{
    if (fwbdebug) qDebug("ProjectPanel::clearFirewallTabs");

    m_panel->ruleSets->hide();

    while (m_panel->ruleSets->count()!=0)
    {
        QWidget *p = m_panel->ruleSets->widget(0);
        m_panel->ruleSets->removeWidget(m_panel->ruleSets->widget(m_panel->ruleSets->indexOf(p)));
        delete p;
    }
    m_panel->ruleSets->show();
    ruleSetViews.clear();
}

void ProjectPanel::ensureObjectVisibleInRules(FWReference *obj)
{
    FWObject *p=obj;
    while (p && RuleSet::cast(p)==NULL ) p=p->getParent();
    if (p==NULL) return;  // something is broken
    openRuleSet(p);
    getCurrentRuleSetView()->selectRE( obj );    
}

/*
 * Ensure rule is visible and highlight given column
 */
void ProjectPanel::ensureRuleIsVisible(Rule *rule, int col)
{
    FWObject *p = rule;

    while (p && RuleSet::cast(p)==NULL ) p=p->getParent();
    if (p==NULL) return;  // something is broken

    RuleSetView *rsv = ruleSetViews[p];

    if (rsv==NULL)
    {
        if (fwbdebug)
            qDebug("ProjectPanel::ensureRuleIsVisible : orphan rule set found");
        return;
    }

    m_panel->ruleSets->setCurrentIndex(m_panel->ruleSets->indexOf(rsv));
    rsv->selectRE( rule->getPosition(), col );
}

void ProjectPanel::updateRuleSetViewSelection()
{
    RuleSetView* rv = dynamic_cast<RuleSetView*>(
        m_panel->ruleSets->currentWidget());
    if (rv!=NULL)
        rv->repaintSelection();
}

void ProjectPanel::updateRuleSetView()
{
    RuleSetView* rv=dynamic_cast<RuleSetView*>(
        m_panel->ruleSets->currentWidget());
    if (rv!=NULL) rv->updateAll();
}

void ProjectPanel::updateRuleOptions()
{
    RuleSetView* rv = dynamic_cast<RuleSetView*>(
        m_panel->ruleSets->currentWidget());
    if (rv!=NULL) rv->updateCurrentCell();
}

void ProjectPanel::updateTreeViewItemOrder()
{
    //this is for case when tree becomes to be resorted
    //if we do not reopen parent item, some of child
    //items mix incorrectly (maybe bug of QT?)
    m_panel->om->reopenCurrentItemParent();
}

RuleSetView * ProjectPanel::getCurrentRuleSetView () 
{
    return dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget ());
}


void ProjectPanel::reopenFirewall()
{
    if (fwbdebug)  qDebug("ProjectPanel::reopenFirewall()");

    if (ruleSetRedrawPending) return;

    int currentPage = m_panel->ruleSets->currentIndex();

    changingTabs=true;

    clearFirewallTabs();
    
/*    if (firewalls.size()==0 || visibleFirewall==NULL)
    {
        changingTabs=false;
        return;
    }
*/
    QStatusBar *sb = mainW->statusBar();
    sb->showMessage( tr("Building policy view...") );
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
    if (fwbdebug)  qDebug("ProjectPanel::reopenFirewall()   adding Policy tab");
/*
    RuleSetView *rsv;
    Policy *pol=Policy::cast(visibleFirewall->getFirstByType(Policy::TYPENAME));
    m_panel->ruleSets->addTab( rsv=new PolicyView(this, pol,NULL) , tr("Policy") );
    ruleSetViews[pol]=rsv;
*/
// let the GUI process events to display new tab
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
    if (visibleRuleSet==NULL)
        return ;

    for (int i =0 ; i < m_panel->ruleSets->count (); i++)
    {
        m_panel->ruleSets->removeWidget(m_panel->ruleSets->widget(i));
    }
    m_panel->rulesetname->setTextFormat(Qt::RichText);
    QString name = "<B>";
    FWObject * p = visibleRuleSet->getParent();
    name += p->getName().c_str();
    name += " / ";
    name += visibleRuleSet->getName().c_str();
    name += "</B>";
    Policy *rule = Policy::cast(visibleRuleSet);
    m_panel->rulesetname->setText(name );

    if (rule!=NULL)
    {
        m_panel->ruleSets->addWidget( new PolicyView(this, rule,NULL) );
    }

    NAT *nat  = NAT::cast(visibleRuleSet);
    if (nat!=NULL)
    {
        m_panel->ruleSets->addWidget( new NATView(this, nat,NULL) );
    }

    Routing *r = Routing::cast(visibleRuleSet);
    if (r!=NULL)
    {
        m_panel->ruleSets->addWidget( new RoutingView(this, r,NULL) );
    }

    sb->clearMessage();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
    if (fwbdebug)  qDebug("ProjectPanel::reopenFirewall()   all tabs are done");

    m_panel->ruleSets->setCurrentIndex( currentPage );

    changingTabs=false;
    mainW->setEnabledAfterRF();
    m_panel->ruleSets->show();
}

int  ProjectPanel::findFirewallInList(FWObject *f)
{
    vector<FWObject*>::iterator i;
    int n=0;
    for (i=firewalls.begin(); i!=firewalls.end(); i++,n++)
    {
        if ( (*i)->getId()==f->getId() ) return n;
    }
    return -1;
}

void ProjectPanel::updateFirewallName(FWObject *, const QString &)
{
    if (visibleRuleSet==NULL)
        return ;

    QString name = "<B>";
    FWObject * p = visibleRuleSet->getParent();
    name += p->getName().c_str();
    name += " / ";
    name += visibleRuleSet->getName().c_str();
    name += "</B>";
    m_panel->rulesetname->setText(name );
}

void ProjectPanel::openRuleSet(FWObject * obj)
{
    blankEditor();
    RuleSet * rs = RuleSet::cast(obj);
    if (rs!= visibleRuleSet)
    {
         visibleRuleSet = rs;
        scheduleRuleSetRedraw();
    }
}

void ProjectPanel::selectRules()
{
//    `unselect();
    RuleSetView* rv=dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());
    rv->setFocus();
}

void ProjectPanel::unselectRules()
{
    bool havePolicies = (m_panel->ruleSets->count()!=0);

/* commented this out so that when I hit "Edit" in the object's pop-down
 * menu in a rule, ruleset wont lose focus when object editor is opened.
 * If rule set loses focus, the object's background turns from "selected" color
 * to white and user loses context (which object is shown in the object editor)
 */
    if (havePolicies)
    {
        RuleSetView* rv=dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());

        if (rv && rv->getSelectedObject()!=getSelectedObject())
        {
            rv->unselect();
            rv->clearFocus();
        }
    }
    mainW->disableActions(havePolicies);
}

void ProjectPanel::editCopy()
{
    if (isManipulatorSelected()) copyObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->copySelectedObject();
}

void ProjectPanel::editCut()
{
    if (isManipulatorSelected()) cutObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->cutSelectedObject();
}

void ProjectPanel::editDelete()
{
    if (isManipulatorSelected()) deleteObj();
    //else
    //    if (ruleSets->count()!=0)
    //        dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->deleteSelectedObject();
}

void ProjectPanel::editPaste()
{
    if (isManipulatorSelected()) pasteObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->pasteObject();
}

bool ProjectPanel::saveIfModified()
{
    if (db() && db()->isDirty())
    {
        QString message = "Some objects have been modified but not saved.\n";
        message+="Do you want to save ";
        message+=rcs->getFileName();
        message+=" changes now ?"; 
        switch (QMessageBox::information(this, "Firewall Builder",
            message,
            tr("&Save"), tr("&Discard"), tr("&Cancel"),
            0,       // Enter = button 0
            2 ) ) {   // Escape == button 2

        case 0:
            save();
            break;
        case 1:  // discard
            db()->setDirty(false);
            break;
        case 2:  // cancel
            return(false);
        }
    }
    return true;
}

QString ProjectPanel::getDestDir(const QString &fname)
{
    QString destdir = "";

    if (st->getWDir().isEmpty())
    {
        if (fname.isEmpty())
        {
/* need some reasonable default working directory.
 * on Unix will use current dir.
 * on Windows will use user's document dir.
 */
#if defined(Q_OS_WIN32) || defined(Q_OS_MACX)
            destdir = userDataDir.c_str();
#else
            destdir = "";
#endif
        } else
        {
            if (QFileInfo(fname).isDir()) destdir=fname;
            else
                destdir = fname.left( fname.lastIndexOf('/',-1) );
        }
    } else
    {
        destdir=st->getWDir();
    }
    return destdir;
}

QString ProjectPanel::chooseNewFileName(const QString &fname,
                                        bool checkPresence,
                                        const QString &title)
{
    QString destdir = getDestDir(fname);

    QString fn = QFileDialog::getSaveFileName( this, title, destdir,
       tr( "FWB Files (*.fwb);;All Files (*)" ) );
    if ( fn.isEmpty() ) return "";

    QFileInfo finfo(fn);

    //if (finfo.extension(false)!="fwb") fn=fn+".fwb";
    if (finfo.suffix()!="fwb") fn=fn+".fwb";

    finfo.setFile(fn);

    if ( ! checkPresence || ! finfo.exists() ||
             QMessageBox::warning(
                 this,"Firewall Builder",
                 tr("The file %1 already exists.\nDo you want to overwrite it ?")
                 .arg(fn.toLatin1().constData()),
                 tr("&Yes"), tr("&No"), QString::null,
                 0, 1 )==0 )
    {
        return fn;
    }

    return "";
}

void ProjectPanel::setFileName(const QString &fname)
{
    systemFile=false;
    rcs->setFileName(fname);
    db()->setFileName(fname.toLatin1().constData());

    QString caption = rcs->getFileName().section("/",-1,-1);
    if (rcs->isInRCS()) caption = caption + ", rev " + rcs->getSelectedRev();

    setWindowTitle( QString("Firewall Builder: ")+caption );
}

void ProjectPanel::fileProp()
{
    if (rcs!=NULL)
    {
        filePropDialog fpd(this,rcs);
        fpd.setPrinter(mainW->getPrinter());
        fpd.exec();
    }
}

bool ProjectPanel::fileNew()
{
    QString nfn = chooseNewFileName(
        st->getWDir(),true,
        tr("Choose name and location for the new file"));

    if ( !nfn.isEmpty() )
    {
        //if (!saveIfModified() || !checkin(true)) return;
        if (!systemFile && rcs!=NULL) 
            fileClose();       // fileClose calls load(this)
        else  
            load(this);

        visibleFirewall = NULL;
        setFileName(nfn);
        save();
        setupAutoSave();
    }
    if (rcs!=NULL)
    {
    	mainW->addToRCSActionSetEn(
            !rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
    	mainW->fileDiscardActionSetEn(
            rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
    	mainW->fileCommitActionSetEn(
            rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
    	mainW->fileSaveActionSetEn( !rcs->isRO() && !rcs->isTemp() );
        return true ;
    }
    else
    {
        return false ;
    }

}

bool ProjectPanel::fileOpen()
{
    if (fwbdebug) qDebug("ProjectPanel::fileOpen(): start");

    QString dir;
    dir=st->getWDir();
    if (dir.isEmpty())  dir=st->getOpenFileDir();
    if (dir.isEmpty())  dir=userDataDir.c_str();

    QString fileName = QFileDialog::getOpenFileName(
        mainW,
        tr("Open File"),
        dir,
        tr("FWB files (*.fwb *.fwl *.xml);;All Files (*)"));

    if (fileName.isEmpty()) return false;

    RCSFilePreview  fp(this);

    bool hasRCS = fp.showFileRLog(fileName);

    if ( (!hasRCS) || (fp.exec() == QDialog::Accepted) )
    {
        if (!saveIfModified() || !checkin(true)) return false;
        if (!systemFile && rcs!=NULL) fileClose();

        //try to get simple rcs instance from RCS preview
        RCS *rcs = fp.getSelectedRev();

        //if preview cannot give RCS,
        //get a new RCS from file dialog
        if (rcs==NULL)
            rcs = new RCS(fileName);

        //if RCS isn't still formed, it's an error
        if (rcs==NULL)
            return false;

/***********************************************************************
 * TODO : add an option "RCS support"
 *
 * if opening read-only, do not checkout
 * checkout may throw exception, need to catch it
 */
        try
        {
            rcs->co();

        } catch (FWException &ex)
        {
/* if there was an exception, abort operation. E.g. RCS::co may throw
 * exception */
            return false;
        }
/***********************************************************************/



        

        QList<QMdiSubWindow *> subWindowList = mw->getMdiArea()->subWindowList();
        QString fileName = rcs->getFileName();
        for (int i = 0 ; i < subWindowList.size();i++)
        {
            ProjectPanel * pp = dynamic_cast<ProjectPanel*>(
                subWindowList[i]->widget());
            if (pp!=NULL && pp->getFileName() == fileName)
            {
                load(this, rcs, pp->objdb );

                if (rcs->isTemp())
                    unlink(rcs->getFileName().toLatin1().constData());
                return true ;
            }
        }
        load(this, rcs );

        if (rcs->isTemp()) unlink(rcs->getFileName().toLatin1().constData());
        return true;
    }
    return false;
}

void ProjectPanel::storeLastOpenedLib()
{
    FWObject*  obj = m_panel->om->getCurrentLib();
    if (obj!=NULL)
    {
        std::string sid = FWObjectDatabase::getStringId(obj->getId());
        QString FileName ;
        if (rcs!=NULL)
        {
            FileName = rcs->getFileName();
        }
        st->setStr("Window/" + FileName + "/LastLib", sid.c_str() );
        
    }
}
    
void ProjectPanel::loadLastOpenedLib(QString filename)
{
    QString FileName = filename;
    if (rcs!=NULL)
    {
        FileName = rcs->getFileName();
    }
    QString sid = st->getStr("Window/" + FileName + "/LastLib");
    if (sid!="")
    {
        m_panel->om->libChangedById(FWObjectDatabase::getIntId(sid.toAscii().data()));
    }        
    else
    {
        m_panel->om->changeFirstNotSystemLib();
    }
}

void ProjectPanel::fileClose()
{
    if (fwbdebug) qDebug("ProjectPanel::fileClose(): start");

    closing=true ;

    findObjectWidget->init();
    if (isEditorVisible()) hideEditor();

    if (!saveIfModified() || !checkin(true))  return;

    if (rcs) delete rcs;
    rcs=NULL;

    if (fwbdebug) qDebug("ProjectPanel::fileClose(): clearing widgets");

    FWObjectClipboard::obj_clipboard->clear();

    mdiWindow->close();

    firewalls.clear();
    visibleFirewall = NULL;
    visibleRuleSet = NULL;
    clearFirewallTabs();
    clearObjects();

    if (fwbdebug) qDebug("ProjectPanel::fileClose(): done");
}

void ProjectPanel::fileSave()
{
    QStatusBar *sb = mainW->statusBar();
    sb->showMessage( tr("Saving data to file...") );
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
    save();
    sb->clearMessage();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
}

void ProjectPanel::fileSaveAs()
{
    if (isEditorVisible()) hideEditor();

/* we need to save data into the current file before we save it into a
 * new file, provided we do have current file
    if (!systemFile && rcs &&
        !rcs->isRO() && !rcs->isTemp() && !rcs->getFileName().isEmpty() &&
        (!saveIfModified() || !checkin(true))
    ) return;

 */


/* need to close the file without asking and saving, then reopen it again */

    db()->setDirty(false);  // so it wont ask if user wants to save
    rcs->abandon();

    QString oldFileName = rcs->getFileName();
    if (rcs!=NULL) delete rcs;

    rcs = new RCS("");

    QString nfn = chooseNewFileName(
        oldFileName, true,
        tr("Choose name and location for the file"));

    if (!nfn.isEmpty())
    {
        setFileName(nfn);

        save();

        mainW->addToRCSActionSetEn( !rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
        mainW->fileDiscardActionSetEn( rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
        mainW->fileCommitActionSetEn( rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
        mainW->fileSaveActionSetEn( !rcs->isRO() && !rcs->isTemp() );
    }
}

void ProjectPanel::fileExit()
{
    if (saveIfModified() && checkin(true))
    {
        saveState();
        storeLastOpenedLib();
        if (rcs) delete rcs;
        qApp->quit();
    }
}

void ProjectPanel::fileCommit()
{
    save();
    if (!checkin(true))  return;
    rcs->co();
}

/*
 * discard changes done to the file and check out clean copy of the
 * head revision from RCS
 */
void ProjectPanel::fileDiscard()
{
    if (QMessageBox::warning(this, "Firewall Builder",
      tr("This operation discards all changes that have been saved\n"
         "into the file so far, closes it and replaces it with a clean\n"
         "copy of its head revision from RCS.\n"
         "\n"
         "All changes will be lost if you do this.\n\n"),
      tr("&Discard changes"),
      tr("&Cancel"), QString::null,
      1 )==0 )
    {
/* need to close the file without asking and saving, then reopen it again */

        QString fname = rcs->getFileName();

        db()->setDirty(false);  // so it wont ask if user wants to save
        rcs->abandon();
        fileClose();

        try
        {
            RCS *rcs = new RCS(fname);
            if (rcs==NULL) return;
            rcs->co();
            load(this, rcs );
        } catch (FWException &ex)
        {
/* if there was an exception, abort operation. E.g. RCS::co may throw
 * exception */
            load(this);
            return;
        }
/***********************************************************************/
    }
}

void ProjectPanel::fileAddToRCS()
{
    if (!saveIfModified()) return;
    if (rcs && rcs->isCheckedOut()) return;

    try
    {
        if (!rcs->isInRCS() && !rcs->isRO())
        {
            rcs->add();
            rcs->co();
            QMessageBox::information(
                this,"Firewall Builder",
                tr("File %1 has been added to RCS.").arg(rcs->getFileName()),
                tr("&Continue"), QString::null,QString::null,
                0, 1 );
        }
    }
    catch (FWException &ex)
    {
        QMessageBox::critical(
            this,"Firewall Builder",
            tr("Error adding file to RCS:\n%1").arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
    }

    QString caption = rcs->getFileName().section("/",-1,-1);
    if (rcs->isInRCS()) caption = caption + ", rev " + rcs->getSelectedRev();
    if (rcs->isRO()) caption = caption + " " + tr("(read-only)");

    setWindowTitle( QString("Firewall Builder: ")+caption );

    mainW->addToRCSActionSetEn( !rcs->isInRCS() && !rcs->isRO());
    mainW->fileDiscardActionSetEn( rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
    mainW->fileCommitActionSetEn( rcs->isInRCS() && !rcs->isRO() && !rcs->isTemp());
}

//wrapers for some ObjectManipulator functions
libfwbuilder::FWObject* ProjectPanel::getOpened()
{
    return m_panel->om->getOpened();
}

libfwbuilder::FWObject* ProjectPanel::getCurrentLib()
{
    return m_panel->om->getCurrentLib();
}


void ProjectPanel::loadDataFromFw(libfwbuilder::Firewall *fw)
{
        m_panel->om->loadObjects();

        if (fw)
        {
            m_panel->om->updateObjName(fw,"", false);
            m_panel->om->editObject(fw);
        }
}

libfwbuilder::FWObject* ProjectPanel::createObject(const QString &objType,
                                          const QString &objName,
                                          libfwbuilder::FWObject *copyFrom)
{
    return m_panel->om->createObject(objType, objName, copyFrom);
}

libfwbuilder::FWObject* ProjectPanel::createObject(libfwbuilder::FWObject *parent,
                                      const QString &objType,
                                      const QString &objName,
                                      libfwbuilder::FWObject *copyFrom)
{
    return m_panel->om->createObject(parent, objType, objName, copyFrom);
}


FWObject* ProjectPanel::copyObj2Tree(const QString &objType, const QString &objName,
         FWObject *copyFrom, FWObject *parent, bool ask4Lib)
{
    return m_panel->om->copyObj2Tree(objType, objName, copyFrom, parent, ask4Lib);
}

void ProjectPanel::moveObject(libfwbuilder::FWObject *target,
                    libfwbuilder::FWObject *obj)
{
    m_panel->om->moveObject(target, obj);
}

void ProjectPanel::moveObject(const QString &targetLibName,
                    libfwbuilder::FWObject *obj)
{
    m_panel->om->moveObject(targetLibName, obj);
}

void ProjectPanel::autorename(libfwbuilder::FWObject *obj,
                    const std::string &objtype,
                    const std::string &namesuffix)
{
    m_panel->om->autorename(obj, objtype, namesuffix);
}


void ProjectPanel::updateLibColor(libfwbuilder::FWObject *lib)
{
    m_panel->om->updateLibColor(lib);
}

void ProjectPanel::updateLibName(libfwbuilder::FWObject *lib)
{
    m_panel->om->updateLibName(lib);
}

void ProjectPanel::updateObjName(libfwbuilder::FWObject *obj,
                       const QString &oldName,
                       bool  askForAutorename)
{
    m_panel->om->updateObjName(obj, oldName, askForAutorename);
}

void ProjectPanel::updateObjName(libfwbuilder::FWObject *obj,
                       const QString &oldName,
                       const QString &oldLabel,
                       bool  askForAutorename)
{
    m_panel->om->updateObjName(obj, oldName, oldLabel, askForAutorename);
}


void ProjectPanel::updateLastModifiedTimestampForOneFirewall(libfwbuilder::FWObject *o)
{
    m_panel->om->updateLastModifiedTimestampForOneFirewall(o);
}

void ProjectPanel::updateLastModifiedTimestampForAllFirewalls(libfwbuilder::FWObject *o)
{
    m_panel->om->updateLastModifiedTimestampForAllFirewalls(o);
}

void ProjectPanel::updateLastInstalledTimestamp(libfwbuilder::FWObject *o)
{
    m_panel->om->updateLastInstalledTimestamp(o);
}

void ProjectPanel::updateLastCompiledTimestamp(libfwbuilder::FWObject *o)
{
    m_panel->om->updateLastCompiledTimestamp(o);
}


libfwbuilder::FWObject* ProjectPanel::pasteTo(libfwbuilder::FWObject *target,
                                    libfwbuilder::FWObject *obj,
                                    bool openobj,
                                    bool validateOnly)
{
    return m_panel->om->pasteTo(target, obj, openobj, validateOnly);
}

void ProjectPanel::delObj(libfwbuilder::FWObject *obj,bool openobj)
{
    m_panel->om->delObj(obj, openobj);
}

ObjectTreeView* ProjectPanel::getCurrentObjectTree()
{
    return m_panel->om->getCurrentObjectTree();
}

void ProjectPanel::openObject(QTreeWidgetItem *otvi)
{
    m_panel->om->openObject(otvi);
}

void ProjectPanel::openObject(libfwbuilder::FWObject *obj)
{
    m_panel->om->openObject(obj);
}

bool ProjectPanel::editObject(libfwbuilder::FWObject *obj)
{
    return m_panel->om->editObject(obj);
}

void ProjectPanel::findAllFirewalls (std::list<libfwbuilder::Firewall *> &fws)
{
    m_panel->om->findAllFirewalls (fws);
}

libfwbuilder::FWObject* ProjectPanel::duplicateObject(libfwbuilder::FWObject *target,
                                            libfwbuilder::FWObject *obj,
                                            const QString &name,
                                            bool  askForAutorename)
{
    return m_panel->om->duplicateObject(target, obj, name, askForAutorename);
}

void ProjectPanel::showDeletedObjects(bool f)
{
    m_panel->om->showDeletedObjects(f);
}

void ProjectPanel::select()
{
    m_panel->om->select();
}

void ProjectPanel::unselect()
{
    m_panel->om->unselect();
}

void ProjectPanel::info()
{
    info(m_panel->om->getSelectedObject(), true);
}

void ProjectPanel::setManipulatorFocus()
{
    m_panel->om->setFocus();
}

void ProjectPanel::clearManipulatorFocus()
{
    m_panel->om->clearFocus();
}

void ProjectPanel::copyObj()
{
    m_panel->om->copyObj();
}

bool ProjectPanel::isManipulatorSelected()
{
    return m_panel->om->isSelected();
}

void ProjectPanel::cutObj()
{
    m_panel->om->cutObj();
}

void ProjectPanel::pasteObj()
{
    m_panel->om->pasteObj();
}


void ProjectPanel::newObject()
{
    m_panel->om->newObject();
}

void ProjectPanel::deleteObj()
{
    m_panel->om->deleteObj();
}

libfwbuilder::FWObject* ProjectPanel::getSelectedObject()
{
    return m_panel->om->getSelectedObject();
}

void ProjectPanel::reopenCurrentItemParent()
{
    m_panel->om->reopenCurrentItemParent();
}

void ProjectPanel::back()
{
    m_panel->om->back();
}

void ProjectPanel::lockObject()
{
    m_panel->om->lockObject();
}

void ProjectPanel::unlockObject()
{
    m_panel->om->unlockObject();
}



//wrapers for some Object Editor functions
bool ProjectPanel::isEditorVisible()
{
    return oe->isVisible();
}

bool ProjectPanel::isEditorModified()
{
    return oe->isModified();
}

void ProjectPanel::showEditor()
{
    oe->show();
}

void ProjectPanel::hideEditor()
{
    oe->hide();
}

void ProjectPanel::closeEditor()
{
    oe->close();
}

void ProjectPanel::openEditor(libfwbuilder::FWObject *o)
{
    oe->open(o);
}

void ProjectPanel::openOptEditor(libfwbuilder::FWObject *o, ObjectEditor::OptType t)
{
    oe->openOpt(o, t);
}

void ProjectPanel::blankEditor()
{
    oe->blank();
}


libfwbuilder::FWObject* ProjectPanel::getOpenedEditor()
{
    return oe->getOpened();
}

ObjectEditor::OptType ProjectPanel::getOpenedOptEditor()
{
    return oe->getOpenedOpt();
}

void ProjectPanel::selectObjectInEditor(libfwbuilder::FWObject *o)
{
    oe->selectObject(o);
}

void ProjectPanel::actionChangedEditor(libfwbuilder::FWObject *o)
{
    oe->actionChanged(o);
}

bool ProjectPanel::validateAndSaveEditor()
{
    return oe->validateAndSave();
}

void ProjectPanel::setFDObject(libfwbuilder::FWObject *o)
{
    fd->setObject(o);
    fd->show();
}
void ProjectPanel::resetFD()
{
    fd->reset();
}


void ProjectPanel::insertRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->insertRule();
}

void ProjectPanel::addRuleAfterCurrent()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->addRuleAfterCurrent();
}

void ProjectPanel::removeRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->removeRule();
}

void ProjectPanel::moveRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRule();
}

void ProjectPanel::moveRuleUp()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRuleUp();
}

void ProjectPanel::moveRuleDown()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRuleDown();
}

void ProjectPanel::copyRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->copyRule();
}

void ProjectPanel::cutRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->cutRule();
}

void ProjectPanel::pasteRuleAbove()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->pasteRuleAbove();
}

void ProjectPanel::pasteRuleBelow()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->pasteRuleBelow();
}

//rcs
void ProjectPanel::startupLoad()
{
    if (fwbdebug) qDebug("ProjectPanel::startupLoad: "
                         "startup: load everything.");

    int sa = st->getStartupAction();

    if (safeMode)  load(NULL);
    else
    {
        if (startupFileName.isEmpty() && sa==1) // load last edited
        {
            startupFileName = st->getLastEdited();
        }

        if ( !startupFileName.isEmpty() )
        {
            try
            {
                RCS *rcs=new RCS(startupFileName);
                rcs->co();
                load(NULL, rcs);

                loadLastOpenedLib();
            } catch (FWException &ex)
            {
                qDebug("Exception: %s",ex.toString().c_str());
                load(NULL);
            }
        } else
        {
            load(NULL); // load standard objects
            loadLastOpenedLib(startupFileName);
        }
    }

    QString id = st->getStr("UI/visibleFirewall");
    int i_id = FWObjectDatabase::getIntId(id.toLatin1().constData());
    FWObject *show_fw=NULL;
    if ( !id.isEmpty() ) show_fw = db()->getById(i_id, true);

    id = st->getStr("UI/visibleObject");
    i_id = FWObjectDatabase::getIntId(id.toLatin1().constData());
    FWObject *show_obj=NULL;
    if ( !id.isEmpty() ) show_obj = db()->getById(i_id, true);
    if ( sa==1 && !safeMode )
    {
        if (show_fw)
        {
            if (fwbdebug)
                qDebug("open firewall %s",show_fw->getName().c_str());
        }

        if (show_obj)
        {
            if (fwbdebug)
                qDebug("open object %s",show_obj->getName().c_str());
        }
    }
    mw->recreateWindowsMenu();
}


bool ProjectPanel::editingLibrary()
{
    return (rcs!=NULL &&
        ( rcs->getFileName().endsWith(".fwl")) );
}

void ProjectPanel::createRCS(const QString &filename)
{
    rcs = new RCS(filename);
    systemFile = true;
}


void ProjectPanel::load(QWidget *dialogs_parent)
{
    if (fwbdebug) qDebug("ProjectPanel::load(): start");
    QStatusBar *sb = mainW->statusBar();

    editingStandardLib = false;
    editingTemplateLib = false;

    QWidget *dlgp=NULL;
    if (dialogs_parent==NULL)
    {
        if (isVisible()) dlgp=this;
    } else
    {
        dlgp=dialogs_parent;
    }

    MessageBoxUpgradePredicate upgrade_predicate(dlgp);

    resetFD();

    if (fwbdebug) qDebug("ProjectPanel::load(): start 2");

    try
    {
// need to drop read-only flag on the database before I load new objects

        objdb = new FWObjectDatabase();
        objdb->setReadOnly( false );

        if (fwbdebug) qDebug("ProjectPanel::load(): loading objects");

        sb->showMessage( tr("Loading system objects...") );
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);

// always loading system objects
        if (fwbdebug)
            qDebug("ProjectPanel::load(): sysfname = %s", sysfname.c_str());

        objdb->load( sysfname, &upgrade_predicate, librespath);
        objdb->setFileName("");

        if (fwbdebug) qDebug("ProjectPanel::load(): create User library");

        FWObject *userLib = createNewLibrary( objdb );
        userLib->setName("User");
        userLib->setStr("color","#d2ffd0");

        // NOTE: add-on libraries are obsolete in v3.0.0
        // TODO: remove this.

        if (fwbdebug) qDebug("ProjectPanel::load(): loading libraries");

        for (list<libData>::iterator i=addOnLibs->begin(); i!=addOnLibs->end(); ++i)
        {
            string libfname = i->path.toLatin1().constData();
            if (libfname!=sysfname && i->load)
            {
                if (fwbdebug) qDebug("ProjectPanel::load(): libfname = %s",libfname.c_str());
                FWObjectDatabase *ndb = new FWObjectDatabase();
                ndb->load(libfname, &upgrade_predicate,librespath);
                FWObject *dobj =
                    ndb->findInIndex( FWObjectDatabase::DELETED_OBJECTS_ID);
                if (dobj) ndb->remove(dobj, false);

                MergeConflictRes mcr(dlgp);
                objdb->merge(ndb, &mcr);

                delete ndb;
            }
        }

        if (fwbdebug) qDebug("ProjectPanel::load(): done loading");

        objdb->setDirty(false);
        objdb->setFileName("");

        if (fwbdebug) qDebug("ProjectPanel::load(): create RCS");

        createRCS("");
/*
 * TODO: we should create new FWObjectDatabase object and assign db
 * instead of using singleton
 */
//        objdb = FWObjectDatabase::db;

        setWindowTitle( "Firewall Builder" );

        mainW->fileSaveActionSetEn( false );
        mainW->addToRCSActionSetEn( false );
        mainW->fileDiscardActionSetEn( false );
        mainW->fileCommitActionSetEn( false );

        if (fwbdebug) qDebug("ProjectPanel::load(): done");

    } catch(FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Error loading file:\n%1").arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
    }

    if (fwbdebug) qDebug("ProjectPanel::load(): load objects in ObjectManager");

    loadObjects();

    if (fwbdebug) qDebug("ProjectPanel::load(): all done");

    setupAutoSave();

}

void ProjectPanel::load(QWidget *dialogs_parent,
                        RCS *_rcs,
                        FWObjectDatabase * clone)
{
    QStatusBar *sb = mainW->statusBar();

    resetFD();

    editingStandardLib = false;
    editingTemplateLib = false;

    bool forceSave=false; // use this flag to force 'save' operation if file should be renamed

    QWidget *dlgp=NULL;
    if (dialogs_parent==NULL)
    {
        if (isVisible()) dlgp=this;
    } else
    {
        dlgp=dialogs_parent;
    }

    MessageBoxUpgradePredicate upgrade_predicate(dlgp);

    assert(_rcs!=NULL);

    rcs = _rcs;

    try
    {
        systemFile=false;
        objdb = clone;
        sb->showMessage( tr("Loading system objects...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        sb->showMessage( tr("Reading and parsing data file...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        sb->clearMessage();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

/* loadingLib is true if user wants to open a library or master library file */
        bool loadingLib         = editingLibrary();
        sb->showMessage( tr("Merging with system objects...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

        MergeConflictRes mcr(dlgp);

        sb->clearMessage();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
        //QApplication::eventLoop()->processEvents(QEventLoop::ExcludeUserInput,100);

/*
 * TODO: we should create new FWObjectDatabase object and assign db
 * instead of using singleton
 */
//        objdb = FWObjectDatabase::db;

        if (fwbdebug)
        {
            qDebug("* Merge is done");
            list<FWObject*> ll = db()->getByType(Library::TYPENAME);
            for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
            {
                qDebug("* Library %s %s in the data file",
                       FWObjectDatabase::getStringId((*i)->getId()).c_str(),
                       (*i)->getName().c_str() );
            }
        }


/* this is a hack: 'Standard' library should be read-only. I have too
 * many files I already converted to the new API/DTD and I am too lazy
 * to convert them again, so I patch it up here.
 *
 * However, if I am editing standard library, it should not be read-only.
 */
        FWObject *slib = objdb->findInIndex(FWObjectDatabase::STANDARD_LIB_ID);
        if (slib!=NULL )
        {
            if (fwbdebug)
                qDebug("standard library read-only status: %d, editingStandardLib: %d",
                       slib->isReadOnly(), editingStandardLib);

            slib->setReadOnly(! editingStandardLib);
        }

/* if the file name has an old extension .xml, change it to .fwb and
 * warn the user
 */
        QString   fn = rcs->getFileName();
        QFileInfo ofinfo(fn);

        if ( ofinfo.suffix()=="xml")
        {
            if (fwbdebug)
            {
                qDebug("Need to rename file:  %s",fn.toAscii().constData());
                qDebug("             dirPath: %s",ofinfo.dir().absolutePath().toAscii().constData());
                qDebug("            filePath: %s",ofinfo.absoluteFilePath().toAscii().constData());
            }
            QString nfn=ofinfo.dir().absolutePath() + "/" + ofinfo.completeBaseName() + ".fwb";

            bool needToRename = true;

/* need these dances with symlinks to fix bug #1008956: "Existing .fwb
 * file gets overwritten if has wrong extension"
 */
            QFileInfo nfinfo(nfn);
            if (nfinfo.exists() && ofinfo.isSymLink() && ofinfo.readLink()==nfn)
            {
// .xml file is a symlink pointing at .fwb file
// no need to rename
                needToRename = false;
            }

            if (needToRename)
            {
                if (nfinfo.exists())
                {
/* .fwb file exists but .xml is not a symlink
 * .fwb is a separate file with the same name.
 *
 * tell the user we need to rename old file but the new file exists,
 * then ask them to choose a new name. If the user chooses the same
 * name and agrees to overwrite the file, just use this name. If the
 * user hits cancel, tell them they need to choose a new name and open
 * "file save" dialog again.
 *
 * Show the first dialog only once. If user hits Cancel, they see
 * shorted version of the dialog and will be presented with "save
 * file" dialog again.
 */
                    QMessageBox::warning(
                        this,"Firewall Builder",
                        tr("Firewall Builder 2 uses file extension '.fwb' and \nneeds to rename old data file '%1' to '%2',\nbut file '%3' already exists.\nChoose a different name for the new file.")
                        .arg(fn).arg(nfn).arg(nfn),
                        tr("&Continue"), QString::null,QString::null,
                        0, 1 );

                    nfn=chooseNewFileName(fn,true,
                                          tr("Choose name and location for the new file"));
                    if (nfn.isEmpty())
                    {
                        QString oldFileName = ofinfo.absoluteFilePath() + ".bak";
                        rename(oldFileName.toLatin1().constData(), fn.toLatin1().constData());

                        QMessageBox::warning(
                            this,"Firewall Builder",
                            tr("Load operation cancelled and data file reverted to original version."),
                            tr("&Continue"), QString::null,QString::null,
                            0, 1 );

                        load(this);
                        return;
                    }
                    nfinfo.setFile(nfn);
                }

                rename(fn.toLatin1().constData(), nfn.toLatin1().constData());


                QMessageBox::warning(
                this,"Firewall Builder",
                tr("Firewall Builder 2 uses file extension '.fwb'. Your data file '%1' \nhas been renamed '%2'")
                .arg(fn).arg(nfn),
                tr("&Continue"), QString::null,QString::null,
                0, 1 );

            }

            fn = nfn;
        }

        rcs->setFileName(fn);
        db()->setFileName(fn.toLatin1().constData());

        QString caption = rcs->getFileName().section("/",-1,-1);
        if (rcs->isInRCS()) caption = caption + ", rev " + rcs->getSelectedRev();
        if (rcs->isRO()) caption = caption + " " + tr("(read-only)");

        setWindowTitle( QString("Firewall Builder: ")+caption );

        mainW->fileSaveActionSetEn( !rcs->isRO() && !rcs->isTemp());
        mainW->addToRCSActionSetEn( !rcs->isInRCS() && !rcs->isRO());
        mainW->fileDiscardActionSetEn( rcs->isInRCS() && !rcs->isRO());
        mainW->fileCommitActionSetEn( rcs->isInRCS() && !rcs->isRO());

    } catch(FWException &ex)
    {
        string trans = ex.getProperties()["failed_transformation"];
        string elem  = ex.getProperties()["failed_element"];

        if(!trans.empty() || !elem.empty())
        {
            QString msg = tr("Exception: %1").arg(ex.toString().c_str());
            if (!trans.empty())
        msg+="\n"+tr("Failed transformation : %1").arg(trans.c_str());
            if (!elem.empty())
        msg+="\n"+tr("XML element : %1").arg(elem.c_str());

             QMessageBox::warning(
                 this,"Firewall Builder",
                 tr("Error loading file:\n%1").arg(msg),
                 tr("&Continue"), QString::null,QString::null,
                 0, 1 );
        } else
             QMessageBox::warning(
                 this,"Firewall Builder",
                 tr("Error loading file:\n%1").arg(ex.toString().c_str()),
                 tr("&Continue"), QString::null,QString::null,
                 0, 1 );

        load(this);
        return;
    }

    db()->setReadOnly( rcs->isRO() || rcs->isTemp() );

// clear dirty flag for all objects, recursively
    if (!forceSave)  db()->setDirty(false);

    sb->showMessage( tr("Building object tree...") );
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    loadObjects();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    sb->showMessage( tr("Indexing...") );
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
    db()->reIndex();

    sb->clearMessage();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    setupAutoSave();
    loadLastOpenedLib();
}


void ProjectPanel::load(QWidget *dialogs_parent, RCS *_rcs)
{
    QStatusBar *sb = mainW->statusBar();

    resetFD();

    editingStandardLib = false;
    editingTemplateLib = false;

    bool forceSave=false; // use this flag to force 'save' operation if file should be renamed

    QWidget *dlgp=NULL;
    if (dialogs_parent==NULL)
    {
        if (isVisible()) dlgp=this;
    } else
    {
        dlgp=dialogs_parent;
    }

    MessageBoxUpgradePredicate upgrade_predicate(dlgp);

    assert(_rcs!=NULL);

    rcs = _rcs;
    try
    {
        /* load the data file */
        systemFile=false;

        objdb = new FWObjectDatabase();

// need to drop read-only flag on the database before I load new objects
        objdb->setReadOnly( false );

// always loading system objects
        sb->showMessage( tr("Loading system objects...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        objdb->load( sysfname, &upgrade_predicate, librespath);
        objdb->setFileName("");

// objects from a data file are in database ndb

        sb->showMessage( tr("Reading and parsing data file...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        //QApplication::eventLoop()->processEvents(QEventLoop::ExcludeUserInput,100);

        FWObjectDatabase *ndb = new FWObjectDatabase();
        ndb->load(rcs->getFileName().toLatin1().constData(), &upgrade_predicate,librespath);
        time_t   oldtimestamp = ndb->getTimeLastModified();

        sb->clearMessage();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

/* loadingLib is true if user wants to open a library or master library file */
        bool loadingLib         = editingLibrary();

        if (fwbdebug)
        {
            list<FWObject*> ll = ndb->getByType(Library::TYPENAME);
            for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
            {
                qDebug("* Found library %s %s in the data file",
                       FWObjectDatabase::getStringId((*i)->getId()).c_str(),
                       (*i)->getName().c_str() );
            }
        }

/* if user opens library file, clear read-only flag so they can edit it */
        if (loadingLib)
        {
            list<FWObject*> ll = ndb->getByType(Library::TYPENAME);
            for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
            {
                if ((*i)->getId()==FWObjectDatabase::STANDARD_LIB_ID)
                    editingStandardLib=true;
                if ((*i)->getId()==FWObjectDatabase::TEMPLATE_LIB_ID)
                    editingTemplateLib=true;
                (*i)->setReadOnly( false );
            }
        } else
        {

            // NOTE: add-on libraries are obsolete in v3.0.0
            // TODO: remove this.

/* preload libraries only if we do not edit a library file */
            for (list<libData>::iterator i=addOnLibs->begin();
                 i!=addOnLibs->end(); ++i)
            {
                string libfname = i->path.toLatin1().constData();
                if (libfname!=sysfname && i->load)
                {
                    if (fwbdebug)
                        qDebug("* Adding library %s",i->name.toLatin1().constData());

                    FWObjectDatabase *ndb1 = new FWObjectDatabase();
                    ndb1->load(libfname, &upgrade_predicate,librespath);
                    FWObject  *nlib1 = ndb1->getFirstByType(Library::TYPENAME);
                    if(nlib1==NULL)
                    {
                        qDebug("Error preloading library from file %s",
                               libfname.c_str());
                        assert(nlib1!=NULL);
                    }
                    int nlib1ID = nlib1->getId();
                    FWObject *dobj =
                        ndb1->findInIndex(FWObjectDatabase::DELETED_OBJECTS_ID);
                    if (dobj) ndb1->remove(dobj, false);

                    MergeConflictRes mcr(dlgp);
                    objdb->merge(ndb1, &mcr);

/* preloaded libraries are always read-only */
                    objdb->findInIndex(nlib1ID)->setReadOnly(true);

                    delete ndb1;
                }
            }
        }

        sb->showMessage( tr("Merging with system objects...") );
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
        //QApplication::eventLoop()->processEvents(QEventLoop::ExcludeUserInput,100);

        MergeConflictRes mcr(dlgp);
        objdb->merge(ndb, &mcr);

        delete ndb;

        objdb->setFileName(rcs->getFileName().toLatin1().constData());
        objdb->resetTimeLastModified(oldtimestamp);
        objdb->setDirty(false);

        sb->clearMessage();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
        //QApplication::eventLoop()->processEvents(QEventLoop::ExcludeUserInput,100);

/*
 * TODO: we should create new FWObjectDatabase object and assign db
 * instead of using singleton
 */
//        objdb = FWObjectDatabase::db;

        if (fwbdebug)
        {
            qDebug("* Merge is done");
            list<FWObject*> ll = db()->getByType(Library::TYPENAME);
            for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
            {
                qDebug("* Library %s %s in the data file",
                       FWObjectDatabase::getStringId((*i)->getId()).c_str(),
                       (*i)->getName().c_str() );
            }
        }


/* this is a hack: 'Standard' library should be read-only. I have too
 * many files I already converted to the new API/DTD and I am too lazy
 * to convert them again, so I patch it up here.
 *
 * However, if I am editing standard library, it should not be read-only.
 */
        FWObject *slib = objdb->findInIndex(FWObjectDatabase::STANDARD_LIB_ID);
        if (slib!=NULL )
        {
            if (fwbdebug)
                qDebug("standard library read-only status: %d, editingStandardLib: %d",
                       slib->isReadOnly(), editingStandardLib);

            slib->setReadOnly(! editingStandardLib);
        }

/* if the file name has an old extension .xml, change it to .fwb and
 * warn the user
 */
        QString   fn = rcs->getFileName();
        QFileInfo ofinfo(fn);

        if ( ofinfo.suffix()=="xml")
        {
            if (fwbdebug)
            {
                qDebug("Need to rename file:  %s",fn.toAscii().constData());
                qDebug("             dirPath: %s",ofinfo.dir().absolutePath().toAscii().constData());
                qDebug("            filePath: %s",ofinfo.absoluteFilePath().toAscii().constData());
            }
            QString nfn=ofinfo.dir().absolutePath() + "/" + ofinfo.completeBaseName() + ".fwb";

            bool needToRename = true;

/* need these dances with symlinks to fix bug #1008956: "Existing .fwb
 * file gets overwritten if has wrong extension"
 */
            QFileInfo nfinfo(nfn);
            if (nfinfo.exists() && ofinfo.isSymLink() && ofinfo.readLink()==nfn)
            {
// .xml file is a symlink pointing at .fwb file
// no need to rename
                needToRename = false;
            }

            if (needToRename)
            {
                if (nfinfo.exists())
                {
/* .fwb file exists but .xml is not a symlink
 * .fwb is a separate file with the same name.
 *
 * tell the user we need to rename old file but the new file exists,
 * then ask them to choose a new name. If the user chooses the same
 * name and agrees to overwrite the file, just use this name. If the
 * user hits cancel, tell them they need to choose a new name and open
 * "file save" dialog again.
 *
 * Show the first dialog only once. If user hits Cancel, they see
 * shorted version of the dialog and will be presented with "save
 * file" dialog again.
 */
                    QMessageBox::warning(
                        this,"Firewall Builder",
                        tr("Firewall Builder 2 uses file extension '.fwb' and \nneeds to rename old data file '%1' to '%2',\nbut file '%3' already exists.\nChoose a different name for the new file.")
                        .arg(fn).arg(nfn).arg(nfn),
                        tr("&Continue"), QString::null,QString::null,
                        0, 1 );

                    nfn=chooseNewFileName(fn,true,
                                          tr("Choose name and location for the new file"));
                    if (nfn.isEmpty())
                    {
                        QString oldFileName = ofinfo.absoluteFilePath() + ".bak";
                        rename(oldFileName.toLatin1().constData(), fn.toLatin1().constData());

                        QMessageBox::warning(
                            this,"Firewall Builder",
                            tr("Load operation cancelled and data file reverted to original version."),
                            tr("&Continue"), QString::null,QString::null,
                            0, 1 );

                        load(this);
                        return;
                    }
                    nfinfo.setFile(nfn);
                }

                rename(fn.toLatin1().constData(), nfn.toLatin1().constData());


                QMessageBox::warning(
                this,"Firewall Builder",
                tr("Firewall Builder 2 uses file extension '.fwb'. Your data file '%1' \nhas been renamed '%2'")
                .arg(fn).arg(nfn),
                tr("&Continue"), QString::null,QString::null,
                0, 1 );

            }

            fn = nfn;
        }

        rcs->setFileName(fn);
        db()->setFileName(fn.toLatin1().constData());

        QString caption = rcs->getFileName().section("/",-1,-1);
        if (rcs->isInRCS()) caption = caption + ", rev " + rcs->getSelectedRev();
        if (rcs->isRO()) caption = caption + " " + tr("(read-only)");

        setWindowTitle( QString("Firewall Builder: ")+caption );

        mainW->fileSaveActionSetEn( !rcs->isRO() && !rcs->isTemp());
        mainW->addToRCSActionSetEn( !rcs->isInRCS() && !rcs->isRO());
        mainW->fileDiscardActionSetEn( rcs->isInRCS() && !rcs->isRO());
        mainW->fileCommitActionSetEn( rcs->isInRCS() && !rcs->isRO());

    } catch(FWException &ex)
    {
        string trans = ex.getProperties()["failed_transformation"];
        string elem  = ex.getProperties()["failed_element"];

        if(!trans.empty() || !elem.empty())
        {
            QString msg = tr("Exception: %1").arg(ex.toString().c_str());
            if (!trans.empty())
        msg+="\n"+tr("Failed transformation : %1").arg(trans.c_str());
            if (!elem.empty())
        msg+="\n"+tr("XML element : %1").arg(elem.c_str());

             QMessageBox::warning(
                 this,"Firewall Builder",
                 tr("Error loading file:\n%1").arg(msg),
                 tr("&Continue"), QString::null,QString::null,
                 0, 1 );
        } else
             QMessageBox::warning(
                 this,"Firewall Builder",
                 tr("Error loading file:\n%1").arg(ex.toString().c_str()),
                 tr("&Continue"), QString::null,QString::null,
                 0, 1 );

        load(this);
        return;
    }

    db()->setReadOnly( rcs->isRO() || rcs->isTemp() );

// clear dirty flag for all objects, recursively
    if (!forceSave)  db()->setDirty(false);

    sb->showMessage( tr("Building object tree...") );
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    loadObjects();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    sb->showMessage( tr("Indexing...") );
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
    db()->reIndex();

    sb->clearMessage();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

    setupAutoSave();
    loadLastOpenedLib();
}

bool ProjectPanel::checkin(bool unlock)
{
/* doing checkin only if we did checkout so rcs!=NULL */
    QString rlog="";

    if (systemFile || rcs==NULL || !rcs->isCheckedOut() || rcs->isTemp())
        return true;

    if (rcs->isDiff())
    {
        // if the file hasn't changed, do not need to ask for the comment
        if ( ! st->getRCSLogState())
        {
            RCSFileSaveDialog_q fsd;
            QDialog *fsd_dialog = new QDialog(this);
            fsd.setupUi(fsd_dialog);
            fsd.checkinDialogTitle->setText(
                QString("<b>")+tr("Checking file %1 in RCS").arg(rcs->getFileName())+QString("</b>")
            );
            if ( fsd_dialog->exec()== QDialog::Rejected )
            {
                delete fsd_dialog;
                return false;
            }

            bool    empty_rcslog = fsd.nolog->isChecked();
            if (empty_rcslog)
            {
                rlog = "";
                st->setRCSLogState(true);
            } else
                rlog = fsd.rcslog->toPlainText();

            delete fsd_dialog;
        }
    }


/***********************************************************************/
    try
    {
        if (fwbdebug)  qDebug("about to check the file in");
        rcs->ci(rlog,unlock);
        if (fwbdebug)  qDebug("done");
    }
    catch (FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Error checking in file %1:\n%2")
            .arg(rcs->getFileName()).arg(ex.toString().c_str()),
            tr("&Continue"), QString::null, QString::null,
            0, 1 );
    }
/***********************************************************************/
    return true;
}

void ProjectPanel::save()
{
    if (fwbdebug)
        qDebug("ProjectPanel::save:  rcs=%p  rcs->isRO=%d  rcs->isTemp=%d rcs->getFileName=%s",
               rcs, rcs->isRO(), rcs->isTemp(), rcs->getFileName().toLatin1().constData());

    if (!rcs->isRO() && !rcs->isTemp())
    {
        try
        {
            if (rcs->getFileName().isEmpty())
                fileSaveAs();  // eventually calls this method again
            else
            {
/* editingLibfile is true if user edits a library or master library file */
                bool editingLibfile=editingLibrary();

                if (st->getDontSaveStdLib())  // this is now default
                {
                    list<FWObject*> userLibs;
                    list<FWObject*> ll = db()->getByType(Library::TYPENAME);
                    for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
                    {
                        if (fwbdebug) qDebug("ProjectPanel::save()  lib %s",
                                             (*i)->getName().c_str() );

                        // NOTE: add-on libraries are obsolete in v3.0.0
                        // TODO: remove this.

/* if we are not editing a library file, skip preloaded libraries */
                        if (!editingLibfile &&
                            addOnLibs->isLoaded((*i)->getName().c_str()))
                        {
                            if (fwbdebug) qDebug("                   skip");
                            continue;
                        }
/* skip standard and template libraries unless we edit them */
                        int id = (*i)->getId();
                        if (id==FWObjectDatabase::STANDARD_LIB_ID && !editingStandardLib) continue;
                        if (id==FWObjectDatabase::TEMPLATE_LIB_ID && !editingTemplateLib) continue;

                        if (fwbdebug) qDebug("                   add");
                        userLibs.push_back( *i );
                    }

                    QApplication::setOverrideCursor(QCursor( Qt::WaitCursor));

                    FWObjectDatabase *ndb = db()->exportSubtree(userLibs);

                    if (editingLibfile)
                    {
/* exported libraries are always read-only */
                        list<FWObject*> ll = ndb->getByType(Library::TYPENAME);
                        for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
                            if ((*i)->getId()!=FWObjectDatabase::STANDARD_LIB_ID &&
                                (*i)->getId()!=FWObjectDatabase::DELETED_OBJECTS_ID) (*i)->setReadOnly( true );
                    }

                    ndb->resetTimeLastModified( db()->getTimeLastModified() );
                    ndb->saveFile( rcs->getFileName().toLatin1().constData() );

                    delete ndb;

                    QApplication::restoreOverrideCursor();

                } else
                {
                    QApplication::setOverrideCursor(QCursor( Qt::WaitCursor));
                    db()->saveFile( rcs->getFileName().toLatin1().constData() );
                    QApplication::restoreOverrideCursor();
                }
            }
            db()->setDirty(false);
        }
        catch (FWException &ex)
        {
            QApplication::restoreOverrideCursor();

/* error saving the file. Since XMLTools does not return any useful
 * error message in the exception, let's check for obvious problems here
 */
            QString err;
            if (access( rcs->getFileName().toLatin1().constData(), W_OK)!=0 && errno==EACCES)
                err=tr("File is read-only");
            else
                err=ex.toString().c_str();

            QMessageBox::warning(
                this,"Firewall Builder",
                tr("Error saving file %1: %2")
                .arg(rcs->getFileName()).arg(err),
                tr("&Continue"), QString::null, QString::null,
                0, 1 );
        }
    }
}

void ProjectPanel::loadLibrary(const string &libfpath)
{
    MessageBoxUpgradePredicate upgrade_predicate;

    try
    {
        FWObjectDatabase *ndb = new FWObjectDatabase();
        ndb->load(libfpath,  &upgrade_predicate,  librespath);

        FWObject *dobj = ndb->findInIndex(FWObjectDatabase::DELETED_OBJECTS_ID);
        if (dobj) ndb->remove(dobj, false);

#if 0
        list<FWObject*> newLibs;
        newLibs= ndb->getByType(Library::TYPENAME);

        list<FWObject*> currentLibs;
        currentLibs= db()->getByType(Library::TYPENAME);

        list<FWObject*> duplicateLibs;

        for (list<FWObject*>::iterator i=newLibs.begin(); i!=newLibs.end(); i++)
        {
            string newLibID = (*i)->getId();
            if (newLibID==STANDARD_LIB)
            {
                duplicateLibs.push_back(*i);
                continue;
            }
            QString name = (*i)->getName().c_str();
            if (std::find_if(currentLibs.begin(),currentLibs.end(),
                             findFWObjectIDPredicate(newLibID))!=currentLibs.end() )
            {
                QMessageBox::warning(
                    NULL,"Firewall Builder",
                    QObject::tr("Duplicate library '%1'").arg(QString::fromUtf8(name)),
                    QObject::tr("&Continue"), QString::null,QString::null,
                    0, 1 );
                duplicateLibs.push_back(*i);
            }
        }

        if (!duplicateLibs.empty())
        {
            for (list<FWObject*>::iterator i=duplicateLibs.begin(); i!=duplicateLibs.end(); i++)
                ndb->remove(*i,false);
        }
#endif
        MergeConflictRes mcr(this);
        db()->merge(ndb, &mcr);

        delete ndb;

    } catch(FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Error loading file %1:\n%2").
                 arg(libfpath.c_str()).arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
    }
}

void ProjectPanel::fileImport()
{
    resetFD();

    QString fname = QFileDialog::getOpenFileName(
        mainW,
        tr("Choose a file to import"),
        st->getWDir(),
        "FWB library files (*.fwl);;FWB Files (*.fwb);;All Files (*)");

    if (fname.isEmpty()) return;   // Cancel  - keep working with old file

    loadLibrary( fname.toLatin1().constData() );

    loadObjects();
}


void ProjectPanel::fileCompare()
{
    resetFD();

    QMessageBox initial_question( QMessageBox::Information, "Firewall Builder",
                    tr("This operation inspects two data files (either .fwb or .fwl) and finds conflicting objects. Conflicting objects have the same internal ID but different attributes. Two data files can not be merged, or one imported into another, if they contain such objects. This operation also helps identify changes made to objects in two copies of the same data file.<br><br>This operation does not find objects present in one file but not in the other, such objects present no problem for merge or import operations.<br><br>This operation works with two external files, neither of which needs to be opened in the program. Currently opened data file is not affected by this operation and objects in the tree do not change.<br><br>Do you want to proceed ?"),
                    QMessageBox::Yes | QMessageBox::No);

    initial_question.setTextFormat( Qt::RichText );
    if (initial_question.exec() != QMessageBox::Yes) return;


    QString fname1 = QFileDialog::getOpenFileName(
        mainW,
        tr("Choose the first file"),
        st->getWDir(),
        "FWB files (*.fwb);;FWB Library Files (*.fwl);;All Files (*)");

    if (fname1.isEmpty()) return;   // Cancel

    QString fname2 = QFileDialog::getOpenFileName(
        mainW,
        tr("Choose the second file"),
        st->getWDir(),
        "FWB files (*.fwb);;FWB Library Files (*.fwl);;All Files (*)");

    if (fname2.isEmpty()) return;   // Cancel

    MessageBoxUpgradePredicate upgrade_predicate;

    FWObjectDatabase *db1;
    FWObjectDatabase *db2;
    FWObject *dobj;

    try
    {
        db1 = new FWObjectDatabase();
        db1->load(fname1.toLatin1().constData(),  &upgrade_predicate,  librespath);

        dobj = db1->findInIndex(FWObjectDatabase::DELETED_OBJECTS_ID);
        if (dobj) db1->remove(dobj, false);
    } catch(FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Error loading file %1:\n%2").
                 arg(fname1).arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
        return;
    }

    try
    {
        db2 = new FWObjectDatabase();
        db2->load(fname2.toLatin1().constData(),  &upgrade_predicate,  librespath);

        dobj = db2->findInIndex(FWObjectDatabase::DELETED_OBJECTS_ID);
        if (dobj) db2->remove(dobj, false);
    } catch(FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Error loading file %1:\n%2").
                 arg(fname2).arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
        return;
    }

    try
    {
        // CompareObjectsDialog is just like ObjConflictResolutionDialog
        // except it always returns 'accepted' and keeps record
        // of all object differences so we can print report in the end

        CompareObjectsDialog cod(this);
        db1->merge(db2, &cod);
        list<QString> report = cod.getReport();

        delete db1;
        delete db2;

        ostringstream str;
        str << cod.getNumberOfConflicts();

        QMessageBox mb( QMessageBox::Information, "Firewall Builder",
                        tr("Total number of conflicting objects: %1.\nDo you want to generate report?").arg(str.str().c_str()),
                        QMessageBox::Yes | QMessageBox::No);

        if (mb.exec() == QMessageBox::Yes)
        {
            // save report to a file

            QString destdir = getDestDir(fname1);

            QString fn = QFileDialog::getSaveFileName( this,
                           tr("Choose name and location for the report file"),
                           destdir,
                           tr( "TXT Files (*.txt);;All Files (*)" ));

            if (fwbdebug)
                qDebug( QString("Saving report to %1").arg(fn).toAscii().constData() );

            if (fn.isEmpty() ) return ;   // Cancel

            if (!fn.endsWith(".txt"))
            {
                fn+=".txt";
            }

            QFile report_file(fn);
            if (report_file.open(QIODevice::WriteOnly))
            {
                QTextStream report_stream(&report_file);
                for (list<QString>::iterator i=report.begin(); i!=report.end(); ++i)
                {
                    report_stream << *i;
                }
                report_file.close();
            } else
            {
                QMessageBox::critical(
                    this,"Firewall Builder",
                    tr("Can not open report file for writing. File '%1'").arg(fn),
                    tr("&Continue"), QString::null,QString::null,
                    0, 1 );
            }

        }

    } catch(FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Unexpected error comparing files %1 and %2:\n%3").
                 arg(fname1).arg(fname2).arg(ex.toString().c_str()),
            tr("&Continue"), QString::null,QString::null,
            0, 1 );
    }

}

void ProjectPanel::findExternalRefs(FWObject *lib,
                                       FWObject *root,
                                       list<FWReference*> &extRefs)
{
    FWReference *ref=FWReference::cast(root);
    if (ref!=NULL)
    {
        FWObject *plib = ref->getPointer()->getLibrary();
        if ( plib->getId()!=FWObjectDatabase::STANDARD_LIB_ID &&
             plib->getId()!=FWObjectDatabase::DELETED_OBJECTS_ID  &&
             plib!=lib )
            extRefs.push_back(ref);
        return;
    } else
    {
        for (FWObject::iterator i=root->begin(); i!=root->end(); i++)
            findExternalRefs(lib, *i, extRefs);

    }
}
bool ProjectPanel::exportLibraryTest(list<FWObject*> &selectedLibs)
{
/* VERY IMPORTANT: External library file must be self-contained,
 * otherwise it can not be exported.
 *
 * check if selected libraries have references to objects in other
 * libraries (not exported to the same file). Exporting such libraries
 * pulls in other ones because of these references. This is confusing
 * because it means we end up with multiple copies of such objects (in
 * exported library file and in user's data file). When user imports
 * this library and opens their file, it is impossible to say which
 * library an object belongs to.
 *
 * This is prohibited. We check if exported set of libraries has such
 * references and refuse to export it. The user is supposed to clean
 * it up by either moving objects into the library they are trying to
 * export, or by rearranging objects. The only exception for this is
 * library "Standard", which is assumed to be always present so we can
 * have references to objects in it.
 */
    QApplication::setOverrideCursor( QCursor( Qt::WaitCursor) );

    list<FWReference*> externalRefs;
    for (list<FWObject*>::iterator i=selectedLibs.begin(); i!=selectedLibs.end(); ++i)
        findExternalRefs( *i, *i, externalRefs);

    QApplication::restoreOverrideCursor();

    if (fwbdebug) qDebug("LibExportDialog::accept  externalRefs.size()=%d",
                         int(externalRefs.size()) );

/*
 * if externalRefs.size()!=0, then there were some references pointing
 * outside of the libraries we export. Some of these references may
 * point at other libraries we export, lets find these.
 */
    list<FWReference*> externalRefs2;
    for (list<FWReference*>::iterator i=externalRefs.begin(); i!=externalRefs.end(); ++i)
    {
        FWObject *tgt    = (*i)->getPointer();
        FWObject *tgtlib = tgt->getLibrary();

        if (std::find(selectedLibs.begin(),selectedLibs.end(),tgtlib)!=selectedLibs.end()) continue;
        externalRefs2.push_back(*i);
    }

    if (externalRefs2.size()!=0)
    {
        QString objlist = "";
        QString s       = "";

        for (list<FWReference*>::iterator i=externalRefs2.begin(); i!=externalRefs2.end(); ++i)
        {
            FWReference *robj   = *i;
            FWObject *selLib = robj->getLibrary();
            FWObject *pp     = robj->getParent();
            FWObject *tgt    = robj->getPointer();
            FWObject *tgtlib = tgt->getLibrary();

            if (fwbdebug)
            {
                qDebug("LibExportDialog::accept  tgt: %s pp_type: %s lib: %s",
                       tgt->getName().c_str(),
                       pp->getTypeName().c_str(),
                       tgtlib->getName().c_str());
            }

            if (std::find(selectedLibs.begin(),selectedLibs.end(),tgtlib)!=selectedLibs.end()) continue;

            if (RuleElement::cast(pp)!=NULL)
            {
                FWObject *fw       = pp;
                FWObject *rule     = pp;
                FWObject *ruleset  = pp;
                FWObject *iface    = pp;

                while (rule!=NULL && Rule::cast(rule)==NULL)
                    rule=rule->getParent();
                while (ruleset!=NULL && RuleSet::cast(ruleset)==NULL)
                    ruleset=ruleset->getParent();
                while (iface!=NULL && Interface::cast(iface)==NULL)
                    iface=iface->getParent();
                while (fw!=NULL && Firewall::cast(fw)==NULL)
                    fw=fw->getParent();

                QString rsname;
                if (Policy::cast(ruleset)!=NULL)
                {
                    s =
   QObject::tr("Library %1: Firewall '%2' (global policy rule #%3) uses object '%4' from library '%5'")
                        .arg(selLib->getName().c_str())
                        .arg(fw->getName().c_str())
                        .arg(Rule::cast(rule)->getPosition())
                        .arg(tgt->getName().c_str())
                        .arg(tgtlib->getName().c_str());
                }
                if (InterfacePolicy::cast(ruleset)!=NULL)
                {
   QObject::tr("Library %1: Firewall '%2' (interface %3 policy rule #%4) uses object '%5' from library '%6'")
                        .arg(selLib->getName().c_str())
                        .arg(fw->getName().c_str())
                        .arg(iface->getName().c_str())
                        .arg(Rule::cast(rule)->getPosition())
                        .arg(tgt->getName().c_str())
                        .arg(tgtlib->getName().c_str());
                }
                if (NAT::cast(ruleset)!=NULL)
                {
                    s =
   QObject::tr("Library %1: Firewall '%2' (NAT rule #%3) uses object '%4' from library '%5'")
                        .arg(selLib->getName().c_str())
                        .arg(fw->getName().c_str())
                        .arg(Rule::cast(rule)->getPosition())
                        .arg(tgt->getName().c_str())
                        .arg(tgtlib->getName().c_str());
                }
            } else
            {
                    s =
   QObject::tr("Library %1: Group '%2' uses object '%3' from library '%4'")
                        .arg(selLib->getName().c_str())
                        .arg(pp->getName().c_str())
                        .arg(tgt->getName().c_str())
                        .arg(tgtlib->getName().c_str());
            }
            s = s + "\n";

            if (fwbdebug) qDebug(s.toAscii().constData());

            objlist = objlist + s;
        }

        longTextDialog ltd( this,

            tr("A library that you are trying to export contains references\n"
               "to objects in the other libraries and can not be exported.\n"
               "The following objects need to be moved outside of it or\n"
               "objects that they refer to moved in it:"),
                            objlist );

        ltd.exec();
        return false;
    }
    return true;
}

void ProjectPanel::exportLibraryTo(QString fname,list<FWObject*> &selectedLibs, bool rof)
{
    QApplication::setOverrideCursor( QCursor( Qt::WaitCursor) );

    FWObjectDatabase *ndb = db()->exportSubtree( selectedLibs );

    QApplication::restoreOverrideCursor();

    if (rof)
    {
        for (list<FWObject*>::iterator i=selectedLibs.begin(); i!=selectedLibs.end(); ++i)
        {
            FWObject *nlib= ndb->findInIndex( (*i)->getId() );
            if (nlib && nlib->getId()!=FWObjectDatabase::DELETED_OBJECTS_ID)
                nlib->setReadOnly( true );
        }
    }

    try
    {
        ndb->saveFile( fname.toLatin1().constData() );
    }
    catch (FWException &ex)
    {
/* error saving the file. Since XMLTools does not return any useful
 * error message in the exception, let's check for obvious problems here
 */
        QString err;
        if (access( fname.toLatin1().constData(), W_OK)!=0 && errno==EACCES)
            err=QObject::tr("File is read-only");

        QMessageBox::warning(
            this,"Firewall Builder",
            QObject::tr("Error saving file %1: %2")
            .arg(fname).arg(err),
            "&Continue", QString::null, QString::null,
            0, 1 );
    }
}

void ProjectPanel::fileExport()
{
    LibExportDialog ed;
    list<FWObject*>  selectedLibs;
    map<int,FWObject*>::iterator i;
    QString path="";
    int lib_idx = -1;
    do
    {
        if (ed.exec()!=QDialog::Accepted) return;

        QList<QListWidgetItem*> selitems = ed.m_dialog->libs->selectedItems();

        for (i=ed.mapOfLibs.begin(); i!=ed.mapOfLibs.end(); i++)
            if (selitems.contains(ed.m_dialog->libs->item(i->first)))
                selectedLibs.push_back(i->second);

        lib_idx=ed.m_dialog->libs->currentRow ();

        if (lib_idx<0 || selectedLibs.size()==0)
        {
            QMessageBox::critical(
                this,"Firewall Builder",
                tr("Please select a library you want to export."),
                "&Continue", QString::null,QString::null,
                0, 1 );

            return;
        }
    } while (!exportLibraryTest(selectedLibs));

    FWObject *selLib = ed.mapOfLibs[ lib_idx ];
    path=st->getWDir()+QString::fromUtf8(selLib->getName().c_str())+".fwl";

    resetFD();
    QString fname;
        fname = QFileDialog::getSaveFileName(
            this,
            "Choose a filename to save under",
            path,
            "Firewall Builder 2 files (*.fwl)");

    if (fname.isEmpty()) return;
    if (QFile::exists(fname) &&
         QMessageBox::warning(
             this,"Firewall Builder",
             tr("The file %1 already exists.\nDo you want to overwrite it ?")
             .arg(fname),
             tr("&Yes"), tr("&No"), QString::null,
             0, 1 )==1 ) return;
    exportLibraryTo(fname,selectedLibs,ed.m_dialog->exportRO->isChecked());
}

void ProjectPanel::setupAutoSave()
{
    if ( st->getBool("Environment/autoSaveFile") &&
         rcs!=NULL && rcs->getFileName()!="")
    {
        int p = st->getInt("Environment/autoSaveFilePeriod");
        autosaveTimer->start( p*1000*60 );
        connect( autosaveTimer, SIGNAL(timeout()), this, SLOT(fileSave()) );
    } else
        autosaveTimer->stop();
}

QString ProjectPanel::getCurrentFileName()
{
    if (rcs!=NULL)  return rcs->getFileName();
    return "";
}

RCS * ProjectPanel::getRCS()
{
    return rcs;
}

void ProjectPanel::findObject(FWObject *o)
{
    findWhereUsedWidget->hide();
    if (fwbdebug) qDebug("ProjectPanel::findObject");
    findObjectWidget->findObject(o);
    m_panel->auxiliaryPanel->show();

}

void ProjectPanel::closeAuxiliaryPanel()
{
    m_panel->auxiliaryPanel->hide();
}

void ProjectPanel::closeEditorPanel()
{
    m_panel->objectEditorFrame->hide();
}

void ProjectPanel::openEditorPanel()
{
    m_panel->objectEditorFrame->adjustSize();
    m_panel->objectEditorFrame->show();

}

void ProjectPanel::search()
{
    findWhereUsedWidget->hide();
    m_panel->auxiliaryPanel->show();
    findObjectWidget->show();
}


void ProjectPanel::compile()
{
    if (isEditorVisible() &&
        !requestEditorOwnership(NULL,NULL,ObjectEditor::optNone,true))
        return;

    fileSave();
    mainW->compile();
}

void ProjectPanel::compile(set<Firewall*> vf)
{
    if (isEditorVisible() &&
        !requestEditorOwnership(NULL,NULL,ObjectEditor::optNone,true))
        return;

    fileSave();
    mainW->compile(vf);
}

void ProjectPanel::install(set<Firewall*> vf)
{
    mainW->install(vf);
}

void ProjectPanel::install()
{
    mainW->install();
}

void ProjectPanel::rollBackSelectionSameWidget()
{
    editorOwner->setFocus();
    emit restoreSelection_sign(true);
}

void ProjectPanel::rollBackSelectionDifferentWidget()
{
    editorOwner->setFocus();
    emit restoreSelection_sign(false);
}


QString ProjectPanel::printHeader()
{
    QString headerText = rcs->getFileName().section("/",-1,-1);
    if (rcs->isInRCS()) headerText = headerText + ", rev " + rcs->getSelectedRev();
    return headerText;
}

void ProjectPanel::releaseEditor()
{
    disconnect( SIGNAL(restoreSelection_sign(bool)) );
}

void ProjectPanel::connectEditor(QWidget *w)
{
    connect(this,
            SIGNAL(restoreSelection_sign(bool)),
            w,
            SLOT(restoreSelection(bool)));
}

bool ProjectPanel::requestEditorOwnership(QWidget *w,
                                      FWObject *obj,
                                      ObjectEditor::OptType otype,
                                      bool validate)
{
    if (!isEditorVisible()) return false;

    if(obj == getOpenedEditor() &&
       otype == getOpenedOptEditor() &&
       w == editorOwner )
    {
        releaseEditor();
        editorOwner = w;
        connectEditor(editorOwner);
        return true;
    }

    if (validate && !validateAndSaveEditor())
    {
        /*
         * roll back selection in the widget that currently
         * owns the editor. Signal restoreSelection_sign
         * is still connected to the previous owner
         */
        if (w == editorOwner )
            QTimer::singleShot( 0, this, SLOT(rollBackSelectionSameWidget()) );
        else
            QTimer::singleShot( 0, this, SLOT(rollBackSelectionDifferentWidget()) );
        return false;
    }

    if (w)
    {
        releaseEditor();
        editorOwner = w;
        connectEditor(editorOwner);
    }
    return true;
}

// NOTE: add-on libraries are obsolete in v3.0.0
// TODO: remove this.
listOfLibraries *ProjectPanel::getAddOnLibs()
{
    return addOnLibs;
}

bool ProjectPanel::isSystem(libfwbuilder::FWObject *obj)
{
    return objectTreeFormat->isSystem(obj);
}

bool ProjectPanel::isStandardId(libfwbuilder::FWObject *obj)
{
    return objectTreeFormat->isStandardId(obj);
}
    
bool ProjectPanel::validateForInsertion(libfwbuilder::FWObject *target,
                                        libfwbuilder::FWObject *obj)
{
    return objectTreeFormat->validateForInsertion(target, obj);
}

bool ProjectPanel::getCopyMenuState(const QString &objPath)
{
    return objectTreeFormat->getCopyMenuState(objPath);
}

bool ProjectPanel::getCutMenuState(const QString &objPath)
{
    return objectTreeFormat->getCutMenuState(objPath);
}

bool ProjectPanel::getPasteMenuState(const QString &objPath)
{
    return objectTreeFormat->getPasteMenuState(objPath);
}

bool ProjectPanel::getDeleteMenuState(const QString &objPath)
{
    return objectTreeFormat->getDeleteMenuState(objPath);
}

libfwbuilder::FWObject* ProjectPanel::createNewLibrary(libfwbuilder::FWObjectDatabase *db)
{
    return objectTreeFormat->createNewLibrary(db);
}



void ProjectPanel::scheduleRuleSetRedraw()
{
    if (!ruleSetRedrawPending)
    {
        ruleSetRedrawPending = true;
        redrawRuleSets();
        //QTimer::singleShot( 0, this, SLOT(redrawRuleSets()) );
    }
}

void ProjectPanel::redrawRuleSets()
{
    ruleSetRedrawPending = false;
    reopenFirewall();
}

void ProjectPanel::findWhereUsed(FWObject * obj)
{
    findObjectWidget->hide();
    m_panel->auxiliaryPanel->show();
    findWhereUsedWidget->find(obj);
}

void ProjectPanel::showEvent( QShowEvent *ev)
{ 
    if (fwbdebug)
    {
        qDebug("ProjectPanel::showEvent");
        int wf = windowFlags();
        qDebug("windowFlags=%x", wf);
    }
    QWidget::showEvent(ev);
}


// this slot is called when user hits "maximize" or "minimize" buttons
// on the title bar of the internal window. Need to restore window
// geometry and splitter position when window becomes normal (not maximized).
void ProjectPanel::stateChanged(Qt::WindowStates oldState,
                                Qt::WindowStates newState)
{
    bool is_maximized = ((newState & Qt::WindowMaximized) != 0);
    bool was_maximized = ((oldState & Qt::WindowMaximized) != 0);
    bool is_active = ((newState & Qt::WindowActive) != 0);
#if 0
    if (fwbdebug)
        qDebug("ProjectPanel::stateChanged "
               "newState=%d was_maximized=%d is_maximized=%d is_active=%d",
               int(newState), was_maximized, is_maximized, is_active);
#endif
    st->setInt("Window/maximized", is_maximized);
    if (!was_maximized && is_maximized) saveState();
    // restore size only if state of the window changes from "maximized"
    // to "normal" for the first time. After this, MDI keeps track of
    // window size automatically.
    //if (was_maximized && !is_maximized && firstTimeNormal) loadState();
    firstTimeNormal = false;
}

void ProjectPanel::hideEvent( QHideEvent *ev)
{
    if (fwbdebug) qDebug("ProjectPanel::hideEvent");
    QWidget::hideEvent(ev);
}


void ProjectPanel::closeEvent( QCloseEvent * ev)
{   
    if (fwbdebug) qDebug("ProjectPanel::closeEvent");

//    if (!closing)
    saveState();
    storeLastOpenedLib();

    if (fwbdebug)
        qDebug("ProjectPanel::closeEvent check in and delete RCS object");

    if (saveIfModified() && checkin(true))
    {
        if (rcs)
        { 
            delete rcs;
            rcs = 0;
        }
    } else
    {
        ev->ignore();
        return;
    }
    closing = true ;
    QWidget::closeEvent(ev);

    if (fwbdebug)
        qDebug("ProjectPanel::closeEvent main window houskeeping tasks");

    mw->updateWindowTitle();
    mw->recreateWindowsMenu();

    if (fwbdebug) qDebug("ProjectPanel::closeEvent all done");

//    emit closed();
}

QString ProjectPanel::getFileName()
{
    if (rcs!=NULL)
    {
        QString FileName = rcs->getFileName();
        return FileName;
    }
    else
        return "";
}


void ProjectPanel::saveState()
{
    QString FileName ;
    if (rcs!=NULL)
    {
        FileName = rcs->getFileName();
    }

    if (fwbdebug)
        qDebug("ProjectPanel::saveState FileName=%s ready=%d",
               FileName.toAscii().data(), ready);

    if (!ready) return;

    if (mdiWindow->isMaximized())
    {
        if (fwbdebug) qDebug ("Window/maximized true");
        st->setInt("Window/maximized", 1);
    }
    else
    {
        if (fwbdebug) qDebug ("Window/maximized false");
        st->setInt("Window/maximized", 0);
    }

    if (!mdiWindow->isMaximized())
    {
        st->setInt("Window/" + FileName + "/x", mdiWindow->x());
        st->setInt("Window/" + FileName + "/y", mdiWindow->y());
        st->setInt("Window/" + FileName + "/width", mdiWindow->width ());
        st->setInt("Window/" + FileName + "/height", mdiWindow->height ());
    }

    // Save position of splitters regardless of the window state
    QList<int> sl = m_panel->mainSplitter->sizes();
    QString arg = QString("%1,%2").arg(sl[0]).arg(sl[1]);
    if (sl[0] || sl[1])
        st->setStr("Window/" + FileName + "/MainWindowSplitter", arg );

    if (fwbdebug)
    {
        QString out1 = " save Window/" + FileName + "/MainWindowSplitter";
        out1+= " " + arg;
        qDebug(out1.toAscii().data());
    }

    sl = m_panel->objInfoSplitter->sizes();
    arg = QString("%1,%2").arg(sl[0]).arg(sl[1]);
    if (sl[0] || sl[1])
        st->setStr("Window/" + FileName + "/ObjInfoSplitter", arg );
}

void ProjectPanel::loadState()
{
    int w1 = 0;
    int w2 = 0;
    QString w1s, w2s;
    bool ok = false;

    if (rcs==NULL) return;
    QString filename = rcs->getFileName();

    if (fwbdebug)
    {
        qDebug("ProjectPanel::loadState filename=%s isMaximized=%d",
               filename.toAscii().data(), isMaximized());
        qDebug("mdiWindow=%p", mdiWindow);
        qDebug("ready=%d", ready);
    }

    if (!ready) return;

    int maximized_status = st->getInt("Window/maximized");
    if (!maximized_status && mdiWindow)
    {
        if (fwbdebug) qDebug("ProjectPanel::loadState  show normal");
        setWindowState(0);
        int x = st->getInt("Window/"+filename+"/x");
        int y = st->getInt("Window/"+filename+"/y");
        int width = st->getInt("Window/"+filename+"/width");
        int height = st->getInt("Window/"+filename+"/height");
        if (width==0 || height==0)
        {
            x = 10;
            y = 10;
            width = 600;
            height= 600;
        }
        if (fwbdebug)
            qDebug("ProjectPanel::loadState  set geometry: %d %d %d %d",
                   x,y,width,height);
        
        mdiWindow->setGeometry(x,y,width,height);
    }

    QString h_splitter_setting = "Window/" + filename + "/MainWindowSplitter";
    QString val = st->getStr(h_splitter_setting);
    
    w1s = val.split(',')[0];
    ok = false;
    w1 = w1s.toInt(&ok, 10);
    if (!ok || w1 == 0) w1 = DEFAULT_H_SPLITTER_POSITION;

    w2 = mdiWindow->width() - w1;

    if (fwbdebug)
        qDebug(QString("%1: %2x%3").arg(h_splitter_setting).
               arg(w1).arg(w2).toAscii().data());

    QList<int> sl;
    sl.push_back(w1);
    sl.push_back(w2);
    if (w1 && w2)
        m_panel->mainSplitter->setSizes( sl );

    if (fwbdebug) qDebug("Restore info window splitter position");

    QString v_splitter_setting = "Window/" + filename + "/ObjInfoSplitter";
    val = st->getStr(v_splitter_setting);

    w1s = val.split(',')[0];
    ok = false;
    w1 = w1s.toInt(&ok, 10);
    if (!ok || w1 == 0) w1 = DEFAULT_V_SPLITTER_POSITION;
    w2 = mdiWindow->height() - w1;

    if (fwbdebug)
        qDebug(QString("%1: %2x%3").arg(v_splitter_setting).
               arg(w1).arg(w2).toAscii().data());

    sl.clear();
    sl.push_back(w1);
    sl.push_back(w2);
    if (w1 && w2)
        m_panel->objInfoSplitter->setSizes( sl );


    if (fwbdebug) qDebug("ProjectPanel::loadState done");
}


void ProjectPanel::splitterMoved(int , int)
{
}

void ProjectPanel::resizeEvent(QResizeEvent*)
{
}

ProjectPanel * ProjectPanel::clone(ProjectPanel * cln)
{
    cln->mainW = mainW;
    cln->rcs = rcs;
    cln->addOnLibs = addOnLibs;
    //cln->objectTreeFormat = objectTreeFormat;
    cln->systemFile = systemFile;
    cln->safeMode = safeMode;
    cln->editingStandardLib = editingStandardLib;
    cln->editingTemplateLib = editingTemplateLib;
    cln->ruleSetRedrawPending = ruleSetRedrawPending;
    cln->startupFileName = startupFileName;
    //cln->objdb = objdb;
    cln->editorOwner = editorOwner;
    cln->oe = oe;
    cln->fd = fd;
    cln->shownInInfo = shownInInfo;
    cln->autosaveTimer = autosaveTimer;
    //cln->ruleSetViews = ruleSetViews;
    cln->ruleSetTabIndex = ruleSetTabIndex;
    cln->visibleFirewall = visibleFirewall;
    cln->firewalls = firewalls;
    cln->lastFirewallIdx = lastFirewallIdx;
    cln->changingTabs = changingTabs;
    cln->noFirewalls = noFirewalls;
    cln->mdiWindow = mdiWindow ;
    cln->m_panel = m_panel;
    cln->findObjectWidget = findObjectWidget;
    cln->findWhereUsedWidget = findWhereUsedWidget;
    cln->copySet = copySet;
    return cln;
}
