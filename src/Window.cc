// Window.cc for Fluxbox Window Manager
// Copyright (c) 2001 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//
// Window.cc for Blackbox - an X11 Window manager
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





bool isWindowVisibleOnSomeHeadOrScreen(FluxboxWindow const& w) {
    int real_x = w.frame().x();
    int real_y = w.frame().y();

    if (w.screen().hasXinerama()) { // xinerama available => use head info
        return (0 != w.screen().getHead(real_x, real_y)); // if visible on some head
    }
    return RectangleUtil::insideRectangle(0, 0, w.screen().width(), w.screen().height(), real_x, real_y);
}

class SetClientCmd:public FbTk::Command<void> {
public:
    explicit SetClientCmd(WinClient &client):m_client(client) { }
    void execute() {
        m_client.focus();
    }
private:
    WinClient &m_client;
};


/// helper class for some STL routines




}


int FluxboxWindow::s_num_grabs = 0;

FluxboxWindow::FluxboxWindow(WinClient &client):
    Focusable(client.screen(), this),
    oplock(false),
    m_creation_time(0),
    moving(false), resizing(false),
    m_initialized(false),
    m_attaching_tab(0),
    display(FbTk::App::instance()->display()),
    m_button_grab_x(0), m_button_grab_y(0),
    m_last_move_x(0), m_last_move_y(0),
    m_last_resize_h(1), m_last_resize_w(1),
    m_last_pressed_button(0),
    m_workspace_number(0),
    m_current_state(0),
    m_old_decoration_mask(0),
    m_client(&client),
    m_toggled_decos(false),
    m_focus_protection(Focus::NoProtection),
    m_mouse_focus(BoolAcc(screen().focusControl(), &FocusControl::isMouseFocus)),
    m_click_focus(true),
    m_last_button_x(0),  m_last_button_y(0),
    m_button_theme(*this, screen().focusedWinButtonTheme(),
                   screen().unfocusedWinButtonTheme()),
    m_theme(*this, screen().focusedWinFrameTheme(),
            screen().unfocusedWinFrameTheme()),
    m_frame(client.screen(), client.depth(), m_state, m_theme),
    m_placed(false),
    m_old_layernum(0),
    m_parent(client.screen().rootWindow()),
    m_resize_corner(RIGHTBOTTOM) {

    join(m_theme.reconfigSig(), FbTk::MemFun(*this, &FluxboxWindow::themeReconfigured));
    join(m_frame.frameExtentSig(), FbTk::MemFun(*this, &FluxboxWindow::frameExtentChanged));

    init();

    if (!isManaged())
        return;

    // add the window to the focus list
    // always add to front on startup to keep the focus order the same
    if (isFocused() || Fluxbox::instance()->isStartup())
        screen().focusControl().addFocusWinFront(*this);
    else
        screen().focusControl().addFocusWinBack(*this);

    Fluxbox::instance()->keys()->registerWindow(frame().window().window(),
                                                *this, Keys::ON_WINDOW);

}


FluxboxWindow::~FluxboxWindow() {
    if (WindowCmd<void>::window() == this)
        WindowCmd<void>::setWindow(0);
    if (FbMenu::window() == this)
        FbMenu::setWindow(0);
    if ( Fluxbox::instance()->keys() != 0 ) {
        Fluxbox::instance()->keys()->
            unregisterWindow(frame().window().window());
    }

    fbdbg << "starting ~FluxboxWindow(" << this << "," 
        << (m_client ? m_client->title().logical().c_str() : "") << ")" << endl
        << "num clients = " << numClients() << endl
        << "curr client = "<< m_client << endl
        << "m_labelbuttons.size = " << m_labelbuttons.size() << endl;

    if (moving)
        stopMoving(true);
    if (resizing)
        stopResizing(true);
    if (m_attaching_tab)
        attachTo(0, 0, true);

    // no longer a valid window to do stuff with
    Fluxbox::instance()->removeWindowSearchGroup(frame().window().window());
    Fluxbox::instance()->removeWindowSearchGroup(frame().tabcontainer().window());
    Fluxbox::instance()->shortcutManager().removeWindow(this);

    Client2ButtonMap::iterator it = m_labelbuttons.begin();
    Client2ButtonMap::iterator it_end = m_labelbuttons.end();
    for (; it != it_end; ++it)
        frame().removeTab((*it).second);

    m_labelbuttons.clear();

    m_timer.stop();
    m_tabActivationTimer.stop();

    // notify die
    dieSig().emit(*this);

    if (m_client != 0 && !m_screen.isShuttingdown())
        delete m_client; // this also removes client from our list
    m_client = 0;

    if (m_clientlist.size() > 1) {
        fbdbg<<"(~FluxboxWindow()) WARNING! clientlist > 1"<<endl;
        while (!m_clientlist.empty()) {
            detachClient(*m_clientlist.back());
        }
    }

    if (!screen().isShuttingdown())
        screen().focusControl().removeWindow(*this);


    fbdbg<<"~FluxboxWindow("<<this<<")"<<endl;
}


