// WindowMoveResize.cc for Fluxbox Window Manager
// Copyright (c) 2001 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//
// WindowMoveResize.cc for Blackbox - an X11 Window manager
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

#include "Window.hh"
#include "Window_internal.hh"

#include "CurrentWindowCmd.hh"
#include "WinClient.hh"
#include "fluxbox.hh"
#include "Keys.hh"
#include "Screen.hh"
#include "FbWinFrameTheme.hh"
#include "FbAtoms.hh"
#include "RootTheme.hh"
#include "Workspace.hh"
#include "FbWinFrame.hh"
#include "WinButton.hh"
#include "WinButtonTheme.hh"
#include "WindowCmd.hh"
#ifdef REMEMBER
#include "Remember.hh"
#endif
#include "MenuCreator.hh"
#include "FocusControl.hh"
#include "IconButton.hh"
#include "ScreenPlacement.hh"
#include "RectangleUtil.hh"
#include "Debug.hh"

#include "FbTk/StringUtil.hh"
#include "FbTk/Compose.hh"
#include "FbTk/CommandParser.hh"
#include "FbTk/EventManager.hh"
#include "FbTk/KeyUtil.hh"
#include "FbTk/SimpleCommand.hh"
#include "FbTk/Select2nd.hh"
#include "FbTk/MemFun.hh"

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif // SHAPE

#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <cstring>
#include <cstdio>
#include <iostream>
#include <cassert>
#include <functional>
#include <algorithm>

using std::endl;
using std::string;
using std::vector;
using std::mem_fn;
using std::equal_to;
using std::max;
using std::swap;
using std::dec;
using std::hex;

using namespace std::placeholders;
using namespace FbTk;


namespace {

int s_original_workspace = 0;

void callForAllTransient(FluxboxWindow& win, void (*callFunc)(FluxboxWindow&)) {
    WinClient::TransientList::const_iterator it = win.winClient().transientList().begin();
    WinClient::TransientList::const_iterator it_end = win.winClient().transientList().end();
    for (; it != it_end; ++it) {
        if ((*it)->fbwindow() && !(*it)->fbwindow()->isIconic())
            // TODO: should we also check if it is the active client?
            callFunc(*(*it)->fbwindow());
    }
}

/// raise window and do the same for each transient of the current window
void raiseFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock)
        return;

    if (win.isIconic())
        return;

    win.oplock = true;

    // we need to lock actual restacking so that raising above active transient
    // won't do anything nasty
    if (!win.winClient().transientList().empty())
        win.screen().layerManager().lock();

    win.layerItem().raise();

    callForAllTransient(win, raiseFluxboxWindow);

    win.oplock = false;

    if (!win.winClient().transientList().empty())
        win.screen().layerManager().unlock();

}

/// lower window and do the same for each transient it holds
void lowerFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock)
        return;

    if (win.isIconic())
        return;

    win.oplock = true;

    // we need to lock actual restacking so that raising above active transient
    // won't do anything nasty
    if (!win.winClient().transientList().empty())
        win.screen().layerManager().lock();

    // lower the windows from the top down, so they don't change stacking order
    const WinClient::TransientList& transients = win.winClient().transientList();
    WinClient::TransientList::const_reverse_iterator it =     transients.rbegin();
    WinClient::TransientList::const_reverse_iterator it_end = transients.rend();
    for (; it != it_end; ++it) {
        if ((*it)->fbwindow() && !(*it)->fbwindow()->isIconic())
            // TODO: should we also check if it is the active client?
            lowerFluxboxWindow(*(*it)->fbwindow());
    }

    win.layerItem().lower();

    win.oplock = false;
    if (!win.winClient().transientList().empty())
        win.screen().layerManager().unlock();

}

/// raise window and do the same for each transient it holds
void tempRaiseFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock) return;
    win.oplock = true;

    if (!win.isIconic()) {
        win.layerItem().tempRaise();
    }

    callForAllTransient(win, tempRaiseFluxboxWindow);

    win.oplock = false;
}

// Helper class for getResizeDirection below
// Tests whether a point is on an edge or the corner.
struct TestCornerHelper {
    int corner_size_px, corner_size_pc;
    bool operator()(int xy, int wh)
    {
        /* The % checking must be right: 0% must fail, 100% must succeed. */
        return xy < corner_size_px  ||  100 * xy < corner_size_pc * wh;
    }
};

} // namespace

void FluxboxWindow::move(int x, int y) {
    moveResize(x, y, frame().width(), frame().height());
}

void FluxboxWindow::resize(unsigned int width, unsigned int height) {
    // don't set window as placed, since we're only resizing
    bool placed = m_placed;
    moveResize(frame().x(), frame().y(), width, height);
    m_placed = placed;
}

// send_event is just an override
void FluxboxWindow::moveResize(int new_x, int new_y,
                               unsigned int new_width, unsigned int new_height,
                               bool send_event) {

    m_placed = true;
    send_event = send_event || frame().x() != new_x || frame().y() != new_y;

    if ((new_width != frame().width() || new_height != frame().height()) &&
        isResizable() && !isShaded()) {

        if ((((signed) frame().width()) + new_x) < 0)
            new_x = 0;
        if ((((signed) frame().height()) + new_y) < 0)
            new_y = 0;

        frame().moveResize(new_x, new_y, new_width, new_height);
        setFocusFlag(m_focused);

        send_event = true;
    } else if (send_event)
        frame().move(new_x, new_y);

    if (send_event && ! moving) {
        sendConfigureNotify();
    }


    if (!moving) {
        m_last_resize_x = new_x;
        m_last_resize_y = new_y;

        /* Ignore all EnterNotify events until the pointer actually moves */
        screen().focusControl().ignoreAtPointer();
    }

}

void FluxboxWindow::moveResizeForClient(int new_x, int new_y,
                               unsigned int new_width, unsigned int new_height, int gravity, unsigned int client_bw) {

    m_placed = true;
    frame().moveResizeForClient(new_x, new_y, new_width, new_height, gravity, client_bw);
    setFocusFlag(m_focused);
    m_state.shaded = false;
    sendConfigureNotify();

    if (!moving) {
        m_last_resize_x = new_x;
        m_last_resize_y = new_y;
    }

}

