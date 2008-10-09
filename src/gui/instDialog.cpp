/*

                          Firewall Builder

                 Copyright (C) 2004 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

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

#include "instDialog.h"
#include "FirewallInstaller.h"
#include "FWBSettings.h"
#include "SSHUnx.h"
#include "SSHPIX.h"
#include "SSHIOS.h"
#include "FWWindow.h"
#include "InstallFirewallViewItem.h"
#include "instOptionsDialog.h"
#include "instBatchOptionsDialog.h"

#include <qcheckbox.h>
#include <qlineedit.h>
#include <qtextedit.h>
#include <qtimer.h>
#include <qfiledialog.h>
#include <qpushbutton.h>
#include <qlabel.h>
#include <qprogressbar.h>
#include <qprocess.h>
#include <qapplication.h>
#include <qeventloop.h>
#include <qfile.h>
#include <qdir.h>
#include <qmessagebox.h>
#include <qspinbox.h>
#include <qgroupbox.h>
#include <qcolor.h>
#include <qtablewidget.h>
#include <qtextcodec.h>
#include <qfileinfo.h>
#include <qtextstream.h>
#include <QDateTime>

#include "fwbuilder/Resources.h"
#include "fwbuilder/FWObjectDatabase.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/XMLTools.h"
#include "fwbuilder/Interface.h"
#include "fwbuilder/Management.h"

#ifndef _WIN32
#  include <unistd.h>     // for access(2) and getdomainname
#endif

#include <errno.h>
#include <iostream>

using namespace std;
using namespace libfwbuilder;


instDialog::instDialog(QWidget* p,
                       BatchOperation op,
                       t_fwSet reqFirewalls_) : QDialog(p)
{
    m_dialog = new Ui::instDialog_q;
    m_dialog->setupUi(this);

    setControlWidgets(this,
                      m_dialog->stackedWidget,
                      m_dialog->nextButton,
                      m_dialog->finishButton,
                      m_dialog->backButton,
                      m_dialog->cancelButton,
                      m_dialog->titleLabel);

    setWindowFlags(Qt::Dialog | Qt::WindowSystemMenuHint);

    installer = NULL;

    page_1_op = COMPILE;

    showSelectedFlag = false;
    rejectDialogFlag = false;

    currentLog = m_dialog->procLogDisplay;
    currentSaveButton = m_dialog->saveMCLogButton;
    currentSaveButton->setEnabled(true);
    currentStopButton = m_dialog->controlMCButton;
    currentProgressBar = m_dialog->compProgress;
    currentFirewallsBar = m_dialog->compFirewallProgress;
    currentLabel = m_dialog->infoMCLabel;
    currentFWLabel = m_dialog->fwMCLabel;
    currentLabel->setText("");

    connect(&proc, SIGNAL(readyReadStandardOutput()),
            this, SLOT(readFromStdout()) );

    // even though we set channel mode to "merged", QProcess
    // seems to not merge them on windows.
    proc.setProcessChannelMode(QProcess::MergedChannels);

    m_dialog->fwWorkList->setSortingEnabled(true);

    for (int page=0; page < pageCount(); page++)
        setFinishEnabled(page, false);

    lastPage=-1;
    reqFirewalls = reqFirewalls_;

    findFirewalls();
    if (firewalls.size()==0)
    {
        setTitle( pageCount()-1, tr("There are no firewalls to process.") );
        for (int i=0;i<pageCount()-1;i++)
        {
            setAppropriate(i,false);
        }
        showPage(pageCount()-1);
        return;
    }
    if (firewalls.size()==1) m_dialog->batchInstall->setEnabled(false);
    // setup wizard appropriate pages
    operation=op;

    creatingTable = false;

    showPage(0);

    switch(op)
    {
    case BATCH_COMPILE:
    { // only compilation's requested
        m_dialog->selectInfoLabel->setText(
            tr("<p align=\"center\"><b><font size=\"+2\">Select firewalls for compilation.</font></b></p>"));
        m_dialog->batchInstFlagFrame->hide();
        setAppropriate(2,false);
        m_dialog->selectTable->hideColumn(1);
        break;
    }

    case BATCH_INSTALL:
    { // full cycle's requested
        break;
    }

    default :
    {
        setTitle( pageCount()-1, tr("Unknown operation.") );
        for (int i=0;i<pageCount()-1;i++)
        {
            setAppropriate(i,false);
        }
        showPage(pageCount()-1);
    }
    }

    //hide all details
    bool fs = st->value(
        SETTINGS_PATH_PREFIX"/Installer/ShowDetails").toBool();
    if (fs)
        m_dialog->detailMCframe->show();
    else
        m_dialog->detailMCframe->hide();

    togleDetailMC();
}

instDialog::~instDialog()
{
    delete m_dialog;
}

// ========================================================================

/* 
 * main loop: use lists compile_fw_list and install_fw_list to iterate
 * all firewalls and do everything.
 */