void FluxboxWindow::init() {
    m_attaching_tab = 0;

    // fetch client size and placement
    XWindowAttributes wattrib;
    if (! m_client->getAttrib(wattrib) ||
        !wattrib.screen  || // no screen? ??
        wattrib.override_redirect || // override redirect
        m_client->initial_state == WithdrawnState ||
        m_client->getWMClassClass() == "DockApp") { // Slit client
        return;
    }

    if (m_client->initial_state == IconicState)
        m_state.iconic = true;

    m_client->setFluxboxWindow(this);
    m_client->setGroupLeftWindow(None); // nothing to the left.

    if (Fluxbox::instance()->haveShape())

        Shape::setShapeNotify(winClient());

    //!! TODO init of client should be better
    // we don't want to duplicate code here and in attachClient
    m_clientlist.push_back(m_client);

    fbdbg<<"FluxboxWindow::init(this="<<this<<", client="<<hex<<
        m_client->window()<<", frame = "<<frame().window().window()<<dec<<")"<<endl;

    Fluxbox &fluxbox = *Fluxbox::instance();

    associateClient(*m_client);

    frame().setFocusTitle(title());

    // redirect events from frame to us
    frame().setEventHandler(*this);
    fluxbox.saveWindowSearchGroup(frame().window().window(), this);
    fluxbox.saveWindowSearchGroup(frame().tabcontainer().window(), this);

    m_workspace_number = m_screen.currentWorkspaceID();

    // set default decorations but don't apply them
    setDecorationMask(WindowState::getDecoMaskFromString(screen().defaultDeco()),
                      false);

    functions.resize = functions.move = functions.iconify = functions.maximize
    = functions.close = functions.tabable = true;

    updateMWMHintsFromClient(*m_client);

    m_timer.setTimeout(fluxbox.getAutoRaiseDelay() * FbTk::FbTime::IN_MILLISECONDS);
    FbTk::RefCount<FbTk::Command<void> > raise_cmd(new FbTk::SimpleCommand<FluxboxWindow>(*this,
                                                                                   &FluxboxWindow::raise));
    m_timer.setCommand(raise_cmd);
    m_timer.fireOnce(true);

    m_tabActivationTimer.setTimeout(fluxbox.getAutoRaiseDelay() * FbTk::FbTime::IN_MILLISECONDS);
    FbTk::RefCount<ActivateTabCmd> activate_tab_cmd(new ActivateTabCmd());
    m_tabActivationTimer.setCommand(activate_tab_cmd);
    m_tabActivationTimer.fireOnce(true);

    m_resizeTimer.setTimeout(screen().opaqueResizeDelay() * FbTk::FbTime::IN_MILLISECONDS);
    FbTk::RefCount<FbTk::Command<void> > resize_cmd(new FbTk::SimpleCommand<FluxboxWindow>(*this,
                                                                                   &FluxboxWindow::updateResize));
    m_resizeTimer.setCommand(resize_cmd);
    m_resizeTimer.fireOnce(true);

    m_reposLabels_timer.setTimeout(IconButton::updateLaziness());
    m_reposLabels_timer.fireOnce(true);
    FbTk::RefCount<FbTk::Command<void> > elrs(new FbTk::SimpleCommand<FluxboxWindow>(*this, &FluxboxWindow::emitLabelReposSig));
    m_reposLabels_timer.setCommand(elrs);

    /**************************************************/
    /* Read state above here, apply state below here. */
    /**************************************************/

    if (m_client->isTransient() && m_client->transientFor()->fbwindow())
        m_state.stuck = m_client->transientFor()->fbwindow()->isStuck();

    if (!m_client->sizeHints().isResizable()) {
        functions.resize = functions.maximize = false;
        decorations.tab = false; //no tab for this window
    }

    associateClientWindow();

    setWindowType(m_client->getWindowType());

    auto sanitizePosition = [&]() {
        int head = screen().getHead(fbWindow());
        if (head == 0 && screen().hasXinerama())
            head = screen().getCurrHead();
        int left = screen().maxLeft(head),   top  = screen().maxTop(head),
            btm  = screen().maxBottom(head), rght = screen().maxRight(head);
        const int margin = hasTitlebar() ? 32 : 8;
        // ensure the window intersects with the workspace x-axis
        if (int(frame().x() + frame().width()) < left) {
            left += margin - frame().width();
        } else if (frame().x() > rght) {
            left = rght - margin;
        } else {
            left = frame().x();
        }
        if (hasTitlebar()) {
            // ensure the titlebar is inside the workspace
            top  = std::max(top,  std::min(frame().y(), btm  - margin));
        } else {
            // ensure "something" is inside the workspace
            if (int(frame().y() + frame().height()) < top)
                top += margin - frame().height();
            else if (frame().y() > btm)
                top = btm - margin;
            else
                top = frame().y();
        }
        frame().move(left, top);
    };

    if (fluxbox.isStartup()) {
        m_placed = true;
    } else if (m_client->normal_hint_flags & (PPosition|USPosition)) {
        m_placed = true;
        sanitizePosition();
    } else {
        if (!isWindowVisibleOnSomeHeadOrScreen(*this)) {
            // this probably should never happen, but if a window
            // unexplicitly has its topleft corner outside any screen,
            // move it to the current screen and ensure it's just placed
            int cur = screen().getHead(fbWindow());
            move(screen().getHeadX(cur), screen().getHeadY(cur));
            m_placed = false; // allow placement strategy to fix position
        }
        setOnHead(screen().getCurrHead());
    }

    // we must do this now, or else resizing may not work properly
    applyDecorations();

    fluxbox.attachSignals(*this);

    if (!m_state.fullscreen) {
        unsigned int new_width = 0, new_height = 0;
        if (m_client->width() >= screen().width()) {
            m_state.maximized |= WindowState::MAX_HORZ;
            new_width = 2 * screen().width() / 3;
        }
        if (m_client->height() >= screen().height()) {
            m_state.maximized |= WindowState::MAX_VERT;
            new_height = 2 * screen().height() / 3;
        }
        if (new_width || new_height) {
            const int maximized = m_state.maximized;
            m_state.maximized = WindowState::MAX_NONE;
            resize(new_width ? new_width : width(), new_height ? new_height : height());
            m_placed = false;
            m_state.maximized = maximized;
        }
    }

    // this window is managed, we are now allowed to modify actual state
    m_initialized = true;

    if (m_workspace_number >= screen().numberOfWorkspaces())
        m_workspace_number = screen().currentWorkspaceID();

    unsigned int real_width = frame().width();
    unsigned int real_height = frame().height();
    frame().applySizeHints(real_width, real_height);

    // if we're a transient then we should be on the same layer and workspace
    FluxboxWindow* twin = m_client->transientFor() ? m_client->transientFor()->fbwindow() : 0;
    if (twin && twin != this) {
        if (twin->layerNum() < ResourceLayer::DESKTOP) { // don't confine layer for desktops
            layerItem().setLayer(twin->layerItem().getLayer());
            m_state.layernum = twin->layerNum();
        }
        m_workspace_number = twin->workspaceNumber();
        const int x = twin->frame().x() + int(twin->frame().width() - frame().width())/2;
        const int y = twin->frame().y() + int(twin->frame().height() - frame().height())/2;
        frame().move(x, y);
        sanitizePosition();
        m_placed = true;
    } else // if no parent then set default layer
        moveToLayer(m_state.layernum, m_state.layernum != ::ResourceLayer::NORMAL);

    fbdbg<<"FluxboxWindow::init("<<title().logical()<<") transientFor: "<<
       m_client->transientFor()<<endl;
    if (twin) {
        fbdbg<<"FluxboxWindow::init("<<title().logical()<<") transientFor->title(): "<<
           twin->title().logical()<<endl;
    }

    screen().getWorkspace(m_workspace_number)->addWindow(*this);
    if (m_placed)
        moveResize(frame().x(), frame().y(), real_width, real_height);
    else
        placeWindow(getOnHead());

    setFocusFlag(false); // update graphics before mapping

    if (m_state.stuck) {
        m_state.stuck = false;
        stick();
    }

    if (m_state.shaded) { // start shaded
        m_state.shaded = false;
        shade();
    }

    if (m_state.iconic) {
        m_state.iconic = false;
        iconify();
    } else if (m_workspace_number == screen().currentWorkspaceID()) {
        m_state.iconic = true;
        deiconify(false);
        // check if we should prevent this window from gaining focus
        m_focused = false; // deiconify sets this
        if (!Fluxbox::instance()->isStartup() && isFocusNew()) {
            Focus::Protection fp = m_focus_protection;
            m_focus_protection &= ~Focus::Deny; // new windows run as "Refuse"
            m_focused = focusRequestFromClient(*m_client);
            m_focus_protection = fp;
            if (!m_focused)
                lower();
        }
    }

    if (m_state.fullscreen) {
        m_state.fullscreen = false;
        setFullscreen(true);
    }

    if (m_state.maximized) {
        int tmp = m_state.maximized;
        m_state.maximized = WindowState::MAX_NONE;
        setMaximizedState(tmp);
    }

    m_workspacesig.emit(*this);
    m_creation_time = FbTk::FbTime::mono();
    frame().frameExtentSig().emit();
    setupWindow();
    fluxbox.sync(false);

}