void FluxboxWindow::getMaxSize(unsigned int* width, unsigned int* height) const {
    if (width)
        *width = m_size_hint.max_width;
    if (height)
        *height = m_size_hint.max_height;
}

// returns whether the focus was "set" to this window
// it doesn't guarantee that it has focus, but says that we have
// tried. A FocusIn event should eventually arrive for that
// window if it actually got the focus, then setFocusFlag is called,
// which updates all the graphics etc
bool FluxboxWindow::focus() {

    if (((signed) (frame().x() + frame().width())) < 0) {
        if (((signed) (frame().y() + frame().height())) < 0) {
            moveResize(frame().window().borderWidth(), frame().window().borderWidth(),
                       frame().width(), frame().height());
        } else if (frame().y() > (signed) screen().height()) {
            moveResize(frame().window().borderWidth(), screen().height() - frame().height(),
                       frame().width(), frame().height());
        } else {
            moveResize(frame().window().borderWidth(), frame().y() + frame().window().borderWidth(),
                       frame().width(), frame().height());
        }
    } else if (frame().x() > (signed) screen().width()) {
        if (((signed) (frame().y() + frame().height())) < 0) {
            moveResize(screen().width() - frame().width(), frame().window().borderWidth(),
                       frame().width(), frame().height());
        } else if (frame().y() > (signed) screen().height()) {
            moveResize(screen().width() - frame().width(),
                       screen().height() - frame().height(),
                       frame().width(), frame().height());
        } else {
            moveResize(screen().width() - frame().width(),
                       frame().y() + frame().window().borderWidth(),
                       frame().width(), frame().height());
        }
    }

    if (! m_client->validateClient())
        return false;

    if (screen().currentWorkspaceID() != workspaceNumber() && !isStuck()) {

        // fetch the window to the current workspace if minimized
        if (isIconic())
            screen().sendToWorkspace(screen().currentWorkspaceID(), this, false);
        // warp to the workspace of the window
        else
            screen().changeWorkspaceID(workspaceNumber(), false);
    }

    if (isIconic()) {
        deiconify();
        m_focused = true; // signal to mapNotifyEvent to set focus when mapped
        return true; // the window probably will get focused, just not yet
    }

    // this needs to be here rather than setFocusFlag because
    // FocusControl::revertFocus will return before FocusIn events arrive
    m_screen.focusControl().setScreenFocusedWindow(*m_client);


    fbdbg<<"FluxboxWindow::"<<__FUNCTION__<<" isModal() = "<<m_client->isModal()<<endl;
    fbdbg<<"FluxboxWindow::"<<__FUNCTION__<<" transient size = "<<m_client->transients.size()<<endl;

    if (!m_client->transients.empty() && m_client->isModal()) {
        fbdbg<<__FUNCTION__<<": isModal and have transients client = "<<
            hex<<m_client->window()<<dec<<endl;
        fbdbg<<__FUNCTION__<<": this = "<<this<<endl;

        WinClient::TransientList::iterator it = m_client->transients.begin();
        WinClient::TransientList::iterator it_end = m_client->transients.end();
        for (; it != it_end; ++it) {
            fbdbg<<__FUNCTION__<<": transient 0x"<<(*it)<<endl;
            if ((*it)->isStateModal())
                return (*it)->focus();
        }
    }

    if (m_client->isModal())
        return false;

    return m_client->sendFocus();
}

// don't hide the frame directly, use this function
void FluxboxWindow::hide(bool interrupt_moving) {
   fbdbg<<"("<<__FUNCTION__<<")["<<this<<"]"<<endl;

    // resizing always stops on hides
    if (resizing)
        stopResizing(true);

    if (interrupt_moving) {
        if (moving)
            stopMoving(true);
        if (m_attaching_tab)
            attachTo(0, 0, true);
    }

    setState(IconicState, false);

    menu().hide();
    frame().hide();

    if (FocusControl::focusedFbWindow() == this)
        FocusControl::setFocusedWindow(0);
}

void FluxboxWindow::show() {
    frame().show();
    setState(NormalState, false);
}

void FluxboxWindow::toggleIconic() {
    if (isIconic())
        deiconify();
    else
        iconify();
}

/**
   Unmaps the window and removes it from workspace list
*/
void FluxboxWindow::iconify() {
    if (isIconic()) // no need to iconify if we're already
        return;

    m_state.iconic = true;
    m_statesig.emit(*this);

    hide(true);

    screen().focusControl().setFocusBack(*this);

    ClientList::iterator client_it = m_clientlist.begin();
    const ClientList::iterator client_it_end = m_clientlist.end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient &client = *(*client_it);
        WinClient::TransientList::iterator it = client.transientList().begin();
        WinClient::TransientList::iterator it_end = client.transientList().end();
        for (; it != it_end; ++it)
            if ((*it)->fbwindow())
                (*it)->fbwindow()->iconify();
    }

    // focus revert is done elsewhere (based on signal)
}

void FluxboxWindow::deiconify(bool do_raise) {
    if (numClients() == 0 || !m_state.iconic || oplock)
        return;

    oplock = true;

    // reassociate first, so it gets removed from screen's icon list
    screen().reassociateWindow(this, m_workspace_number, false);
    m_state.iconic = false;
    m_statesig.emit(*this);

    // deiconify all transients
    ClientList::iterator client_it = clientList().begin();
    ClientList::iterator client_it_end = clientList().end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient::TransientList::iterator trans_it =
            (*client_it)->transientList().begin();
        WinClient::TransientList::iterator trans_it_end =
            (*client_it)->transientList().end();
        for (; trans_it != trans_it_end; ++trans_it) {
            if ((*trans_it)->fbwindow())
                (*trans_it)->fbwindow()->deiconify(false);
        }
    }

    if (m_workspace_number != screen().currentWorkspaceID()) {
        oplock = false;
        return;
    }

    show();

    // focus new, OR if it's the only window on the workspace
    // but not on startup: focus will be handled after creating everything
    // we use m_focused as a signal to focus the window when mapped
    if (screen().currentWorkspace()->numberOfWindows() == 1 ||
        isFocusNew() || m_client->isTransient())
        m_focused = true;

    oplock = false;

    if (do_raise)
        raise();
}

