// ScreenWorkspaces.cc for Fluxbox Window Manager
// Copyright (c) 2001 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//
// Screen.cc for Blackbox - an X11 Window manager
// Copyright (c) 1997 - 2000 Brad Hughes (bhughes at tcac.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Screen.hh"

#include "fluxbox.hh"
#include "Keys.hh"
#include "Window.hh"
#include "Workspace.hh"

#include "FbMenu.hh"
#include "WinClient.hh"

#include "FbTk/App.hh"
#include "FbTk/FbString.hh"

#include <string>

using std::string;

unsigned int BScreen::currentWorkspaceID() const {
    return m_current_workspace->workspaceID();
}

void BScreen::updateWorkspaceName(unsigned int w) {
    Workspace *space = getWorkspace(w);
    if (space) {
        m_workspace_names[w] = space->name();
        m_workspacenames_sig.emit(*this);
        Fluxbox::instance()->save_rc();
    }
}

void BScreen::removeWorkspaceNames() {
    m_workspace_names.clear();
}

int BScreen::addWorkspace() {

    bool save_name = getNameOfWorkspace(m_workspaces_list.size()) == "";
    std::string name = getNameOfWorkspace(m_workspaces_list.size());
    Workspace *ws = new Workspace(*this, name, m_workspaces_list.size());
    m_workspaces_list.push_back(ws);

    if (save_name) {
        addWorkspaceName(ws->name().c_str());
        m_workspacenames_sig.emit(*this);
    }

    saveWorkspaces(m_workspaces_list.size());
    workspaceCountSig().emit( *this );

    return m_workspaces_list.size();

}

/// removes last workspace
/// @return number of desktops left
int BScreen::removeLastWorkspace() {
    if (m_workspaces_list.size() <= 1)
        return 0;
    Workspace *wkspc = m_workspaces_list.back();

    if (m_current_workspace->workspaceID() == wkspc->workspaceID())
        changeWorkspaceID(m_current_workspace->workspaceID() - 1);
    if (m_former_workspace && m_former_workspace->workspaceID() == wkspc->workspaceID())
        m_former_workspace = 0;

    wkspc->removeAll(wkspc->workspaceID()-1);

    Icons::iterator it = iconList().begin();
    const Icons::iterator it_end = iconList().end();
    for (; it != it_end; ++it) {
        if ((*it)->workspaceNumber() == wkspc->workspaceID())
            (*it)->setWorkspace(wkspc->workspaceID()-1);
    }
    m_clientlist_sig.emit(*this);

    //remove last workspace
    m_workspaces_list.pop_back();

    saveWorkspaces(m_workspaces_list.size());
    workspaceCountSig().emit( *this );
    // must be deleted after we send notify!!
    // so we dont get bad pointers somewhere
    // while processing the notify signal
    delete wkspc;

    return m_workspaces_list.size();
}


void BScreen::changeWorkspaceID(unsigned int id, bool revert) {

    if (! m_current_workspace || id >= m_workspaces_list.size() ||
        id == m_current_workspace->workspaceID())
        return;

    m_former_workspace = m_current_workspace;

    /* Ignore all EnterNotify events until the pointer actually moves */
    this->focusControl().ignoreAtPointer();

    FbTk::App::instance()->sync(false);
    Fluxbox::instance()->grab();

    FluxboxWindow *focused = FocusControl::focusedFbWindow();

    if (focused && focused->isMoving() && doOpaqueMove())
        // don't reassociate if not opaque moving
        reassociateWindow(focused, id, true);

    // set new workspace
    Workspace *old = currentWorkspace();
    m_current_workspace = getWorkspace(id);

    // we show new workspace first in order to appear faster
    currentWorkspace()->showAll();

    // reassociate all windows that are stuck to the new workspace
    Workspace::Windows wins = old->windowList();
    Workspace::Windows::iterator it = wins.begin();
    for (; it != wins.end(); ++it) {
        if ((*it)->isStuck()) {
            reassociateWindow(*it, id, true);
        }
    }

    // change workspace ID of stuck iconified windows, too
    Icons::iterator icon_it = iconList().begin();
    for (; icon_it != iconList().end(); ++icon_it) {
        if ((*icon_it)->isStuck())
            (*icon_it)->setWorkspace(id);
    }

    if (focused && focused->isMoving() && doOpaqueMove())
        focused->focus();
    else if (revert)
        FocusControl::revertFocus(*this);

    old->hideAll(false);

    Fluxbox::instance()->ungrab();
    FbTk::App::instance()->sync(false);

    m_currentworkspace_sig.emit(*this);

    // do this after atom handlers, so scripts can access new workspace number
    Fluxbox::instance()->keys()->doAction(FocusIn, 0, 0, Keys::ON_DESKTOP);
}