/// attach a client to this window and destroy old window
void FluxboxWindow::attachClient(WinClient &client, int x, int y) {
    //!! TODO: check for isGroupable in client
    if (client.fbwindow() == this)
        return;

    menu().hide();

    // reparent client win to this frame
    frame().setClientWindow(client);
    bool was_focused = false;
    WinClient *focused_win = 0;

    // get the current window on the end of our client list
    Window leftwin = None;
    if (!clientList().empty())
        leftwin = clientList().back()->window();

    client.setGroupLeftWindow(leftwin);

    if (client.fbwindow() != 0) {
        FluxboxWindow *old_win = client.fbwindow(); // store old window

        if (FocusControl::focusedFbWindow() == old_win)
            was_focused = true;

        ClientList::iterator client_insert_pos = getClientInsertPosition(x, y);
        FbTk::TextButton *button_insert_pos = NULL;
        if (client_insert_pos != m_clientlist.end())
            button_insert_pos = m_labelbuttons[*client_insert_pos];

        // make sure we set new window search for each client
        ClientList::iterator client_it = old_win->clientList().begin();
        ClientList::iterator client_it_end = old_win->clientList().end();
        for (; client_it != client_it_end; ++client_it) {
            // reparent window to this
            frame().setClientWindow(**client_it);

            moveResizeClient(**client_it);

            // create a labelbutton for this client and
            // associate it with the pointer
            associateClient(*(*client_it));

            //null if we want the new button at the end of the list
            if (x >= 0 && button_insert_pos)
                frame().moveLabelButtonLeftOf(*m_labelbuttons[*client_it], *button_insert_pos);
        }

        // add client and move over all attached clients
        // from the old window to this list
        m_clientlist.splice(client_insert_pos, old_win->m_clientlist);
        updateClientLeftWindow();
        old_win->m_client = 0;

        delete old_win;

    } else { // client.fbwindow() == 0

        associateClient(client);
        moveResizeClient(client);

        // right now, this block only happens with new windows or on restart
        bool is_startup = Fluxbox::instance()->isStartup();

        // we use m_focused as a signal to focus the window when mapped
        if (isFocusNew() && !is_startup)
            m_focused = focusRequestFromClient(client);
        focused_win = (isFocusNew() || is_startup) ? &client : m_client;

        m_clientlist.push_back(&client);
    }

    // make sure that the state etc etc is updated for the new client
    // TODO: one day these should probably be neatened to only act on the
    // affected clients if possible
    m_statesig.emit(*this);
    m_workspacesig.emit(*this);
    m_layersig.emit(*this);

    if (was_focused) {
        // don't ask me why, but client doesn't seem to keep focus in new window
        // and we don't seem to get a FocusIn event from setInputFocus
        client.focus();
        FocusControl::setFocusedWindow(&client);
    } else {
        if (!focused_win)
            focused_win = screen().focusControl().lastFocusedWindow(*this);
        if (focused_win) {
            setCurrentClient(*focused_win, false);
            if (isIconic() && m_focused)
                deiconify();
        }
    }
    frame().reconfigure();
}