/** setFullscreen mode:

    - maximize as big as the screen is, dont care about slit / toolbar
    - raise to toplayer
*/
void FluxboxWindow::setFullscreen(bool flag) {

    if (!m_initialized) {
        // this will interfere with window placement, so we delay it
        m_state.fullscreen = flag;
        return;
    }

    if (flag && !isFullscreen()) {

        m_old_layernum = layerNum();
        m_state.fullscreen = true;
        frame().applyState();

        setFullscreenLayer(); // calls stateSig().emit()
        if (!isFocused()) {
            join(screen().focusedWindowSig(),
                 FbTk::MemFun(*this, &FluxboxWindow::focusedWindowChanged));
        }

    } else if (!flag && isFullscreen()) {

        m_state.fullscreen = false;
        frame().applyState();

        moveToLayer(m_old_layernum);
        stateSig().emit(*this);
    }

    attachWorkAreaSig();
}

void FluxboxWindow::setFullscreenLayer() {

    FluxboxWindow *foc = FocusControl::focusedFbWindow();
    // if another window on the same head is focused, make sure we can see it
    if (isFocused() || !foc || &foc->screen() != &screen() ||
        getOnHead() != foc->getOnHead() ||
        (foc->winClient().isTransient() &&
         foc->winClient().transientFor()->fbwindow() == this)) {
        moveToLayer(::ResourceLayer::ABOVE_DOCK);
    } else {
        moveToLayer(foc->layerNum());
        foc->raise();
    }
    stateSig().emit(*this);

}

void FluxboxWindow::attachWorkAreaSig() {
    // notify when struts change, so we can resize accordingly
    // Subject checks for duplicates for us
    // XXX: this is no longer true with signals
    if (m_state.maximized || m_state.fullscreen)
        join(screen().workspaceAreaSig(),
             FbTk::MemFun(*this, &FluxboxWindow::workspaceAreaChanged));
    else
        leave(screen().workspaceAreaSig());
}

/**
   Maximize window both horizontal and vertical
*/
void FluxboxWindow::maximize(int type) {
    int new_max = m_state.queryToggleMaximized(type);
    setMaximizedState(new_max);
}

void FluxboxWindow::setMaximizedState(int type) {

    if (!m_initialized || type == m_state.maximized) {
        // this will interfere with window placement, so we delay it
        m_state.maximized = type;
        return;
    }

    if (isResizing())
        stopResizing();

    if (isShaded()) {
        // do not call ::shade() here to trigger frame().applyState() and
        // stateSig().emit() only once
        m_state.shaded = false;
    }

    m_state.maximized = type;
    frame().applyState();

    attachWorkAreaSig();

    // notify listeners that we changed state
    stateSig().emit(*this);
}

void FluxboxWindow::disableMaximization() {

    m_state.maximized = WindowState::MAX_NONE;
    // TODO: could be optional, if the window gets back to original size /
    // position after maximization is disabled
    m_state.saveGeometry(frame().x(), frame().y(),
                         frame().width(), frame().height());
    frame().applyState();
    stateSig().emit(*this);
}


/**
 * Maximize window horizontal
 */
void FluxboxWindow::maximizeHorizontal() {
    maximize(WindowState::MAX_HORZ);
}

/**
 * Maximize window vertical
 */
void FluxboxWindow::maximizeVertical() {
    maximize(WindowState::MAX_VERT);
}

/**
 * Maximize window fully
 */
void FluxboxWindow::maximizeFull() {
    maximize(WindowState::MAX_FULL);
}

void FluxboxWindow::setWorkspace(int n) {
    unsigned int old_wkspc = m_workspace_number;

    m_workspace_number = n;

    // notify workspace change
    if (m_initialized && old_wkspc != m_workspace_number) {
        fbdbg<<this<<" emit workspace signal"<<endl;
        m_workspacesig.emit(*this);
    }
}

void FluxboxWindow::setLayerNum(int layernum) {
    m_state.layernum = layernum;

    if (m_initialized) {
        fbdbg<<this<<" notify layer signal"<<endl;
        m_layersig.emit(*this);
    }
}

void FluxboxWindow::shade() {
    // we can only shade if we have a titlebar
    if (!decorations.titlebar)
        return;

    m_state.shaded = !m_state.shaded;
    if (!m_initialized)
        return;

    frame().applyState();
    stateSig().emit(*this);
    // TODO: this should set IconicState, but then we can't focus the window
}

void FluxboxWindow::shadeOn() {
    if (!m_state.shaded)
        shade();
}

void FluxboxWindow::shadeOff() {
    if (m_state.shaded)
        shade();
}

void FluxboxWindow::setShaded(bool val) {
    if (val != m_state.shaded)
        shade();
}

void FluxboxWindow::stick() {

    m_state.stuck = !m_state.stuck;

    if (m_initialized) {
        stateSig().emit(*this);
        // notify since some things consider "stuck" to be a pseudo-workspace
        m_workspacesig.emit(*this);
    }

    ClientList::iterator client_it = clientList().begin();
    ClientList::iterator client_it_end = clientList().end();
    for (; client_it != client_it_end; ++client_it) {

        WinClient::TransientList::const_iterator it = (*client_it)->transientList().begin();
        WinClient::TransientList::const_iterator it_end = (*client_it)->transientList().end();
        for (; it != it_end; ++it) {
            if ((*it)->fbwindow())
                (*it)->fbwindow()->setStuck(m_state.stuck);
        }

    }

}

void FluxboxWindow::setStuck(bool val) {
    if (val != m_state.stuck)
        stick();
}

void FluxboxWindow::setIconic(bool val) {
    if (!val && isIconic())
        deiconify();
    if (val && !isIconic())
        iconify();
}