void BScreen::sendToWorkspace(unsigned int id, FluxboxWindow *win, bool changeWS) {
    if (! m_current_workspace || id >= m_workspaces_list.size())
        return;

    if (!win)
        win = FocusControl::focusedFbWindow();

    if (!win || &win->screen() != this || win->isStuck())
        return;

    FbTk::App::instance()->sync(false);

    windowMenu().hide();
    reassociateWindow(win, id, true);

    // change workspace ?
    if (changeWS)
        changeWorkspaceID(id, false);

    // if the window is on current workspace, show it; else hide it.
    if (id == currentWorkspace()->workspaceID() && !win->isIconic())
        win->show();
    else {
        win->hide(true);
        FocusControl::revertFocus(*this);
    }

    // send all the transients too
    FluxboxWindow::ClientList::iterator client_it = win->clientList().begin();
    FluxboxWindow::ClientList::iterator client_it_end = win->clientList().end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient::TransientList::const_iterator it = (*client_it)->transientList().begin();
        WinClient::TransientList::const_iterator it_end = (*client_it)->transientList().end();
        for (; it != it_end; ++it) {
            if ((*it)->fbwindow())
                sendToWorkspace(id, (*it)->fbwindow(), false);
        }
    }

}

void BScreen::addWorkspaceName(const char *name) {
    m_workspace_names.push_back(FbTk::FbStringUtil::LocaleStrToFb(name));
    Workspace *wkspc = getWorkspace(m_workspace_names.size()-1);
    if (wkspc)
        wkspc->setName(m_workspace_names.back());
}


string BScreen::getNameOfWorkspace(unsigned int workspace) const {
    if (workspace < m_workspace_names.size())
        return m_workspace_names[workspace];
    else
        return "";
}

void BScreen::reassociateWindow(FluxboxWindow *w, unsigned int wkspc_id,
                                bool ignore_sticky) {
    if (w == 0)
        return;

    if (wkspc_id >= numberOfWorkspaces())
        wkspc_id = currentWorkspace()->workspaceID();

    if (!w->isIconic() && w->workspaceNumber() == wkspc_id)
        return;


    if (w->isIconic()) {
        removeIcon(w);
        getWorkspace(wkspc_id)->addWindow(*w);
    } else if (ignore_sticky || ! w->isStuck()) {
        // fresh windows have workspaceNumber == -1, which leads to
        // an invalid workspace (unsigned int)
        Workspace* ws = getWorkspace(w->workspaceNumber());
        if (ws)
            ws->removeWindow(w, true);
        getWorkspace(wkspc_id)->addWindow(*w);
    }
}

/**
 Goes to the workspace "right" of the current
*/
void BScreen::nextWorkspace(int delta) {
    focusControl().stopCyclingFocus();
    if (delta)
        changeWorkspaceID( (currentWorkspaceID() + delta) % numberOfWorkspaces());
    else if (m_former_workspace)
        changeWorkspaceID(m_former_workspace->workspaceID());
}

/**
 Goes to the workspace "left" of the current
*/
void BScreen::prevWorkspace(int delta) {
    focusControl().stopCyclingFocus();
    if (delta)
        changeWorkspaceID( (static_cast<signed>(numberOfWorkspaces()) + currentWorkspaceID() - (delta % numberOfWorkspaces())) % numberOfWorkspaces());
    else if (m_former_workspace)
        changeWorkspaceID(m_former_workspace->workspaceID());
}

/**
 Goes to the workspace "right" of the current
*/
void BScreen::rightWorkspace(int delta) {
    focusControl().stopCyclingFocus();
    if (currentWorkspaceID()+delta < numberOfWorkspaces())
        changeWorkspaceID(currentWorkspaceID()+delta);
}

/**
 Goes to the workspace "left" of the current
*/
void BScreen::leftWorkspace(int delta) {
    focusControl().stopCyclingFocus();
    if (currentWorkspaceID() >= static_cast<unsigned int>(delta))
        changeWorkspaceID(currentWorkspaceID()-delta);
}