/// detach client from window and create a new window for it
bool FluxboxWindow::detachClient(WinClient &client) {
    if (client.fbwindow() != this || numClients() <= 1)
        return false;

    Window leftwin = None;
    ClientList::iterator client_it, client_it_after;
    client_it = client_it_after =
        find(clientList().begin(), clientList().end(), &client);

    if (client_it != clientList().begin())
        leftwin = (*(--client_it))->window();

    if (++client_it_after != clientList().end())
        (*client_it_after)->setGroupLeftWindow(leftwin);

    removeClient(client);
    screen().createWindow(client);
    return true;
}

void FluxboxWindow::detachCurrentClient() {
    // should only operate if we had more than one client
    if (numClients() <= 1)
        return;
    WinClient &client = *m_client;
    detachClient(*m_client);
    if (client.fbwindow() != 0)
        client.fbwindow()->show();
}

/// removes client from client list, does not create new fluxboxwindow for it
bool FluxboxWindow::removeClient(WinClient &client) {
    if (client.fbwindow() != this || numClients() == 0)
        return false;


    fbdbg<<"("<<__FUNCTION__<<")["<<this<<"]"<<endl;


    // if it is our active client, deal with it...
    if (m_client == &client) {
        WinClient *next_client = screen().focusControl().lastFocusedWindow(*this, m_client);
        if (next_client != 0)
            setCurrentClient(*next_client, false);
    }

    menu().hide();

    m_clientlist.remove(&client);

    if (m_client == &client) {
        if (m_clientlist.empty())
            m_client = 0;
        else
            // this really shouldn't happen
            m_client = m_clientlist.back();
    }

    FbTk::EventManager &evm = *FbTk::EventManager::instance();
    evm.remove(client.window());

    IconButton *label_btn = m_labelbuttons[&client];
    if (label_btn != 0) {
        frame().removeTab(label_btn);
        label_btn = 0;
    }

    m_labelbuttons.erase(&client);
    frame().reconfigure();
    updateClientLeftWindow();

    fbdbg<<"("<<__FUNCTION__<<")["<<this<<"] numClients = "<<numClients()<<endl;

    return true;
}

/// returns WinClient of window we're searching for
WinClient *FluxboxWindow::findClient(Window win) {
    ClientList::iterator it = find_if(clientList().begin(),
                                      clientList().end(),
                                      Compose(std::bind(equal_to<Window>(), _1, win),
                                              mem_fn(&WinClient::window)));
    return (it == clientList().end() ? 0 : *it);
}

/// raise and focus next client
void FluxboxWindow::nextClient() {
    if (numClients() <= 1)
        return;

    ClientList::iterator it = find(m_clientlist.begin(), m_clientlist.end(),
                                   m_client);
    if (it == m_clientlist.end())
        return;

    ++it;
    if (it == m_clientlist.end())
        it = m_clientlist.begin();

    setCurrentClient(**it, isFocused());
}

void FluxboxWindow::prevClient() {
    if (numClients() <= 1)
        return;

    ClientList::iterator it = find(m_clientlist.begin(), m_clientlist.end(),
                                   m_client);
    if (it == m_clientlist.end())
        return;

    if (it == m_clientlist.begin())
        it = m_clientlist.end();
    --it;

    setCurrentClient(**it, isFocused());
}


void FluxboxWindow::moveClientLeft() {
    if (m_clientlist.size() == 1 ||
        *m_clientlist.begin() == &winClient())
        return;

    // move client in clientlist to the left
    ClientList::iterator oldpos = find(m_clientlist.begin(), m_clientlist.end(), &winClient());
    ClientList::iterator newpos = oldpos; newpos--;
    swap(*newpos, *oldpos);
    frame().moveLabelButtonLeft(*m_labelbuttons[&winClient()]);

    updateClientLeftWindow();

}

void FluxboxWindow::moveClientRight() {
    if (m_clientlist.size() == 1 ||
        *m_clientlist.rbegin() == &winClient())
        return;

    ClientList::iterator oldpos = find(m_clientlist.begin(), m_clientlist.end(), &winClient());
    ClientList::iterator newpos = oldpos; newpos++;
    swap(*newpos, *oldpos);
    frame().moveLabelButtonRight(*m_labelbuttons[&winClient()]);

    updateClientLeftWindow();
}