void FluxboxWindow::raise() {
    if (isIconic())
        return;

    fbdbg<<"FluxboxWindow("<<title().logical()<<")::raise()[layer="<<layerNum()<<"]"<<endl;

    // get root window
    WinClient *client = getRootTransientFor(m_client);

    // if we have transient_for then we should put ourself last in
    // transients list so we get raised last and thus gets above the other transients
    if (m_client->transientFor() && m_client != m_client->transientFor()->transientList().back()) {
        // remove and push back so this window gets raised last
        m_client->transientFor()->transientList().remove(m_client);
        m_client->transientFor()->transientList().push_back(m_client);
    }
    // raise this window and every transient in it with this one last
    if (client->fbwindow()) {
        // doing this on startup messes up the focus order
        if (!Fluxbox::instance()->isStartup() && client->fbwindow() != this &&
                &client->fbwindow()->winClient() != client)
            // activate the client so the transient won't get pushed back down
            client->fbwindow()->setCurrentClient(*client, false);
        raiseFluxboxWindow(*client->fbwindow());
    }

}

void FluxboxWindow::lower() {
    if (isIconic())
        return;

    fbdbg<<"FluxboxWindow("<<title().logical()<<")::lower()"<<endl;

    /* Ignore all EnterNotify events until the pointer actually moves */
    screen().focusControl().ignoreAtPointer();

    // get root window
    WinClient *client = getRootTransientFor(m_client);

    if (client->fbwindow())
        lowerFluxboxWindow(*client->fbwindow());
}

void FluxboxWindow::tempRaise() {
    // Note: currently, this causes a problem with cycling through minimized
    // clients if this window has more than one tab, since the window will not
    // match isIconic() when the rest of the tabs get checked
    if (isIconic())
        deiconify();

    // the root transient will get raised when we stop cycling
    // raising it here causes problems when it isn't the active tab
    tempRaiseFluxboxWindow(*this);
}


void FluxboxWindow::changeLayer(int diff) {
    moveToLayer(m_state.layernum+diff);
}

void FluxboxWindow::moveToLayer(int layernum, bool force) {

    fbdbg<<"FluxboxWindow("<<title().logical()<<")::moveToLayer("<<layernum<<")"<<endl;

    // don't let it set its layer into menu area
    if (layernum <= ::ResourceLayer::MENU)
        layernum = ::ResourceLayer::MENU + 1;
    else if (layernum >= ::ResourceLayer::NUM_LAYERS)
        layernum = ::ResourceLayer::NUM_LAYERS - 1;

    if (!m_initialized)
        m_state.layernum = layernum;

    if ((m_state.layernum == layernum && !force) || !m_client)
        return;

    // get root window
    WinClient *client = getRootTransientFor(m_client);

    FluxboxWindow *win = client->fbwindow();
    if (!win) return;

    win->layerItem().moveToLayer(layernum);
    // remember number just in case a transient happens to revisit this window
    layernum = win->layerItem().getLayerNum();
    win->setLayerNum(layernum);

    // move all the transients, too
    ClientList::iterator client_it = win->clientList().begin();
    ClientList::iterator client_it_end = win->clientList().end();
    for (; client_it != client_it_end; ++client_it) {

        WinClient::TransientList::const_iterator it = (*client_it)->transientList().begin();
        WinClient::TransientList::const_iterator it_end = (*client_it)->transientList().end();
        for (; it != it_end; ++it) {
            FluxboxWindow *fbwin = (*it)->fbwindow();
            if (fbwin && !fbwin->isIconic()) {
                fbwin->layerItem().moveToLayer(layernum);
                fbwin->setLayerNum(layernum);
            }
        }

    }

}

void FluxboxWindow::setFocusHidden(bool value) {
    m_state.focus_hidden = value;
    if (m_initialized)
        m_statesig.emit(*this);
}

void FluxboxWindow::setIconHidden(bool value) {
    m_state.icon_hidden = value;
    if (m_initialized)
        m_statesig.emit(*this);
}


// window has actually RECEIVED focus (got a FocusIn event)
// so now we make it a focused frame etc
void FluxboxWindow::setFocusFlag(bool focus) {
    if (!m_client) return;

    bool was_focused = isFocused();
    m_focused = focus;

    fbdbg<<"FluxboxWindow("<<title().logical()<<")::setFocusFlag("<<focus<<")"<<endl;


    installColormap(focus);

    // if we're fullscreen and another window gains focus on the same head,
    // then we need to let the user see it
    if (m_state.fullscreen && !focus) {
        join(screen().focusedWindowSig(),
             FbTk::MemFun(*this, &FluxboxWindow::focusedWindowChanged));
    }

    if (m_state.fullscreen && focus) {
        moveToLayer(::ResourceLayer::ABOVE_DOCK);
        leave(screen().focusedWindowSig());
    }

    if (focus != frame().focused())
        frame().setFocus(focus);

    if (focus && screen().focusControl().isCycling())
        tempRaise();
    else if (screen().doAutoRaise()) {
        if (m_focused)
            m_timer.start();
        else
            m_timer.stop();
    }

    // did focus change? notify listeners
    if (was_focused != focus) {
        m_attention_state = false;
        notifyFocusChanged();
        if (m_client)
            m_client->notifyFocusChanged();
        Fluxbox::instance()->keys()->doAction(focus ? FocusIn : FocusOut, 0, 0,
                                              Keys::ON_WINDOW, m_client);
    }
}


void FluxboxWindow::installColormap(bool install) {
    if (m_client == 0) return;

    Fluxbox *fluxbox = Fluxbox::instance();
    fluxbox->grab();
    if (! m_client->validateClient())
        return;

    int i = 0, ncmap = 0;
    Colormap *cmaps = XListInstalledColormaps(display, m_client->window(), &ncmap);
    XWindowAttributes wattrib;
    if (cmaps) { //!!
        if (m_client->getAttrib(wattrib)) {
            if (install) {
                // install the window's colormap
                for (i = 0; i < ncmap; i++) {
                    if (*(cmaps + i) == wattrib.colormap) {
                        // this window is using an installed color map... do not install
                        install = false;
                        break; //end for-loop (we dont need to check more)
                    }
                }
                // otherwise, install the window's colormap
                if (install)
                    XInstallColormap(display, wattrib.colormap);
            } else {
                for (i = 0; i < ncmap; i++) { // uninstall the window's colormap
                    if (*(cmaps + i) == wattrib.colormap)
                       XUninstallColormap(display, wattrib.colormap);
                }
            }
        }

        XFree(cmaps);
    }

    fluxbox->ungrab();
}