void instDialog::mainLoopCompile()
{
    // first compile all
    if (compile_fw_list.size())
    {
        Firewall *fw = compile_fw_list.front();
        compile_fw_list.pop_front();
        cnf.clear();
        runCompiler(fw);
        return;
    } else
    {
        // Compile is done or there was no firewalls to compile to
        // begin with Check if we have any firewalls to install. Note
        // that we "uncheck" "install" checkboxes in the first page of
        // the wizard on compile failure, so we need to rebuild install_fw_list
        // here.
        fillInstallOpList();
        if (install_fw_list.size() > 0)
        {
            page_1_op = INSTALL;
            setNextEnabled(1, true);
        }
    }
    setFinishEnabled(currentPage(), true);
}

void instDialog::mainLoopInstall()
{
    if (fwbdebug)
        qDebug("instDialog::mainLoopInstall:  %d firewalls to install",
               int(install_fw_list.size()));

    if (install_fw_list.size())
    {
        if (install_fw_list.size() == install_list_initial_size)
        {
            // this is the first firewall. If batch mode was requested, show
            // installer options dialog (this will be the only time it is 
            // shown)
            if (m_dialog->batchInstall->isChecked() && !getBatchInstOptions())
                return;
        }

        Firewall *fw = install_fw_list.front();
        install_fw_list.pop_front();
        runInstaller(fw);
        return;
    }
    setFinishEnabled(currentPage(), true);
}

// ========================================================================


void instDialog::showPage(const int page)
{
    FakeWizard::showPage(page);

    if (fwbdebug && reqFirewalls.empty())
        qDebug("instDialog::showPage reqFirewalls is empty");

    if (fwbdebug) qDebug("instDialog::showPage");
    int p = page;

    if (fwbdebug)
        qDebug(QString("to page: %1  from page: %2").
               arg(p).arg(lastPage).toAscii().constData());

    if (p==2)
    {
        showPage(1);
        return;
    }

    switch (p)
    {
    case 0: // select firewalls for compiling and installing
    {
        if (lastPage<0) fillCompileSelectList();
        setNextEnabled(0, tableHasChecked());
        break;
    }

    case 1: // compile and install firewalls
    {
        setBackEnabled(1, false);
        setNextEnabled(1, false);

        fillCompileOpList();
        fillInstallOpList();

        // Page 1 of the wizard does both compile and install
        // controlled by flag page_1_op
        switch (page_1_op)
        {
        case COMPILE:
        {
            if (fwbdebug) qDebug("Page 1 compile");
            if (compile_fw_list.size()==0)
            {
                if (install_fw_list.size()==0)
                {
                    showPage(0);
                    return;
                }
                page_1_op = INSTALL;
                showPage(1);
                return;
            }

            mw->fileSave();

            currentFirewallsBar->reset();
            currentFirewallsBar->setMaximum(compile_list_initial_size);
            currentLog->clear();
            fillCompileUIList();
            qApp->processEvents();
            mainLoopCompile();
            break;
        }

        case INSTALL:
        {
            if (fwbdebug) qDebug("Page 1 install");
            if (install_fw_list.size() > 0)
            {
                currentFirewallsBar->reset();
                currentFirewallsBar->setMaximum(install_list_initial_size);
                currentLog->clear();
                fillInstallUIList();
                qApp->processEvents();
                mainLoopInstall();
                break;
            }
        }
        }
        break;
    }

    default: { }
    }

    lastPage = currentPage();
    setCurrentPage(page);
}