FluxboxWindow::ClientList::iterator FluxboxWindow::getClientInsertPosition(int x, int y) {

    int dest_x = 0, dest_y = 0;
    Window labelbutton = 0;
    if (!XTranslateCoordinates(FbTk::App::instance()->display(),
                               parent().window(), frame().tabcontainer().window(),
                               x, y, &dest_x, &dest_y,
                               &labelbutton))
        return m_clientlist.end();

    WinClient* c = winClientOfLabelButtonWindow(labelbutton);

    // label button not found
    if (!c)
        return m_clientlist.end();

    Window child_return=0;
    // make x and y relative to our labelbutton
    if (!XTranslateCoordinates(FbTk::App::instance()->display(),
                               frame().tabcontainer().window(), labelbutton,
                               dest_x, dest_y, &x, &y,
                               &child_return))
        return m_clientlist.end();

    ClientList::iterator client = find(m_clientlist.begin(),
                                       m_clientlist.end(),
                                       c);
    if (x > static_cast<signed>(m_labelbuttons[c]->width()) / 2)
        client++;

    return client;

}



void FluxboxWindow::moveClientTo(WinClient &win, int x, int y) {
    int dest_x = 0, dest_y = 0;
    Window labelbutton = 0;
    if (!XTranslateCoordinates(FbTk::App::instance()->display(),
                               parent().window(), frame().tabcontainer().window(),
                               x, y, &dest_x, &dest_y,
                               &labelbutton))
        return;

    WinClient* client = winClientOfLabelButtonWindow(labelbutton);

    if (!client)
        return;

    Window child_return = 0;
    //make x and y relative to our labelbutton
    if (!XTranslateCoordinates(FbTk::App::instance()->display(),
                               frame().tabcontainer().window(), labelbutton,
                               dest_x, dest_y, &x, &y,
                               &child_return))
        return;
    if (x > static_cast<signed>(m_labelbuttons[client]->width()) / 2)
        moveClientRightOf(win, *client);
    else
        moveClientLeftOf(win, *client);

}


void FluxboxWindow::moveClientLeftOf(WinClient &win, WinClient &dest) {

        frame().moveLabelButtonLeftOf(*m_labelbuttons[&win], *m_labelbuttons[&dest]);

        ClientList::iterator it = find(m_clientlist.begin(),
                                       m_clientlist.end(),
                                       &win);
        ClientList::iterator new_pos = find(m_clientlist.begin(),
                                            m_clientlist.end(),
                                            &dest);

        // make sure we found them
        if (it == m_clientlist.end() || new_pos==m_clientlist.end())
            return;
        //moving a button to the left of itself results in no change
        if (new_pos == it)
            return;
        //remove from list
        m_clientlist.erase(it);
        //insert on the new place
        m_clientlist.insert(new_pos, &win);

        updateClientLeftWindow();
}


void FluxboxWindow::moveClientRightOf(WinClient &win, WinClient &dest) {
    frame().moveLabelButtonRightOf(*m_labelbuttons[&win], *m_labelbuttons[&dest]);

    ClientList::iterator it = find(m_clientlist.begin(),
                                   m_clientlist.end(),
                                   &win);
    ClientList::iterator new_pos = find(m_clientlist.begin(),
                                        m_clientlist.end(),
                                        &dest);

    // make sure we found them
    if (it == m_clientlist.end() || new_pos==m_clientlist.end())
        return;

    //moving a button to the right of itself results in no change
    if (new_pos == it)
        return;

    //remove from list
    m_clientlist.erase(it);
    //need to insert into the next position
    new_pos++;
    //insert on the new place
    if (new_pos == m_clientlist.end())
        m_clientlist.push_back(&win);
    else
        m_clientlist.insert(new_pos, &win);

    updateClientLeftWindow();
}

/// Update LEFT window atom on all clients.
void FluxboxWindow::updateClientLeftWindow() {
    if (clientList().empty())
        return;

    // It should just update the affected clients but that
    // would require more complex code and we're assuming
    // the user dont have alot of windows grouped so this
    // wouldn't be too time consuming and it's easier to
    // implement.
    ClientList::iterator it = clientList().begin();
    ClientList::iterator it_end = clientList().end();
    // set no left window on first tab
    (*it)->setGroupLeftWindow(0);
    WinClient *last_client = *it;
    ++it;
    for (; it != it_end; ++it) {
        (*it)->setGroupLeftWindow(last_client->window());
        last_client = *it;
    }
}

bool FluxboxWindow::setCurrentClient(WinClient &client, bool setinput) {
    // make sure it's in our list
    if (client.fbwindow() != this)
        return false;

    IconButton *button = m_labelbuttons[&client];
    // in case the window is being destroyed, but this should never happen
    if (!button)
        return false;

    if (!client.acceptsFocus())
        setinput = false; // don't try

    WinClient *old = m_client;
    m_client = &client;

    bool ret = setinput && focus();
    if (setinput && old->acceptsFocus()) {
        m_client = old;
        return ret;
    }

    m_client->raise();
    if (m_focused) {
        m_client->notifyFocusChanged();
        if (old)
            old->notifyFocusChanged();
    }

    fbdbg<<"FluxboxWindow::"<<__FUNCTION__<<": labelbutton[client] = "<<
        button<<endl;

    if (old != &client) {
        titleSig().emit(title().logical(), *this);
        frame().setFocusTitle(title());
        frame().setShapingClient(&client, false);
    }
    return ret;
}

bool FluxboxWindow::isGroupable() const {
    if (isResizable() && isMaximizable() && !winClient().isTransient())
        return true;
    return false;
}

bool FluxboxWindow::isFocusNew() const {
    if (m_focus_protection & Focus::Gain)
        return true;
    if (m_focus_protection & Focus::Refuse)
        return false;
    return screen().focusControl().focusNew();
}

void FluxboxWindow::associateClientWindow() {
    frame().setShapingClient(m_client, false);

    frame().moveResizeForClient(m_client->x(), m_client->y(),
                                m_client->width(), m_client->height(),
                                m_client->gravity(), m_client->old_bw);

    updateSizeHints();
    frame().setClientWindow(*m_client);
}