/**
   Show the window menu at pos x, y
*/
void FluxboxWindow::showMenu(int x, int y) {

    menu().reloadHelper()->checkReload();
    FbMenu::setWindow(this);
    screen().placementStrategy()
        .placeAndShowMenu(menu(), x, y, true);
}

void FluxboxWindow::popupMenu(int x, int y) {
    // hide menu if it was opened for this window before
    if (menu().isVisible() && FbMenu::window() == this) {
       menu().hide();
       return;
    }

    menu().disableTitle();

    showMenu(x, y);
}

/**
   Moves the menu to last button press position and shows it,
   if it's already visible it'll be hidden
 */
void FluxboxWindow::popupMenu() {

    if (m_last_button_x < x() || m_last_button_x > x() + static_cast<signed>(width()))
        m_last_button_x = x();

    popupMenu(m_last_button_x, frame().titlebarHeight() + frame().y());
}

void FluxboxWindow::startMoving(int x, int y) {

    if (isMoving()) {
        return;
    }

    if (s_num_grabs > 0) {
        return;
    }

    if ((isMaximized() || isFullscreen()) && screen().getMaxDisableMove())
        return;

    // save first event point
    m_last_resize_x = x;
    m_last_resize_y = y;
    m_button_grab_x = x - frame().x() - frame().window().borderWidth();
    m_button_grab_y = y - frame().y() - frame().window().borderWidth();

    moving = true;

    Fluxbox *fluxbox = Fluxbox::instance();
    // grabbing (and masking) on the root window allows us to
    // freely map and unmap the window we're moving.
    grabPointer(screen().rootWindow().window(), False, ButtonMotionMask |
                ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                screen().rootWindow().window(), frame().theme()->moveCursor(), CurrentTime);

    if (menu().isVisible())
        menu().hide();

    fluxbox->maskWindowEvents(screen().rootWindow().window(), this);

    m_last_move_x = frame().x();
    m_last_move_y = frame().y();
    if (! screen().doOpaqueMove()) {
        fluxbox->grab();
        parent().drawRectangle(screen().rootTheme()->opGC(),
                               frame().x(), frame().y(),
                               frame().width() + 2*frame().window().borderWidth()-1,
                               frame().height() + 2*frame().window().borderWidth()-1);
        screen().showPosition(frame().x(), frame().y());
    }
}

void FluxboxWindow::stopMoving(bool interrupted) {
    moving = false;
    Fluxbox *fluxbox = Fluxbox::instance();

    fluxbox->maskWindowEvents(0, 0);

    if (! screen().doOpaqueMove()) {
        parent().drawRectangle(screen().rootTheme()->opGC(),
                               m_last_move_x, m_last_move_y,
                               frame().width() + 2*frame().window().borderWidth()-1,
                               frame().height() + 2*frame().window().borderWidth()-1);
        if (!interrupted) {
            moveResize(m_last_move_x, m_last_move_y, frame().width(), frame().height());
            if (m_workspace_number != screen().currentWorkspaceID())
                screen().sendToWorkspace(screen().currentWorkspaceID(), this);
            focus();
        }
        fluxbox->ungrab();
    } else if (!interrupted) {
        moveResize(frame().x(), frame().y(), frame().width(), frame().height(), true);
        frame().notifyMoved(true);
    }


    screen().hidePosition();
    ungrabPointer(CurrentTime);

    FbTk::App::instance()->sync(false); //make sure the redraw is made before we continue

    // if Head has been changed we want it to redraw by current state
    if (m_state.maximized || m_state.fullscreen) {
        frame().applyState();
        attachWorkAreaSig();
        stateSig().emit(*this);
    }
}

/**
 * Helper function that snaps a window to another window
 * We snap if we're closer than the x/ylimits.
 */
inline void snapToWindow(int &xlimit, int &ylimit,
                         int left, int right, int top, int bottom,
                         int oleft, int oright, int otop, int obottom,
                         bool resize) {
    // Only snap if we're adjacent to the edge we're looking at

    // for left + right, need to be in the right y range
    if (top <= obottom && bottom >= otop) {
        // left
        if (abs(left-oleft)  < abs(xlimit)) xlimit = -(left-oleft);
        if (abs(right-oleft) < abs(xlimit)) xlimit = -(right-oleft);

        // right
        if (abs(left-oright)  < abs(xlimit)) xlimit = -(left-oright);
        if (!resize && abs(right-oright) < abs(xlimit)) xlimit = -(right-oright);
    }

    // for top + bottom, need to be in the right x range
    if (left <= oright && right >= oleft) {
        // top
        if (abs(top-otop)    < abs(ylimit)) ylimit = -(top-otop);
        if (abs(bottom-otop) < abs(ylimit)) ylimit = -(bottom-otop);

        // bottom
        if (abs(top-obottom)    < abs(ylimit)) ylimit = -(top-obottom);
        if (!resize && abs(bottom-obottom) < abs(ylimit)) ylimit = -(bottom-obottom);
    }

}

/*
 * Do Whatever snapping magic is necessary, and return using the orig_left
 * and orig_top variables to indicate the new x,y position
 */