QString instDialog::replaceMacrosInCommand(const QString &ocmd)
{

/* replace macros in activation command:
 *
 * %FWSCRIPT%, %FWDIR%, %FWBPROMPT%, %RBTIMEOUT%
 *
 * check if cnf.conffile is a full path. If it is, strip the path part
 * and use only the file name for %FWSCRIPT%
 */
    QString cmd = ocmd;
    QString fwbscript = QFileInfo(cnf.conffile).fileName();
    if (fwbdebug)
    {
        qDebug("Macro substitutions:");
        qDebug("  cnf.conffile=%s", cnf.conffile.toAscii().constData());
        qDebug("  %%FWSCRIPT%%=%s", fwbscript.toAscii().constData());
        qDebug("  %%FWDIR%%=%s", cnf.fwdir.toAscii().constData());
    }

    cmd.replace("%FWSCRIPT%", fwbscript);
    cmd.replace("%FWDIR%", cnf.fwdir);
    cmd.replace("%FWBPROMPT%", fwb_prompt);

    int r = cnf.rollbackTime;
    if (cnf.rollbackTimeUnit=="sec")  r = r*60;

    QString rbt;
    rbt.setNum(r);
    cmd.replace("%RBTIMEOUT%",rbt);
    return cmd;
}

void instDialog::testRunRequested()
{
#if 0
#endif
}

void instDialog::findFirewalls()
{
    firewalls.clear();
    mw->findAllFirewalls(firewalls);
    firewalls.sort(FWObjectNameCmpPredicate());
    m_dialog->saveMCLogButton->setEnabled(true);
}


bool instDialog::testFirewall(Firewall *fw)
{
    if (fwbdebug) qDebug("instDialog::testFirewall");
    FWOptions  *fwopt = fw->getOptionsObject();
    customScriptFlag = false;

    Management *mgmt = fw->getManagementObject();
    assert(mgmt!=NULL);
    PolicyInstallScript *pis   = mgmt->getPolicyInstallScript();

/* we don't care about ssh settings if external installer is to be used */

    if ( pis->getCommand()=="" && st->getSSHPath().isEmpty())
    {
        QMessageBox::critical(this, "Firewall Builder",
   tr("Policy installer uses Secure Shell to communicate with the firewall.\n"
      "Please configure directory path to the secure shell utility \n"
      "installed on your machine using Preferences dialog"),
                              tr("&Continue") );

        addToLog("Please configure directory path to the secure \n "
                 "shell utility installed on your machine using \n"
                 "Preferences dialog\n");
        return false;
    }

    return true;
}

bool instDialog::isCiscoFamily()
{
    string platform = cnf.fwobj->getStr("platform");
    return (platform=="pix" || platform=="fwsm" || platform=="iosacl");
}

/*
 * "uncheck" checkbox in the "install" column to make sure we do not
 * try to install this firewall. Used in instDialog_compile on failure.
 */
void instDialog::blockInstallForFirewall(Firewall *fw)
{
    installMapping[fw]->setCheckState(Qt::Unchecked);
}

#if 0
void instDialog::analyseInstallQueue(bool &fPix, bool &fCustInst)
{
    if (fwbdebug) qDebug("instDialog::analyseInstallQueue");
    Firewall *f;
    //FWOptions *fwopt;
    Management *mgmt;
    PolicyInstallScript *pis;

    fPix=false;
    fCustInst=true;

    t_fwList::iterator i;
    for(i=opList.begin(); i!=opList.end(); ++i)
    {
        f=(*i);
        //fwopt=f->getOptionsObject();
        mgmt=f->getManagementObject();
        pis   = mgmt->getPolicyInstallScript();

        fPix = fPix || f->getStr("platform")=="pix" || f->getStr("platform")=="fwsm" || f->getStr("platform")=="iosacl";
        fCustInst =  fCustInst && !( pis->getCommand()=="" );

        if (fwbdebug)
        {
            qDebug(("f:"+f->getName()).c_str());
            qDebug(("p:"+f->getStr("platform")).c_str());
            qDebug((QString("fPix:")+(fPix?"true":"false")).toAscii().constData());
        }

        if (fPix && !fCustInst) return;// nothing can change if we continue loop
    }
}
#endif