void FluxboxWindow::updateSizeHints() {
    m_size_hint = m_client->sizeHints();

    ClientList::const_iterator it = clientList().begin();
    ClientList::const_iterator it_end = clientList().end();
    for (; it != it_end; ++it) {
        if ((*it) == m_client)
            continue;

        const SizeHints &hint = (*it)->sizeHints();
        if (m_size_hint.min_width < hint.min_width)
            m_size_hint.min_width = hint.min_width;
        if (m_size_hint.max_width > hint.max_width)
            m_size_hint.max_width = hint.max_width;
        if (m_size_hint.min_height < hint.min_height)
            m_size_hint.min_height = hint.min_height;
        if (m_size_hint.max_height > hint.max_height)
            m_size_hint.max_height = hint.max_height;
        // lcm could end up a bit silly, and the situation is bad no matter what
        if (m_size_hint.width_inc < hint.width_inc)
            m_size_hint.width_inc = hint.width_inc;
        if (m_size_hint.height_inc < hint.height_inc)
            m_size_hint.height_inc = hint.height_inc;
        if (m_size_hint.base_width < hint.base_width)
            m_size_hint.base_width = hint.base_width;
        if (m_size_hint.base_height < hint.base_height)
            m_size_hint.base_height = hint.base_height;
        if (m_size_hint.min_aspect_x * hint.min_aspect_y >
            m_size_hint.min_aspect_y * hint.min_aspect_x) {
            m_size_hint.min_aspect_x = hint.min_aspect_x;
            m_size_hint.min_aspect_y = hint.min_aspect_y;
        }
        if (m_size_hint.max_aspect_x * hint.max_aspect_y >
            m_size_hint.max_aspect_y * hint.max_aspect_x) {
            m_size_hint.max_aspect_x = hint.max_aspect_x;
            m_size_hint.max_aspect_y = hint.max_aspect_y;
        }
    }
    frame().setSizeHints(m_size_hint);
}

void FluxboxWindow::grabButtons() {

    // needed for click to focus
    XGrabButton(display, Button1, AnyModifier,
                frame().window().window(), True, ButtonPressMask,
                GrabModeSync, GrabModeSync, None, None);
    XUngrabButton(display, Button1, Mod1Mask|Mod2Mask|Mod3Mask,
                  frame().window().window());
}


void FluxboxWindow::reconfigure() {

    applyDecorations();
    setFocusFlag(m_focused);
    moveResize(frame().x(), frame().y(), frame().width(), frame().height());
    m_timer.setTimeout(Fluxbox::instance()->getAutoRaiseDelay() * FbTk::FbTime::IN_MILLISECONDS);
    m_tabActivationTimer.setTimeout(Fluxbox::instance()->getAutoRaiseDelay() * FbTk::FbTime::IN_MILLISECONDS);
    updateButtons();
    frame().reconfigure();
    menu().reconfigure();

    Client2ButtonMap::iterator it = m_labelbuttons.begin(),
                               it_end = m_labelbuttons.end();
    for (; it != it_end; ++it)
        it->second->setPixmap(screen().getTabsUsePixmap());

}

void FluxboxWindow::updateMWMHintsFromClient(WinClient &client) {
    const WinClient::MwmHints *hint = client.getMwmHint();

    if (hint && !m_toggled_decos && hint->flags & MwmHintsDecorations) {
        if (hint->decorations & MwmDecorAll) {
            decorations.titlebar = decorations.handle = decorations.border =
                decorations.iconify = decorations.maximize =
                decorations.menu = true;
        } else {
            decorations.titlebar = decorations.handle = decorations.border =
                decorations.iconify = decorations.maximize =
                decorations.tab = false;
            decorations.menu = true;
            if (hint->decorations & MwmDecorBorder)
                decorations.border = true;
            if (hint->decorations & MwmDecorHandle)
                decorations.handle = true;
            if (hint->decorations & MwmDecorTitle) {
                //only tab on windows with titlebar
                decorations.titlebar = decorations.tab = true;
            }
            if (hint->decorations & MwmDecorMenu)
                decorations.menu = true;
            if (hint->decorations & MwmDecorIconify)
                decorations.iconify = true;
            if (hint->decorations & MwmDecorMaximize)
                decorations.maximize = true;
        }
    } else {
        decorations.titlebar = decorations.handle = decorations.border =
        decorations.iconify = decorations.maximize = decorations.menu = true;
    }

    unsigned int mask = decorationMask();
    mask &= WindowState::getDecoMaskFromString(screen().defaultDeco());
    setDecorationMask(mask, false);

    // functions.tabable is ours, not special one
    // note that it means this window is "tabbable"
    if (hint && hint->flags & MwmHintsFunctions) {
        if (hint->functions & MwmFuncAll) {
            functions.resize = functions.move = functions.iconify =
                functions.maximize = functions.close = true;
        } else {
            functions.resize = functions.move = functions.iconify =
                functions.maximize = functions.close = false;

            if (hint->functions & MwmFuncResize)
                functions.resize = true;
            if (hint->functions & MwmFuncMove)
                functions.move = true;
            if (hint->functions & MwmFuncIconify)
                functions.iconify = true;
            if (hint->functions & MwmFuncMaximize)
                functions.maximize = true;
            if (hint->functions & MwmFuncClose)
                functions.close = true;
        }
    } else {
        functions.resize = functions.move = functions.iconify =
        functions.maximize = functions.close = true;
    }
}