void FluxboxWindow::doSnapping(int &orig_left, int &orig_top, bool resize) {
    /*
     * Snap to screen/head edges
     * Snap to windows
     */
    int threshold;

    if (resize) {
        threshold = screen().getEdgeResizeSnapThreshold();
    } else {
        threshold = screen().getEdgeSnapThreshold();
    }

    if (0 == threshold) return;

    // Keep track of our best offsets so far
    // We need to find things less than or equal to the threshold
    int dx = threshold + 1;
    int dy = threshold + 1;

    // we only care about the left/top etc that includes borders
    int borderW = 0;

    if (decorationMask() & (WindowState::DECORM_BORDER|WindowState::DECORM_HANDLE))
        borderW = frame().window().borderWidth();

    int top = orig_top; // orig include the borders
    int left = orig_left;

    int right = orig_left + width() + 2 * borderW;
    int bottom = orig_top + height() + 2 * borderW;

    // test against tabs too
    bool i_have_tabs = frame().externalTabMode();
    int xoff = 0, yoff = 0, woff = 0, hoff = 0;
    if (i_have_tabs) {
        xoff = xOffset();
        yoff = yOffset();
        woff = widthOffset();
        hoff = heightOffset();
    }

    /////////////////////////////////////
    // begin by checking the screen (or Xinerama head) edges

    int starth = 0;

    // head "0" == whole screen width + height, which we skip since the
    // sum of all the heads covers those edges, if >1 head
    if (screen().numHeads() > 0)
        starth=1;

    for (int h=starth; h <= screen().numHeads(); h++) {
        snapToWindow(dx, dy, left, right, top, bottom,
                     screen().maxLeft(h),
                     screen().maxRight(h),
                     screen().maxTop(h),
                     screen().maxBottom(h),
                     resize);

        if (i_have_tabs)
            snapToWindow(dx, dy, left - xoff, right - xoff + woff, top - yoff, bottom - yoff + hoff,
                         screen().maxLeft(h),
                         screen().maxRight(h),
                         screen().maxTop(h),
                         screen().maxBottom(h),
                         resize);
    }
    for (int h=starth; h <= screen().numHeads(); h++) {
        snapToWindow(dx, dy, left, right, top, bottom,
                     screen().getHeadX(h),
                     screen().getHeadX(h) + screen().getHeadWidth(h),
                     screen().getHeadY(h),
                     screen().getHeadY(h) + screen().getHeadHeight(h),
                     resize);

        if (i_have_tabs)
            snapToWindow(dx, dy, left - xoff, right - xoff + woff, top - yoff, bottom - yoff + hoff,
                         screen().getHeadX(h),
                         screen().getHeadX(h) + screen().getHeadWidth(h),
                         screen().getHeadY(h),
                         screen().getHeadY(h) + screen().getHeadHeight(h),
                         resize);
    }

    /////////////////////////////////////
    // now check window edges

    Workspace::Windows &wins =
        screen().currentWorkspace()->windowList();

    Workspace::Windows::iterator it = wins.begin();
    Workspace::Windows::iterator it_end = wins.end();

    unsigned int bw;
    for (; it != it_end; ++it) {
        if ((*it) == this)
            continue; // skip myself

        bw = (*it)->decorationMask() & (WindowState::DECORM_BORDER|WindowState::DECORM_HANDLE) ?
                (*it)->frame().window().borderWidth() : 0;

        snapToWindow(dx, dy, left, right, top, bottom,
                     (*it)->x(),
                     (*it)->x() + (*it)->width() + 2 * bw,
                     (*it)->y(),
                     (*it)->y() + (*it)->height() + 2 * bw,
                     resize);

        if (i_have_tabs)
            snapToWindow(dx, dy, left - xoff, right - xoff + woff, top - yoff, bottom - yoff + hoff,
                         (*it)->x(),
                         (*it)->x() + (*it)->width() + 2 * bw,
                         (*it)->y(),
                         (*it)->y() + (*it)->height() + 2 * bw,
                         resize);

        // also snap to the box containing the tabs (don't bother with actual
        // tab edges, since they're dynamic
        if ((*it)->frame().externalTabMode())
            snapToWindow(dx, dy, left, right, top, bottom,
                         (*it)->x() - (*it)->xOffset(),
                         (*it)->x() - (*it)->xOffset() + (*it)->width() + 2 * bw + (*it)->widthOffset(),
                         (*it)->y() - (*it)->yOffset(),
                         (*it)->y() - (*it)->yOffset() + (*it)->height() + 2 * bw + (*it)->heightOffset(),
                         resize);

        if (i_have_tabs)
            snapToWindow(dx, dy, left - xoff, right - xoff + woff, top - yoff, bottom - yoff + hoff,
                         (*it)->x() - (*it)->xOffset(),
                         (*it)->x() - (*it)->xOffset() + (*it)->width() + 2 * bw + (*it)->widthOffset(),
                         (*it)->y() - (*it)->yOffset(),
                         (*it)->y() - (*it)->yOffset() + (*it)->height() + 2 * bw + (*it)->heightOffset(),
                         resize);

    }

    // commit
    if (dx <= threshold)
        orig_left += dx;
    if (dy <= threshold)
        orig_top  += dy;

}


FluxboxWindow::ReferenceCorner FluxboxWindow::getResizeDirection(int x, int y,
        ResizeModel model, int corner_size_px, int corner_size_pc) const
{
    if (model == TOPLEFTRESIZE)     return LEFTTOP;
    if (model == TOPRESIZE)         return TOP;
    if (model == TOPRIGHTRESIZE)    return RIGHTTOP;
    if (model == LEFTRESIZE)        return LEFT;
    if (model == RIGHTRESIZE)       return RIGHT;
    if (model == BOTTOMLEFTRESIZE)  return LEFTBOTTOM;
    if (model == BOTTOMRESIZE)      return BOTTOM;
    if (model == CENTERRESIZE)      return CENTER;

    if (model == EDGEORCORNERRESIZE)
    {
        int w = frame().width();
        int h = frame().height();
        int cx = w / 2;
        int cy = h / 2;
        TestCornerHelper test_corner = { corner_size_px, corner_size_pc };
        if (x < cx  &&  test_corner(x, cx)) {
            if (y < cy  &&  test_corner(y, cy))
                return LEFTTOP;
            else if (test_corner(h - y - 1, h - cy))
                return LEFTBOTTOM;
        } else if (test_corner(w - x - 1, w - cx)) {
            if (y < cy  &&  test_corner(y, cy))
                return RIGHTTOP;
            else if (test_corner(h - y - 1, h - cy))
                return RIGHTBOTTOM;
        }

        /* Nope, not a corner; find the nearest edge instead. */
        if (cy - abs(y - cy) < cx - abs(x - cx)) // y is nearest
            return (y > cy) ? BOTTOM : TOP;
        else
            return (x > cx) ? RIGHT : LEFT;
    }
    return RIGHTBOTTOM;
}

void FluxboxWindow::startResizing(int x, int y, ReferenceCorner dir) {

    if (isResizing())
        return;

    if (s_num_grabs > 0 || isShaded() || isIconic() )
        return;

    if ((isMaximized() || isFullscreen()) && screen().getMaxDisableResize())
        return;

    m_resize_corner = dir;

    resizing = true;

    disableMaximization();

    const Cursor& cursor = (m_resize_corner == LEFTTOP) ? frame().theme()->upperLeftAngleCursor() :
                           (m_resize_corner == RIGHTTOP) ? frame().theme()->upperRightAngleCursor() :
                           (m_resize_corner == RIGHTBOTTOM) ? frame().theme()->lowerRightAngleCursor() :
                           (m_resize_corner == LEFT) ? frame().theme()->leftSideCursor() :
                           (m_resize_corner == RIGHT) ? frame().theme()->rightSideCursor() :
                           (m_resize_corner == TOP) ? frame().theme()->topSideCursor() :
                           (m_resize_corner == BOTTOM) ? frame().theme()->bottomSideCursor() :
                                                            frame().theme()->lowerLeftAngleCursor();

    grabPointer(fbWindow().window(),
                false, ButtonMotionMask | ButtonReleaseMask,
                GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);

    m_button_grab_x = x + frame().x();
    m_button_grab_y = y + frame().y();
    resize_base_x = m_last_resize_x = frame().x();
    resize_base_y = m_last_resize_y = frame().y();
    resize_base_w = m_last_resize_w = frame().width();
    resize_base_h = m_last_resize_h = frame().height();

    fixSize();
    frame().displaySize(m_last_resize_w, m_last_resize_h);

    if (!screen().doOpaqueResize()) {
        parent().drawRectangle(screen().rootTheme()->opGC(),
                       m_last_resize_x, m_last_resize_y,
                       m_last_resize_w - 1 + 2 * frame().window().borderWidth(),
                       m_last_resize_h - 1 + 2 * frame().window().borderWidth());
    }
}

void FluxboxWindow::stopResizing(bool interrupted) {
    resizing = false;

    if (!screen().doOpaqueResize()) {
        parent().drawRectangle(screen().rootTheme()->opGC(),
                           m_last_resize_x, m_last_resize_y,
                           m_last_resize_w - 1 + 2 * frame().window().borderWidth(),
                           m_last_resize_h - 1 + 2 * frame().window().borderWidth());
    }

    screen().hideGeometry();

    if (!interrupted) {
        fixSize();

        moveResize(m_last_resize_x, m_last_resize_y,
                   m_last_resize_w, m_last_resize_h);
    }

    ungrabPointer(CurrentTime);
}

WinClient* FluxboxWindow::winClientOfLabelButtonWindow(Window window) {
    WinClient* result = 0;
    Client2ButtonMap::iterator it =
        find_if(m_labelbuttons.begin(),
                m_labelbuttons.end(),
                Compose(std::bind(equal_to<Window>(), _1, window),
                        Compose(mem_fn(&FbTk::Button::window),
                                Select2nd<Client2ButtonMap::value_type>())));
    if (it != m_labelbuttons.end())
        result = it->first;

    return result;
}

void FluxboxWindow::startTabbing(const XButtonEvent &be) {

    if (s_num_grabs > 0)
        return;

    s_original_workspace = workspaceNumber();
    m_attaching_tab = winClientOfLabelButtonWindow(be.window);

    // start drag'n'drop for tab
    grabPointer(be.window, False, ButtonMotionMask |
                ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                None, frame().theme()->moveCursor(), CurrentTime);

    // relative position on the button
    m_button_grab_x = be.x;
    m_button_grab_y = be.y;
    // position of the button
    m_last_move_x = be.x_root - be.x;
    m_last_move_y = be.y_root - be.y;
    // hijack extra vars for initial grab location
    m_last_resize_x = be.x_root;
    m_last_resize_y = be.y_root;

    Fluxbox::instance()->grab();

    if (m_attaching_tab) {
        FbTk::TextButton &active_button = *m_labelbuttons[m_attaching_tab];
        m_last_resize_w = active_button.width();
        m_last_resize_h = active_button.height();
    } else {
        m_attaching_tab = m_client;
        unsigned int bw = 2*frame().window().borderWidth()-1;
        m_last_resize_w = frame().width() + bw;
        m_last_resize_h = frame().height() + bw;
    }

    parent().drawRectangle(screen().rootTheme()->opGC(),
                           m_last_move_x, m_last_move_y,
                           m_last_resize_w, m_last_resize_h);

    menu().hide();
}

void FluxboxWindow::attachTo(int x, int y, bool interrupted) {
    if (m_attaching_tab == 0)
        return;

    parent().drawRectangle(screen().rootTheme()->opGC(),
                           m_last_move_x, m_last_move_y,
                           m_last_resize_w, m_last_resize_h);

    ungrabPointer(CurrentTime);

    Fluxbox::instance()->ungrab();

    // make sure we clean up here, since this object may be deleted inside attachClient
    WinClient *old_attached = m_attaching_tab;
    m_attaching_tab = 0;

    if (interrupted)
        return;

    int dest_x = 0, dest_y = 0;
    Window child = 0;
    if (XTranslateCoordinates(display, parent().window(),
                              parent().window(),
                              x, y, &dest_x, &dest_y, &child)) {

        bool inside_titlebar = false;
        // search for a fluxboxwindow
        WinClient *client = Fluxbox::instance()->searchWindow(child);
        FluxboxWindow *attach_to_win = 0;
        if (client) {

            inside_titlebar = client->fbwindow()->hasTitlebar() &&
                client->fbwindow()->y() + static_cast<signed>(client->fbwindow()->titlebarHeight()) > dest_y;

            Fluxbox::TabsAttachArea area= Fluxbox::instance()->getTabsAttachArea();
            if (area == Fluxbox::ATTACH_AREA_WINDOW)
                attach_to_win = client->fbwindow();
            else if (area == Fluxbox::ATTACH_AREA_TITLEBAR && inside_titlebar) {
                attach_to_win = client->fbwindow();
            }
        }

        if (attach_to_win != this &&
            attach_to_win != 0 && attach_to_win->isTabable()) {

            attach_to_win->attachClient(*old_attached,x,y );
            // we could be deleted here, DO NOT do anything else that alters this object
        } else if (attach_to_win != this || (attach_to_win == this && !inside_titlebar)) {
            // disconnect client if we didn't drop on a window
            WinClient &client = *old_attached;
            detachClient(*old_attached);
            screen().sendToWorkspace(s_original_workspace, this, false);
            if (FluxboxWindow *fbwin = client.fbwindow())
                fbwin->move(m_last_move_x, m_last_move_y);
        } else if( attach_to_win == this && attach_to_win->isTabable()) {
            //reording of tabs within a frame
            moveClientTo(*old_attached, x, y);
        }
    }
}