void FluxboxWindow::updateFunctions() {
    if (!m_client)
        return;
    bool changed = false;
    if (m_client->isClosable() != functions.close) {
        functions.close = m_client->isClosable();
        changed = true;
    }

    if (changed)
        setupWindow();
}




void FluxboxWindow::setTitle(const std::string& title, Focusable &client) {
    // only update focus title for current client
    if (&client != m_client) {
        return;
    }

    frame().setFocusTitle(title);
    // relay title to others that display the focus title
    titleSig().emit(title, *this);
    m_reposLabels_timer.start();
}

void FluxboxWindow::emitLabelReposSig() {
    frame().tabcontainer().repositionItems();
}

void FluxboxWindow::frameExtentChanged() {
    if (m_initialized) {
        Fluxbox::instance()->updateFrameExtents(*this);
        sendConfigureNotify();
    }
}

void FluxboxWindow::themeReconfigured() {
    frame().applyDecorations();
    sendConfigureNotify();
}

void FluxboxWindow::workspaceAreaChanged(BScreen &screen) {
    frame().applyState();
}


void FluxboxWindow::restore(WinClient *client, bool remap) {
    if (client->fbwindow() != this)
        return;

    XChangeSaveSet(display, client->window(), SetModeDelete);
    client->setEventMask(NoEventMask);

    int wx = frame().x(), wy = frame().y();
    // don't move the frame, in case there are other tabs in it
    // just set the new coordinates on the reparented window
    frame().gravityTranslate(wx, wy, -client->gravity(), client->old_bw, false); // negative to invert

    // Why was this hide done? It broke vncviewer (and mplayer?),
    // since it would reparent when going fullscreen.
    // is it needed for anything? Reparent should imply unmap
    // ok, it should hide sometimes, e.g. if the reparent was sent by a client
    //client->hide();

    // restore old border width
    client->setBorderWidth(client->old_bw);

    XEvent xev;
    if (! XCheckTypedWindowEvent(display, client->window(), ReparentNotify,
                                 &xev)) {

        fbdbg<<"FluxboxWindow::restore: reparent 0x"<<hex<<client->window()<<dec<<" to root"<<endl;

        // reparent to root window
        client->reparent(screen().rootWindow(), wx, wy, false);

        if (!remap)
            client->hide();
    }

    if (remap)
        client->show();

    installColormap(false);

    delete client;


    fbdbg<<"FluxboxWindow::restore: remap = "<<remap<<endl;
    fbdbg<<"("<<__FUNCTION__<<"): numClients() = "<<numClients()<<endl;

    if (numClients() == 0)
        delete this;

}

void FluxboxWindow::restore(bool remap) {
    if (numClients() == 0)
        return;

    fbdbg<<"restore("<<remap<<")"<<endl;

    while (!clientList().empty()) {
        restore(clientList().back(), remap);
        // deleting winClient removes it from the clientList
    }
}

bool FluxboxWindow::isVisible() const {
    return frame().isVisible();
}

FbTk::FbWindow &FluxboxWindow::fbWindow() {
    return frame().window();
}

const FbTk::FbWindow &FluxboxWindow::fbWindow() const {
    return frame().window();
}

FbMenu &FluxboxWindow::menu() {
    return screen().windowMenu();
}

bool FluxboxWindow::acceptsFocus() const {
    return (m_client ? m_client->acceptsFocus() : false);
}

bool FluxboxWindow::isModal() const {
    return (m_client ? m_client->isModal() : true);
}

const FbTk::PixmapWithMask &FluxboxWindow::icon() const {
    return (m_client ? m_client->icon() : m_icon);
}

const FbMenu &FluxboxWindow::menu() const {
    return screen().windowMenu();
}

unsigned int FluxboxWindow::titlebarHeight() const {
    return frame().titlebarHeight();
}

Window FluxboxWindow::clientWindow() const  {
    if (m_client == 0)
        return 0;
    return m_client->window();
}


const FbTk::BiDiString& FluxboxWindow::title() const {
    return (m_client ? m_client->title() : m_title);
}

const FbTk::FbString& FluxboxWindow::getWMClassName() const {
    return (m_client ? m_client->getWMClassName() : getWMClassName());
}

const FbTk::FbString& FluxboxWindow::getWMClassClass() const {
    return (m_client ? m_client->getWMClassClass() : getWMClassClass());
}

FbTk::FbString FluxboxWindow::getWMRole() const {
    return (m_client ? m_client->getWMRole() : "FluxboxWindow");
}

long FluxboxWindow::getCardinalProperty(Atom prop,bool*exists) const {
    return (m_client ? m_client->getCardinalProperty(prop,exists) : Focusable::getCardinalProperty(prop,exists));
}

FbTk::FbString FluxboxWindow::getTextProperty(Atom prop,bool*exists) const {
    return (m_client ? m_client->getTextProperty(prop,exists) : Focusable::getTextProperty(prop,exists));
}

bool FluxboxWindow::isTransient() const {
    return (m_client && m_client->isTransient());
}

int FluxboxWindow::initialState() const { return m_client->initial_state; }

void FluxboxWindow::fixSize() {

    // m_last_resize_w / m_last_resize_h could be negative
    // due to user interactions. check here and limit
    unsigned int w = 1;
    unsigned int h = 1;
    if (m_last_resize_w > 0)
        w = m_last_resize_w;
    if (m_last_resize_h > 0)
        h = m_last_resize_h;

    frame().applySizeHints(w, h);

    m_last_resize_w = w;
    m_last_resize_h = h;

    // move X if necessary
    if (m_resize_corner == LEFTTOP || m_resize_corner == LEFTBOTTOM ||
        m_resize_corner == LEFT) {
        m_last_resize_x = frame().x() + frame().width() - m_last_resize_w;
    }

    if (m_resize_corner == LEFTTOP || m_resize_corner == RIGHTTOP ||
        m_resize_corner == TOP) {
        m_last_resize_y = frame().y() + frame().height() - m_last_resize_h;
    }
}

void FluxboxWindow::moveResizeClient(WinClient &client) {
    client.moveResize(frame().clientArea().x(), frame().clientArea().y(),
                      frame().clientArea().width(),
                      frame().clientArea().height());
    client.sendConfigureNotify(frame().x() + frame().clientArea().x() +
                                             frame().window().borderWidth(),
                               frame().y() + frame().clientArea().y() +
                                             frame().window().borderWidth(),
                               frame().clientArea().width(),
                               frame().clientArea().height());
}

void FluxboxWindow::sendConfigureNotify() {
    ClientList::iterator client_it = m_clientlist.begin();
    ClientList::iterator client_it_end = m_clientlist.end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient &client = *(*client_it);
        /*
          Send event telling where the root position
          of the client window is. (ie frame pos + client pos inside the frame = send pos)
        */
        //!!
        moveResizeClient(client);

    } // end for

}


void FluxboxWindow::close() {
    if (WindowCmd<void>::window() == this && WindowCmd<void>::client())
        WindowCmd<void>::client()->sendClose(false);
    else if (m_client)
        m_client->sendClose(false);
}

void FluxboxWindow::kill() {
    if (WindowCmd<void>::window() == this && WindowCmd<void>::client())
        WindowCmd<void>::client()->sendClose(true);
    else if (m_client)
        m_client->sendClose(true);
}



void FluxboxWindow::associateClient(WinClient &client) {
    IconButton *btn = new IconButton(frame().tabcontainer(),
            frame().theme().focusedTheme()->iconbarTheme(),
            frame().theme().unfocusedTheme()->iconbarTheme(), client);
    frame().createTab(*btn);

    btn->setTextPadding(Fluxbox::instance()->getTabsPadding());
    btn->setPixmap(screen().getTabsUsePixmap());

    m_labelbuttons[&client] = btn;

    FbTk::EventManager &evm = *FbTk::EventManager::instance();

    evm.add(*this, btn->window()); // we take care of button events for this
    evm.add(*this, client.window());

    client.setFluxboxWindow(this);
    join(client.titleSig(),
         FbTk::MemFun(*this, &FluxboxWindow::setTitle));
}


void FluxboxWindow::setWindowType(WindowState::WindowType type) {
    m_state.type = type;
    switch (type) {
    case WindowState::TYPE_DOCK:
        /* From Extended Window Manager Hints, draft 1.3:
         *
         * _NET_WM_WINDOW_TYPE_DOCK indicates a dock or panel feature.
         * Typically a Window Manager would keep such windows on top
         * of all other windows.
         *
         */
        setFocusHidden(true);
        setIconHidden(true);
        setFocusNew(false);
        setMouseFocus(false);
        setClickFocus(false);
        setDecorationMask(WindowState::DECOR_NONE);
        moveToLayer(::ResourceLayer::DOCK);
        break;
    case WindowState::TYPE_DESKTOP:
        /*
         * _NET_WM_WINDOW_TYPE_DESKTOP indicates a "false desktop" window
         * We let it be the size it wants, but it gets no decoration,
         * is hidden in the toolbar and window cycling list, plus
         * windows don't tab with it and is right on the bottom.
         */
        setFocusHidden(true);
        setIconHidden(true);
        setFocusNew(false);
        setMouseFocus(false);
        moveToLayer(::ResourceLayer::DESKTOP);
        setDecorationMask(WindowState::DECOR_NONE);
        setTabable(false);
        setMovable(false);
        setResizable(false);
        setStuck(true);
        break;
    case WindowState::TYPE_SPLASH:
        /*
         * _NET_WM_WINDOW_TYPE_SPLASH indicates that the
         * window is a splash screen displayed as an application
         * is starting up.
         */
        setDecorationMask(WindowState::DECOR_NONE);
        setFocusHidden(true);
        setIconHidden(true);
        setFocusNew(false);
        setMouseFocus(false);
        setClickFocus(false);
        setMovable(false);
        break;
    case WindowState::TYPE_DIALOG:
        setTabable(false);
        break;
    case WindowState::TYPE_MENU:
    case WindowState::TYPE_TOOLBAR:
        /*
         * _NET_WM_WINDOW_TYPE_TOOLBAR and _NET_WM_WINDOW_TYPE_MENU
         * indicate toolbar and pinnable menu windows, respectively
         * (i.e. toolbars and menus "torn off" from the main
         * application). Windows of this type may set the
         * WM_TRANSIENT_FOR hint indicating the main application window.
         */
        setDecorationMask(WindowState::DECOR_TOOL);
        setIconHidden(true);
        moveToLayer(::ResourceLayer::ABOVE_DOCK);
        break;
    case WindowState::TYPE_NORMAL:
    default:
        break;
    }

    /*
     * NOT YET IMPLEMENTED:
     *   _NET_WM_WINDOW_TYPE_UTILITY
     */
}

void FluxboxWindow::focusedWindowChanged(BScreen &screen,
                                         FluxboxWindow *focused_win, WinClient* client) {
    if (focused_win) {
        setFullscreenLayer();
    }
}