// grab pointer and increase counter.
// we need this to count grab pointers,
// especially at startup, where we can drag/resize while starting
// and causing it to send events to windows later on and make
// two different windows do grab pointer which only one window
// should do at the time
void FluxboxWindow::grabPointer(Window grab_window,
                                Bool owner_events,
                                unsigned int event_mask,
                                int pointer_mode, int keyboard_mode,
                                Window confine_to,
                                Cursor cursor,
                                Time time) {

    XGrabPointer(FbTk::App::instance()->display(),
                 grab_window,
                 owner_events,
                 event_mask,
                 pointer_mode, keyboard_mode,
                 confine_to,
                 cursor,
                 time);
    s_num_grabs++;
}

// ungrab and decrease counter
void FluxboxWindow::ungrabPointer(Time time) {
    XUngrabPointer(FbTk::App::instance()->display(), time);
    s_num_grabs--;
    if (s_num_grabs < 0)
        s_num_grabs = 0;
}

FluxboxWindow::ReferenceCorner FluxboxWindow::getCorner(string str) {
    str = FbTk::StringUtil::toLower(str);
    if (str == "lefttop" || str == "topleft" || str == "upperleft" || str == "")
        return LEFTTOP;
    if (str == "top" || str == "upper" || str == "topcenter")
        return TOP;
    if (str == "righttop" || str == "topright" || str == "upperright")
        return RIGHTTOP;
    if (str == "left" || str == "leftcenter")
        return LEFT;
    if (str == "center" || str == "wincenter")
        return CENTER;
    if (str == "right" || str == "rightcenter")
        return RIGHT;
    if (str == "leftbottom" || str == "bottomleft" || str == "lowerleft")
        return LEFTBOTTOM;
    if (str == "bottom" || str == "lower" || str == "bottomcenter")
        return BOTTOM;
    if (str == "rightbottom" || str == "bottomright" || str == "lowerright")
        return RIGHTBOTTOM;
    return ERROR;
}

void FluxboxWindow::translateXCoords(int &x, ReferenceCorner dir) const {
    int head = getOnHead(), bw = 2 * frame().window().borderWidth(),
        left = screen().maxLeft(head), right = screen().maxRight(head);
    int w = width();

    if (dir == LEFTTOP || dir == LEFT || dir == LEFTBOTTOM)
        x += left;
    if (dir == RIGHTTOP || dir == RIGHT || dir == RIGHTBOTTOM)
        x = right - w - bw - x;
    if (dir == TOP || dir == CENTER || dir == BOTTOM)
        x += (left + right - w - bw)/2;
}

void FluxboxWindow::translateYCoords(int &y, ReferenceCorner dir) const {
    int head = getOnHead(), bw = 2 * frame().window().borderWidth(),
        top = screen().maxTop(head), bottom = screen().maxBottom(head);
    int h = height();

    if (dir == LEFTTOP || dir == TOP || dir == RIGHTTOP)
        y += top;
    if (dir == LEFTBOTTOM || dir == BOTTOM || dir == RIGHTBOTTOM)
        y = bottom - h - bw - y;
    if (dir == LEFT || dir == CENTER || dir == RIGHT)
        y += (top + bottom - h - bw)/2;
}

void FluxboxWindow::translateCoords(int &x, int &y, ReferenceCorner dir) const {
  translateXCoords(x, dir);
  translateYCoords(y, dir);
}

int FluxboxWindow::getOnHead() const {
    return screen().getHead(fbWindow());
}

void FluxboxWindow::setOnHead(int head) {
    if (head > 0 && head <= screen().numHeads()) {
        int cur = screen().getHead(fbWindow());
        bool placed = m_placed;
        int x = frame().x(), y = frame().y();
        const int w = frame().width(), h = frame().height(), bw = frame().window().borderWidth();
        const int sx = screen().getHeadX(cur), sw = screen().getHeadWidth(cur),
                  sy = screen().getHeadY(cur), sh = screen().getHeadHeight(cur);
        int d = sx + sw - (x + bw + w);
        if (std::abs(sx - x) > bw && std::abs(d) <= bw) // right aligned
            x = screen().getHeadX(head) + screen().getHeadWidth(head) - (w + bw + d);
        else // calc top-left relative position
            x = screen().getHeadWidth(head) * (x - sx) / sw + screen().getHeadX(head);
        d = sy + sh - (y + bw + h);
        if (std::abs(sy - y) > bw && std::abs(d) <= bw) // bottom aligned
            y = screen().getHeadY(head) + screen().getHeadHeight(head) - (h + bw + d);
        else // calc top-left relative position
            y = screen().getHeadHeight(head) * (y - sy) / sh + screen().getHeadY(head);
        move(x, y);
        m_placed = placed;
    }

    // if Head has been changed we want it to redraw by current state
    if (m_state.maximized || m_state.fullscreen) {
        frame().applyState();
        attachWorkAreaSig();
        stateSig().emit(*this);
    }
}

void FluxboxWindow::placeWindow(int head) {
    int new_x, new_y;
    // we ignore the return value,
    // the screen placement strategy is guaranteed to succeed.
    screen().placementStrategy().placeWindow(*this, head, new_x, new_y);
    m_state.saveGeometry(new_x, new_y, frame().width(), frame().height(), true);
    move(new_x, new_y);
}
