// WindowEvents.cc for Fluxbox Window Manager
// Copyright (c) 2001 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//
// WindowEvents.cc for Blackbox - an X11 Window manager
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

// X event scanner for enter/leave notifies - adapted from twm
typedef struct scanargs {
    Window w;
    Bool leave, inferior, enter;
} scanargs;

// look for valid enter or leave events (that may invalidate the earlier one we are interested in)
extern "C" int queueScanner(Display *, XEvent *e, char *args) {
    if (e->type == LeaveNotify &&
        e->xcrossing.window == ((scanargs *) args)->w &&
        e->xcrossing.mode == NotifyNormal) {
        ((scanargs *) args)->leave = true;
        ((scanargs *) args)->inferior = (e->xcrossing.detail == NotifyInferior);
    } else if (e->type == EnterNotify &&
               e->xcrossing.mode == NotifyUngrab)
        ((scanargs *) args)->enter = true;

    return false;
}

// used in FluxboxWindow::updateAll() and FluxboxWindow::Setu
typedef FbTk::Resource<vector<WinButton::Type> > WBR;

// create a button ready to be used in the frame-titlebar
WinButton* makeButton(FluxboxWindow& win, FocusableTheme<WinButtonTheme>& btheme, WinButton::Type btype) {

    FbTk::ThemeProxy<WinButtonTheme>& theme = win.screen().pressedWinButtonTheme();
    FbWinFrame& frame = win.frame();
    FbTk::FbWindow& parent = frame.titlebar();
    const unsigned int h = frame.buttonHeight();
    const unsigned int w = h;

    return new WinButton(win, btheme, theme, btype, parent, 0, 0, w, h);
}

class ChangeProperty {
public:
    ChangeProperty(Display *disp, Atom prop, int mode,
                   unsigned char *state, int num):m_disp(disp),
                                                  m_prop(prop),
                                                  m_state(state),
                                                  m_num(num),
                                                  m_mode(mode){

    }
    void operator () (FbTk::FbWindow *win) {
        XChangeProperty(m_disp, win->window(), m_prop, m_prop, 32, m_mode,
                        m_state, m_num);
    }
private:
    Display *m_disp;
    Atom m_prop;
    unsigned char *m_state;
    int m_num;
    int m_mode;
};

} // namespace

/**
 Sets state on each client in our list
 Use setting_up for setting startup state - it may not be committed yet
 That'll happen when its mapped
 */
void FluxboxWindow::setState(unsigned long new_state, bool setting_up) {
    m_current_state = new_state;
    if (numClients() == 0 || setting_up)
        return;

    unsigned long state[2];
    state[0] = (unsigned long) m_current_state;
    state[1] = (unsigned long) None;

    for_each(m_clientlist.begin(), m_clientlist.end(),
             ChangeProperty(display,
                                  FbAtoms::instance()->getWMStateAtom(),
                                  PropModeReplace,
                                  (unsigned char *)state, 2));

    ClientList::iterator it = clientList().begin();
    ClientList::iterator it_end = clientList().end();
    for (; it != it_end; ++it) {
        (*it)->setEventMask(NoEventMask);
        if (new_state == IconicState)
            (*it)->hide();
        else if (new_state == NormalState)
            (*it)->show();
        (*it)->setEventMask(PropertyChangeMask | StructureNotifyMask | FocusChangeMask | KeyPressMask);
    }
}

bool FluxboxWindow::getState() {

    Atom atom_return;
    bool ret = false;
    int foo;
    unsigned long *state, ulfoo, nitems;
    if (!m_client->property(FbAtoms::instance()->getWMStateAtom(),
                             0l, 2l, false, FbAtoms::instance()->getWMStateAtom(),
                             &atom_return, &foo, &nitems, &ulfoo,
                             (unsigned char **) &state) || !state)
        return false;

    if (nitems >= 1) {
        m_current_state = static_cast<unsigned long>(state[0]);
        ret = true;
    }

    XFree(static_cast<void *>(state));

    return ret;
}

/**
   Redirect any unhandled event to our handlers
*/
void FluxboxWindow::handleEvent(XEvent &event) {
    switch (event.type) {
    case ConfigureRequest:
       fbdbg<<"ConfigureRequest("<<title().logical()<<")"<<endl;

        configureRequestEvent(event.xconfigurerequest);
        break;
    case MapNotify:
        mapNotifyEvent(event.xmap);
        break;
        // This is already handled in Fluxbox::handleEvent
        // case MapRequest:
        //        mapRequestEvent(event.xmaprequest);
        //break;
    case PropertyNotify: {

#ifdef DEBUG
        char *atomname = XGetAtomName(display, event.xproperty.atom);
        fbdbg<<"PropertyNotify("<<title().logical()<<"), property = "<<atomname<<endl;
        if (atomname)
            XFree(atomname);
#endif // DEBUG
        WinClient *client = findClient(event.xproperty.window);
        if (client)
            propertyNotifyEvent(*client, event.xproperty.atom);

    }
        break;

    default:
#ifdef SHAPE
        if (Fluxbox::instance()->haveShape() &&
            event.type == Fluxbox::instance()->shapeEventbase() + ShapeNotify) {

            fbdbg<<"ShapeNotify("<<title().logical()<<")"<<endl;

            XShapeEvent *shape_event = (XShapeEvent *)&event;

            if (shape_event->shaped)
                frame().setShapingClient(m_client, true);
            else
                frame().setShapingClient(0, true);

            FbTk::App::instance()->sync(false);
            break;
        }
#endif // SHAPE

        break;
    }
}

void FluxboxWindow::mapRequestEvent(XMapRequestEvent &re) {

    // we're only concerned about client window event
    WinClient *client = findClient(re.window);
    if (client == 0) {
        fbdbg<<"("<<__FUNCTION__<<"): Can't find client!"<<endl;
        return;
    }

    // Note: this function never gets called from WithdrawnState
    // initial state is handled in init()

    setCurrentClient(*client, false); // focus handled on MapNotify
    deiconify();

    if (isFocusNew()) {
        m_focused = false; // deiconify sets this
        Focus::Protection fp = m_focus_protection;
        m_focus_protection &= ~Focus::Deny; // goes by "Refuse"
        m_focused = focusRequestFromClient(*client);
        m_focus_protection = fp;
        if (!m_focused)
            lower();
    }

}

bool FluxboxWindow::focusRequestFromClient(WinClient &from) {

    if (from.fbwindow() != this)
        return false;

    bool ret = true;

    FluxboxWindow *cur = FocusControl::focusedFbWindow();
    WinClient *client = FocusControl::focusedWindow();
    if ((from.fbwindow() && (from.fbwindow()->focusProtection() & Focus::Deny)) ||
        (cur && (cur->focusProtection() & Focus::Lock))) {
        ret = false;
    } else if (cur && getRootTransientFor(&from) != getRootTransientFor(client)) {
        ret = !(cur->isFullscreen() && getOnHead() == cur->getOnHead()) &&
              !cur->isTyping();
    }

    if (!ret)
        Fluxbox::instance()->attentionHandler().addAttention(from);
    return ret;

}

void FluxboxWindow::mapNotifyEvent(XMapEvent &ne) {
    WinClient *client = findClient(ne.window);
    if (!client || client != m_client)
        return;

    if (ne.override_redirect || !isVisible() || !client->validateClient())
        return;

    m_state.iconic = false;

    // setting state will cause all tabs to be mapped, but we only want the
    // original tab to be focused
    if (m_current_state != NormalState)
        setState(NormalState, false);

    // we use m_focused as a signal that this should be focused when mapped
    if (m_focused) {
        m_focused = false;
        focus();
    }

}

/**
   Unmaps frame window and client window if
   event.window == m_client->window
*/
void FluxboxWindow::unmapNotifyEvent(XUnmapEvent &ue) {
    WinClient *client = findClient(ue.window);
    if (client == 0)
        return;


    fbdbg<<"("<<__FUNCTION__<<"): 0x"<<hex<<client->window()<<dec<<endl;
    fbdbg<<"("<<__FUNCTION__<<"): title="<<client->title().logical()<<endl;

    if (numClients() == 1) // unmapping the last client
        frame().hide(); // hide this now, otherwise compositors will fade out the frame, bug #1110
    setState(WithdrawnState, false);
    restore(client, false);

}

/**
   Checks if event is for m_client->window.
   If it isn't, we leave it until the window is unmapped, if it is,
   we just hide it for now.
*/
void FluxboxWindow::destroyNotifyEvent(XDestroyWindowEvent &de) {
    if (de.window == m_client->window()) {
        fbdbg<<"DestroyNotifyEvent this="<<this<<" title = "<<title().logical()<<endl;
        delete m_client;
        if (numClients() == 0)
            delete this;
    }

}


void FluxboxWindow::propertyNotifyEvent(WinClient &client, Atom atom) {
    switch(atom) {
    case XA_WM_CLASS:
    case XA_WM_CLIENT_MACHINE:
    case XA_WM_COMMAND:
        break;

    case XA_WM_TRANSIENT_FOR: {
        bool was_transient = client.isTransient();
        client.updateTransientInfo();
        // update our layer to be the same layer as our transient for
        if (client.isTransient() && !was_transient
            && client.transientFor()->fbwindow())
            layerItem().setLayer(client.transientFor()->fbwindow()->layerItem().getLayer());

    } break;

    case XA_WM_HINTS:
        client.updateWMHints();
        titleSig().emit(title().logical(), *this);
        // nothing uses this yet
        // hintSig().emit(*this);
        break;

    case XA_WM_ICON_NAME:
        // we don't use icon title, since many apps don't update it,
        // and we don't show icons anyway
        break;
    case XA_WM_NAME:
        client.updateTitle();
        break;

    case XA_WM_NORMAL_HINTS: {
        fbdbg<<"XA_WM_NORMAL_HINTS("<<title().logical()<<")"<<endl;
        unsigned int old_max_width = client.maxWidth();
        unsigned int old_min_width = client.minWidth();
        unsigned int old_min_height = client.minHeight();
        unsigned int old_max_height = client.maxHeight();
        bool changed = false;
        client.updateWMNormalHints();
        updateSizeHints();

        if (client.minWidth() != old_min_width ||
            client.maxWidth() != old_max_width ||
            client.minHeight() != old_min_height ||
            client.maxHeight() != old_max_height) {
            if (!client.sizeHints().isResizable()) {
                if (functions.resize ||
                    functions.maximize)
                    changed = true;
                functions.resize = functions.maximize = false;
            } else {
                // TODO: is broken while handled by FbW, needs to be in WinClient
                if (!functions.maximize || !functions.resize)
                    changed = true;
                functions.maximize = functions.resize = true;
            }

            if (changed) {
                setupWindow();
                applyDecorations();
            }
       }

        moveResize(frame().x(), frame().y(),
                   frame().width(), frame().height());

        break;
    }

    default:
        FbAtoms *fbatoms = FbAtoms::instance();
        if (atom == fbatoms->getWMProtocolsAtom()) {
            client.updateWMProtocols();
        } else if (atom == fbatoms->getMWMHintsAtom()) {
            client.updateMWMHints();
            updateMWMHintsFromClient(client);
#ifdef REMEMBER
            if (!m_toggled_decos) {
                Remember::instance().updateDecoStateFromClient(client);
            }
#endif
            applyDecorations(); // update decorations (if they changed)
        }
        break;
    }

}


void FluxboxWindow::exposeEvent(XExposeEvent &ee) {
    frame().exposeEvent(ee);
}

void FluxboxWindow::configureRequestEvent(XConfigureRequestEvent &cr) {

    WinClient *client = findClient(cr.window);
    if (client == 0 || isIconic())
        return;

    int old_x = frame().x(), old_y = frame().y();
    unsigned int old_w = frame().width();
    unsigned int old_h = frame().height() - frame().titlebarHeight()
                       - frame().handleHeight();
    int cx = old_x, cy = old_y, ignore = 0;
    unsigned int cw = old_w, ch = old_h;

    // make sure the new width/height would be ok with all clients, or else they
    // could try to resize the window back and forth
    if (cr.value_mask & CWWidth || cr.value_mask & CWHeight) {
        unsigned int new_w = (cr.value_mask & CWWidth) ? cr.width : cw;
        unsigned int new_h = (cr.value_mask & CWHeight) ? cr.height : ch;
        ClientList::iterator it = clientList().begin();
        ClientList::iterator it_end = clientList().end();
        for (; it != it_end; ++it) {
            if (*it != client && !(*it)->sizeHints().valid(new_w, new_h))
                cr.value_mask = cr.value_mask & ~(CWWidth | CWHeight);
        }
    }

    // don't allow moving/resizing fullscreen or maximized windows
    if (isFullscreen() || (isMaximizedHorz() && screen().getMaxIgnoreIncrement()))
        cr.value_mask = cr.value_mask & ~(CWWidth | CWX);
    if (isFullscreen() || (isMaximizedVert() && screen().getMaxIgnoreIncrement()))
        cr.value_mask = cr.value_mask & ~(CWHeight | CWY);

#ifdef REMEMBER
    // don't let misbehaving clients (e.g. MPlayer) move/resize their windows
    // just after creation if the user has a saved position/size
    if (m_creation_time) {

        uint64_t now = FbTk::FbTime::mono();

        Remember& rinst = Remember::instance();

        if (now > (m_creation_time + FbTk::FbTime::IN_SECONDS)) {
            m_creation_time = 0;
        } else if (rinst.isRemembered(*client, Remember::REM_MAXIMIZEDSTATE) ||
                 rinst.isRemembered(*client, Remember::REM_FULLSCREENSTATE)) {
            cr.value_mask = cr.value_mask & ~(CWWidth | CWHeight);
            cr.value_mask = cr.value_mask & ~(CWX | CWY);
        } else {
            if (rinst.isRemembered(*client, Remember::REM_DIMENSIONS))
                cr.value_mask = cr.value_mask & ~(CWWidth | CWHeight);

            if (rinst.isRemembered(*client, Remember::REM_POSITION))
                cr.value_mask = cr.value_mask & ~(CWX | CWY);
        }
    }
#endif // REMEMBER

    if (cr.value_mask & CWBorderWidth)
        client->old_bw = cr.border_width;

    if ((cr.value_mask & CWX) &&
        (cr.value_mask & CWY)) {
        cx = cr.x;
        cy = cr.y;
        frame().gravityTranslate(cx, cy, client->gravity(), client->old_bw, false);
        frame().setActiveGravity(client->gravity(), client->old_bw);
    } else if (cr.value_mask & CWX) {
        cx = cr.x;
        frame().gravityTranslate(cx, ignore, client->gravity(), client->old_bw, false);
        frame().setActiveGravity(client->gravity(), client->old_bw);
    } else if (cr.value_mask & CWY) {
        cy = cr.y;
        frame().gravityTranslate(ignore, cy, client->gravity(), client->old_bw, false);
        frame().setActiveGravity(client->gravity(), client->old_bw);
    }

    if (cr.value_mask & CWWidth)
        cw = cr.width;

    if (cr.value_mask & CWHeight)
        ch = cr.height;

    // the request is for client window so we resize the frame to it first
    if (old_w != cw || old_h != ch) {
        if (old_x != cx || old_y != cy)
            frame().moveResizeForClient(cx, cy, cw, ch);
        else
            frame().resizeForClient(cw, ch);
    } else if (old_x != cx || old_y != cy) {
        frame().move(cx, cy);
    }

    if (cr.value_mask & CWStackMode) {
        switch (cr.detail) {
        case Above:
        case TopIf:
        default:
            if ((isFocused() && focusRequestFromClient(*client)) ||
                !FocusControl::focusedWindow()) {
                setCurrentClient(*client, true);
                raise();
            } else if (getRootTransientFor(client) ==
                         getRootTransientFor(FocusControl::focusedWindow())) {
                setCurrentClient(*client, false);
                raise();
            }
            break;

        case Below:
        case BottomIf:
            lower();
            break;
        }
    }

    sendConfigureNotify();

}

// keep track of last keypress in window, so we can decide not to focusNew
void FluxboxWindow::keyPressEvent(XKeyEvent &ke) {
    // if there's a modifier key down, the user probably expects the new window
    if (FbTk::KeyUtil::instance().cleanMods(ke.state))
        return;

    // we need to ignore modifier keys themselves, too
    KeySym ks;
    char keychar[1];
    XLookupString(&ke, keychar, 1, &ks, 0);
    if (IsModifierKey(ks))
        return;

    // if the key was return/enter, the user probably expects the window
    // e.g., typed the command in a terminal
    if (ks == XK_KP_Enter || ks == XK_Return) {
        // we'll actually reset the time for this one
        m_last_keypress_time = 0;
        return;
    }

    // otherwise, make a note that the user is typing
    m_last_keypress_time = FbTk::FbTime::mono();
}

bool FluxboxWindow::isTyping() const {

    uint64_t diff = FbTk::FbTime::mono() - m_last_keypress_time;
    return ((diff / 1000) < screen().noFocusWhileTypingDelay());
}

void FluxboxWindow::buttonPressEvent(XButtonEvent &be) {
    m_last_button_x = be.x_root;
    m_last_button_y = be.y_root;
    m_last_pressed_button = be.button;

    FbTk::Menu::hideShownMenu();

    Keys *k = Fluxbox::instance()->keys();
    int context = 0;
    context = frame().getContext(be.subwindow ? be.subwindow : be.window, be.x_root, be.y_root);
    if (!context && be.subwindow)
        context = frame().getContext(be.window);

    if (k->doAction(be.type, be.state, be.button, context, &winClient(), be.time)) {
        XAllowEvents(display, SyncPointer, CurrentTime);
        return;
    }

    WinClient *client = 0;
    if (!screen().focusControl().isMouseTabFocus()) {
        // determine if we're in a label button (tab)
        client = winClientOfLabelButtonWindow(be.window);
    }


    // - refeed the event into the queue so the app or titlebar subwindow gets it
    if (be.subwindow)
        XAllowEvents(display, ReplayPointer, CurrentTime);
    else
        XAllowEvents(display, SyncPointer, CurrentTime);

    // if nothing was bound via keys-file then
    // - raise() if clickRaise is enabled
    // - hide open menues
    // - focus on clickFocus
    if (frame().window().window() == be.window) {
        if (screen().clickRaises())
            raise();

        m_button_grab_x = be.x_root - frame().x() - frame().window().borderWidth();
        m_button_grab_y = be.y_root - frame().y() - frame().window().borderWidth();
    }

    if (!m_focused && acceptsFocus() && m_click_focus)
        focus();

    if (!screen().focusControl().isMouseTabFocus() &&
        client && client != m_client &&
        !screen().focusControl().isIgnored(be.x_root, be.y_root) ) {
        setCurrentClient(*client, isFocused());
    }



}

const unsigned int DEADZONE = 4;

void FluxboxWindow::buttonReleaseEvent(XButtonEvent &re) {

    if (m_last_pressed_button == static_cast<int>(re.button)) {
        m_last_pressed_button = 0;
    }

    if (isMoving())
        stopMoving();
    else if (isResizing())
        stopResizing();
    else if (m_attaching_tab)
        attachTo(re.x_root, re.y_root);
    else if (std::abs(m_last_button_x - re.x_root) + std::abs(m_last_button_y - re.y_root) < DEADZONE) {
        int context = 0;
        context = frame().getContext(re.subwindow ? re.subwindow : re.window,
                                     re.x_root, re.y_root);
        if (!context && re.subwindow)
            context = frame().getContext(re.window);

        Fluxbox::instance()->keys()->doAction(re.type, re.state, re.button,
                                              context, &winClient(), re.time);
    }
}


void FluxboxWindow::motionNotifyEvent(XMotionEvent &me) {

    if (isMoving() && me.window == parent()) {
        me.window = frame().window().window();
    }

    int context = frame().getContext(me.window,  me.x_root, me.y_root, m_last_button_x, m_last_button_y, true);

    if (Fluxbox::instance()->getIgnoreBorder() && m_attaching_tab == 0
        && !(isMoving() || isResizing())) {

        if (context & Keys::ON_WINDOWBORDER) {
            return;
        }
    }

    // in case someone put  MoveX :StartMoving etc into keys, we have
    // to activate it before doing the actual motionNotify code
    if (std::abs(m_last_button_x - me.x_root) + std::abs(m_last_button_y - me.y_root) >= DEADZONE) {
        XEvent &e = const_cast<XEvent&>(Fluxbox::instance()->lastEvent()); // is copy of "me"
        e.xmotion.x_root = m_last_button_x;
        e.xmotion.y_root = m_last_button_y;
        Fluxbox::instance()->keys()->doAction(me.type, me.state, m_last_pressed_button, context, &winClient(), me.time);
        e.xmotion.x = me.x_root;
        e.xmotion.y = me.y_root;
    }

    if (moving || m_attaching_tab) {

        XEvent e;

        if (XCheckTypedEvent(display, MotionNotify, &e)) {
            XPutBackEvent(display, &e);
            return;
        }

        const bool xor_outline = m_attaching_tab || !screen().doOpaqueMove();

        // Warp to next or previous workspace?, must have moved sideways some
        int moved_x = me.x_root - m_last_resize_x;

        // Warp to a workspace offset (if treating workspaces like a grid)
        int moved_y = me.y_root - m_last_resize_y;

        // save last event point
        m_last_resize_x = me.x_root;
        m_last_resize_y = me.y_root;

        // undraw rectangle before warping workspaces
        if (xor_outline) {
            int bw = static_cast<int>(frame().window().borderWidth());
            int w = static_cast<int>(frame().width()) + 2*bw -1;
            int h = static_cast<int>(frame().height()) + 2*bw - 1;
            if (w > 0 && h > 0) {
                parent().drawRectangle(screen().rootTheme()->opGC(),
                    m_last_move_x, m_last_move_y, w, h);
            }
        }


        // check for warping
        //
        // +--monitor-1--+--monitor-2---+
        // |w            |             w|
        // |w            |             w|
        // +-------------+--------------+
        //
        // mouse-warping is enabled, the mouse needs to be in the "warp_pad"
        // zone.
        //
        const int  warp_pad            = screen().getEdgeSnapThreshold();
        const int  workspaces          = screen().numberOfWorkspaces();
        const bool is_warping          = screen().isWorkspaceWarping();
        const bool is_warping_horzntal = screen().isWorkspaceWarpingHorizontal();
        const bool is_warping_vertical = screen().isWorkspaceWarpingVertical();

        if ((moved_x || moved_y) && is_warping) {
            unsigned int cur_id = screen().currentWorkspaceID();
            unsigned int new_id = cur_id;

            // border threshold
            int bt_right  = int(screen().width()) - warp_pad - 1;
            int bt_left   = warp_pad;
            int bt_top    = int(screen().height()) - warp_pad - 1;
            int bt_bottom = warp_pad;

            if (moved_x && is_warping_horzntal) {
	        const int warp_offset = screen().getWorkspaceWarpingHorizontalOffset();
                if (me.x_root >= bt_right && moved_x > 0) { //warp right
                    new_id          = (cur_id + warp_offset) % workspaces;
                    m_last_resize_x = 0;
                } else if (me.x_root <= bt_left && moved_x < 0) { //warp left
                    new_id          = (cur_id + workspaces - warp_offset) % workspaces;
                    m_last_resize_x = screen().width() - 1;
                }
            }

            if (moved_y && is_warping_vertical) {

                const int warp_offset = screen().getWorkspaceWarpingVerticalOffset();

                if (me.y_root >= bt_top && moved_y > 0) { // warp down
                    new_id          = (cur_id + warp_offset) % workspaces;
                    m_last_resize_y = 0;
                } else if (me.y_root <= bt_bottom && moved_y < 0) { // warp up
                    new_id          = (cur_id + workspaces - warp_offset) % workspaces;
                    m_last_resize_y = screen().height() - 1;
                }
            }

            // if we are warping
            if (new_id != cur_id) {
                // remove motion events from queue to avoid repeated warps
                while (XCheckTypedEvent(display, MotionNotify, &e)) {
                    // might as well update the y-coordinate
                    m_last_resize_y = e.xmotion.y_root;
                }

                // move the pointer to (m_last_resize_x,m_last_resize_y)
                XWarpPointer(display, None, me.root, 0, 0, 0, 0,
                        m_last_resize_x, m_last_resize_y);

                if (m_attaching_tab || // tabbing grabs the pointer, we must not hide the window!
                            screen().doOpaqueMove())
                    screen().sendToWorkspace(new_id, this, true);
                else
                    screen().changeWorkspaceID(new_id, false);
            }
        }

        int dx = m_last_resize_x - m_button_grab_x,
            dy = m_last_resize_y - m_button_grab_y;

        dx -= frame().window().borderWidth();
        dy -= frame().window().borderWidth();

        // dx = current left side, dy = current top
        if (moving)
            doSnapping(dx, dy);

        // do not update display if another motion event is already pending

        if (xor_outline) {
            int bw = frame().window().borderWidth();
            int w = static_cast<int>(frame().width()) + 2*bw - 1;
            int h = static_cast<int>(frame().height()) + 2*bw - 1;
            if (w > 0 && h > 0) {
                parent().drawRectangle(screen().rootTheme()->opGC(), dx, dy, w, h);
            }
            m_last_move_x = dx;
            m_last_move_y = dy;
        } else {
            //moveResize(dx, dy, frame().width(), frame().height());
            // need to move the base window without interfering with transparency
            frame().quietMoveResize(dx, dy, frame().width(), frame().height());
        }
        if (moving)
            screen().showPosition(dx, dy);
        // end if moving
    } else if (resizing) {

        int old_resize_x = m_last_resize_x;
        int old_resize_y = m_last_resize_y;
        int old_resize_w = m_last_resize_w;
        int old_resize_h = m_last_resize_h;

        int dx = me.x_root - m_button_grab_x;
        int dy = me.y_root - m_button_grab_y;

        if (m_resize_corner == LEFTTOP || m_resize_corner == LEFTBOTTOM ||
                m_resize_corner == LEFT) {
            m_last_resize_w = resize_base_w - dx;
            m_last_resize_x = resize_base_x + dx;
        }
        if (m_resize_corner == LEFTTOP || m_resize_corner == RIGHTTOP ||
                m_resize_corner == TOP) {
            m_last_resize_h = resize_base_h - dy;
            m_last_resize_y = resize_base_y + dy;
        }
        if (m_resize_corner == LEFTBOTTOM || m_resize_corner == BOTTOM ||
                m_resize_corner == RIGHTBOTTOM)
            m_last_resize_h = resize_base_h + dy;
        if (m_resize_corner == RIGHTBOTTOM || m_resize_corner == RIGHTTOP ||
                m_resize_corner == RIGHT)
            m_last_resize_w = resize_base_w + dx;
        if (m_resize_corner == CENTER) {
            // dx or dy must be at least 2
            if (abs(dx) >= 2 || abs(dy) >= 2) {
                // take max and make it even
                int diff = 2 * (max(dx, dy) / 2);

                m_last_resize_h =  resize_base_h + diff;

                m_last_resize_w = resize_base_w + diff;
                m_last_resize_x = resize_base_x - diff/2;
                m_last_resize_y = resize_base_y - diff/2;
            }
        }

        fixSize();
        frame().displaySize(m_last_resize_w, m_last_resize_h);

        if (old_resize_x != m_last_resize_x ||
                old_resize_y != m_last_resize_y ||
                old_resize_w != m_last_resize_w ||
                old_resize_h != m_last_resize_h ) {

                if (screen().getEdgeResizeSnapThreshold() != 0) {
                    int tx, ty;
                    int botright_x = m_last_resize_x + m_last_resize_w;
                    int botright_y = m_last_resize_y + m_last_resize_h;

                    switch (m_resize_corner) {
                    case LEFTTOP:
                        tx = m_last_resize_x;
                        ty = m_last_resize_y;

                        doSnapping(tx, ty, true);

                        m_last_resize_x = tx;
                        m_last_resize_y = ty;

                        m_last_resize_w = botright_x - m_last_resize_x;
                        m_last_resize_h = botright_y - m_last_resize_y;

                        break;

                    case LEFTBOTTOM:
                        tx = m_last_resize_x;
                        ty = m_last_resize_y + m_last_resize_h;

                        ty += frame().window().borderWidth() * 2;

                        doSnapping(tx, ty, true);

                        ty -= frame().window().borderWidth() * 2;

                        m_last_resize_x = tx;
                        m_last_resize_h = ty - m_last_resize_y;

                        m_last_resize_w = botright_x - m_last_resize_x;

                        break;

                    case RIGHTTOP:
                        tx = m_last_resize_x + m_last_resize_w;
                        ty = m_last_resize_y;

                        tx += frame().window().borderWidth() * 2;

                        doSnapping(tx, ty, true);

                        tx -= frame().window().borderWidth() * 2;

                        m_last_resize_w = tx - m_last_resize_x;
                        m_last_resize_y = ty;

                        m_last_resize_h = botright_y - m_last_resize_y;

                        break;

                    case RIGHTBOTTOM:
                        tx = m_last_resize_x + m_last_resize_w;
                        ty = m_last_resize_y + m_last_resize_h;

                        tx += frame().window().borderWidth() * 2;
                        ty += frame().window().borderWidth() * 2;

                        doSnapping(tx, ty, true);

                        tx -= frame().window().borderWidth() * 2;
                        ty -= frame().window().borderWidth() * 2;

                        m_last_resize_w = tx - m_last_resize_x;
                        m_last_resize_h = ty - m_last_resize_y;

                        break;

                    default:
                        break;
                    }
                }

            if (m_last_resize_w != old_resize_w || m_last_resize_h != old_resize_h) {
                if (screen().doOpaqueResize()) {
                    m_resizeTimer.start();
                } else {
                    // draw over old rect
                    parent().drawRectangle(screen().rootTheme()->opGC(),
                            old_resize_x, old_resize_y,
                            old_resize_w - 1 + 2 * frame().window().borderWidth(),
                            old_resize_h - 1 + 2 * frame().window().borderWidth());

                    // draw resize rectangle
                    parent().drawRectangle(screen().rootTheme()->opGC(),
                            m_last_resize_x, m_last_resize_y,
                            m_last_resize_w - 1 + 2 * frame().window().borderWidth(),
                            m_last_resize_h - 1 + 2 * frame().window().borderWidth());
                }
            }
        }
    }
}

void FluxboxWindow::enterNotifyEvent(XCrossingEvent &ev) {

    static FluxboxWindow *s_last_really_entered = 0;

    if (ev.mode == NotifyUngrab && s_last_really_entered == this) {
        // if this results from an ungrab, only act if the window really changed.
        // otherwise we might pollute the focus which could have been assigned
        // by alt+tab (bug #597)
        return;
    }

    // ignore grab activates, or if we're not visible
    if (ev.mode == NotifyGrab || !isVisible()) {
        return;
    }

    s_last_really_entered = this;
    if (ev.window == frame().window())
        Fluxbox::instance()->keys()->doAction(ev.type, ev.state, 0,
                                              Keys::ON_WINDOW, m_client);

    // determine if we're in a label button (tab)
    WinClient *client = winClientOfLabelButtonWindow(ev.window);
    if (client) {
        if (IconButton *tab = m_labelbuttons[client]) {
            m_has_tooltip = true;
            tab->showTooltip();
        }
    }

    if (ev.window == frame().window() ||
        ev.window == m_client->window() ||
        client) {

        if (m_mouse_focus && !isFocused() && acceptsFocus()) {

            // check that there aren't any subsequent leave notify events in the
            // X event queue
            XEvent dummy;
            scanargs sa;
            sa.w = ev.window;
            sa.enter = sa.leave = False;
            XCheckIfEvent(display, &dummy, queueScanner, (char *) &sa);

            if ((!sa.leave || sa.inferior) &&
                !screen().focusControl().isCycling() &&
                !screen().focusControl().isIgnored(ev.x_root, ev.y_root) ) {
                focus();
            }
        }
    }

    if (screen().focusControl().isMouseTabFocus() &&
        client && client != m_client &&
        !screen().focusControl().isIgnored(ev.x_root, ev.y_root) ) {
        m_tabActivationTimer.start();
    }

}

void FluxboxWindow::leaveNotifyEvent(XCrossingEvent &ev) {

    // ignore grab activates, or if we're not visible
    if (ev.mode == NotifyGrab || ev.mode == NotifyUngrab ||
        !isVisible()) {
        return;
    }

    if (m_has_tooltip) {
        m_has_tooltip = false;
        screen().hideTooltip();
    }

    // still inside?
    if (ev.x_root > frame().x() && ev.y_root > frame().y() &&
        ev.x_root <= (int)(frame().x() + frame().width()) &&
        ev.y_root <= (int)(frame().y() + frame().height()))
        return;

    Fluxbox::instance()->keys()->doAction(ev.type, ev.state, 0,
                                          Keys::ON_WINDOW, m_client);

    // I hope commenting this out is right - simon 21jul2003
    //if (ev.window == frame().window())
    //installColormap(false);
}

// commit current decoration values to actual displayed things
void FluxboxWindow::applyDecorations() {
    frame().setDecorationMask(decorationMask());
    frame().applyDecorations();
}

void FluxboxWindow::toggleDecoration() {
    //don't toggle decor if the window is shaded
    if (isShaded() || isFullscreen())
        return;

    m_toggled_decos = !m_toggled_decos;

    if (m_toggled_decos) {
        m_old_decoration_mask = decorationMask();
        if (decorations.titlebar | decorations.tab)
            setDecorationMask(WindowState::DECOR_NONE);
        else
            setDecorationMask(WindowState::DECOR_NORMAL);
    } else //revert back to old decoration
        setDecorationMask(m_old_decoration_mask);

}

unsigned int FluxboxWindow::decorationMask() const {
    unsigned int ret = 0;
    if (decorations.titlebar)
        ret |= WindowState::DECORM_TITLEBAR;
    if (decorations.handle)
        ret |= WindowState::DECORM_HANDLE;
    if (decorations.border)
        ret |= WindowState::DECORM_BORDER;
    if (decorations.iconify)
        ret |= WindowState::DECORM_ICONIFY;
    if (decorations.maximize)
        ret |= WindowState::DECORM_MAXIMIZE;
    if (decorations.close)
        ret |= WindowState::DECORM_CLOSE;
    if (decorations.menu)
        ret |= WindowState::DECORM_MENU;
    if (decorations.sticky)
        ret |= WindowState::DECORM_STICKY;
    if (decorations.shade)
        ret |= WindowState::DECORM_SHADE;
    if (decorations.tab)
        ret |= WindowState::DECORM_TAB;
    if (decorations.enabled)
        ret |= WindowState::DECORM_ENABLED;
    return ret;
}

void FluxboxWindow::setDecorationMask(unsigned int mask, bool apply) {
    decorations.titlebar = mask & WindowState::DECORM_TITLEBAR;
    decorations.handle   = mask & WindowState::DECORM_HANDLE;
    decorations.border   = mask & WindowState::DECORM_BORDER;
    decorations.iconify  = mask & WindowState::DECORM_ICONIFY;
    decorations.maximize = mask & WindowState::DECORM_MAXIMIZE;
    decorations.close    = mask & WindowState::DECORM_CLOSE;
    decorations.menu     = mask & WindowState::DECORM_MENU;
    decorations.sticky   = mask & WindowState::DECORM_STICKY;
    decorations.shade    = mask & WindowState::DECORM_SHADE;
    decorations.tab      = mask & WindowState::DECORM_TAB;
    decorations.enabled  = mask & WindowState::DECORM_ENABLED;

    // we don't want to do this during initialization
    if (apply)
        applyDecorations();
}


void FluxboxWindow::setupWindow() {

    // sets up our window
    // we allow both to be done at once to share the commands
    FbTk::ResourceManager &rm = screen().resourceManager();

    struct {
        std::string name;
        std::string alt_name;
        size_t n_buttons;
        WinButton::Type buttons[3];
    } side[2] = {
        {
            screen().name() + ".titlebar.left",
            screen().name() + ".Titlebar.Left",
            1, { WinButton::STICK },
        },
        {
            screen().name() + ".titlebar.right",
            screen().name() + ".Titlebar.Right",
            3, { WinButton::MINIMIZE, WinButton::MAXIMIZE, WinButton::CLOSE },
        }
    };


    // create resource for titlebar
    for (size_t i = 0; i < sizeof(side)/sizeof(side[0]); ++i) {

        WBR* res = dynamic_cast<WBR*>(rm.findResource(side[i].name));
        if (res != 0)
            continue; // find next resource too

        WinButton::Type* s = &side[i].buttons[0];
        WinButton::Type* e = s + side[i].n_buttons;
        res = new WBR(rm, WBR::Type(s, e), side[i].name, side[i].alt_name);
        screen().addManagedResource(res);
    }

    updateButtons();
}


void FluxboxWindow::updateButtons() {

    ResourceManager &rm = screen().resourceManager();
    size_t i;
    size_t j;
    struct {
        std::string name;
        WBR* res;
    } sides[2] = {
        { screen().name() + ".titlebar.left", 0 },
        { screen().name() + ".titlebar.right", 0 },
    };
    const size_t n_sides = sizeof(sides)/sizeof(sides[0]);
    bool need_update = false;


    // get button resources for each titlebar and check if they differ
    for (i = 0; i < n_sides; ++i) {

        sides[i].res = dynamic_cast<WBR*>(rm.findResource(sides[i].name));

        if (sides[i].res == 0) {
            if (!m_titlebar_buttons[i].empty()) {
                need_update = true;
            }
            continue;
        }

        // check if we need to update our buttons
        const vector<WinButton::Type>& buttons = *(*sides[i].res);
        size_t s = buttons.size();

        if (s != m_titlebar_buttons[i].size()) {
            need_update = true;
            continue;
        }

        for (j = 0; !need_update && j < s; j++) {
            if (buttons[j] != m_titlebar_buttons[i][j]) {
                need_update = true;
                break;
            }
        }
    }

    if (!need_update)
        return;

    frame().removeAllButtons();

    using namespace FbTk;
    typedef RefCount<Command<void> > CommandRef;
    typedef SimpleCommand<FluxboxWindow> WindowCmd;

    CommandRef iconify_cmd(new WindowCmd(*this, &FluxboxWindow::iconify));
    CommandRef maximize_cmd(new WindowCmd(*this, &FluxboxWindow::maximizeFull));
    CommandRef maximize_vert_cmd(new WindowCmd(*this, &FluxboxWindow::maximizeVertical));
    CommandRef maximize_horiz_cmd(new WindowCmd(*this, &FluxboxWindow::maximizeHorizontal));
    CommandRef close_cmd(new WindowCmd(*this, &FluxboxWindow::close));
    CommandRef shade_cmd(new WindowCmd(*this, &FluxboxWindow::shade));
    CommandRef stick_cmd(new WindowCmd(*this, &FluxboxWindow::stick));
    CommandRef show_menu_cmd(new WindowCmd(*this, &FluxboxWindow::popupMenu));

    for (i = 0; i < n_sides; i++) {

        if (sides[i].res == 0) {
            continue;
        }

        const vector<WinButton::Type>& buttons = *(*sides[i].res);
        m_titlebar_buttons[i] = buttons;

        for (j = 0; j < buttons.size(); ++j) {

            WinButton* btn = 0;

            switch (buttons[j]) {
            case WinButton::MINIMIZE:
                if (isIconifiable() && (m_state.deco_mask & WindowState::DECORM_ICONIFY)) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->setOnClick(iconify_cmd);
                }
                break;
            case WinButton::MAXIMIZE:
                if (isMaximizable() && (m_state.deco_mask & WindowState::DECORM_MAXIMIZE) ) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->setOnClick(maximize_cmd, 1);
                    btn->setOnClick(maximize_horiz_cmd, 3);
                    btn->setOnClick(maximize_vert_cmd, 2);
                }
                break;
            case WinButton::CLOSE:
                if (m_client->isClosable() && (m_state.deco_mask & WindowState::DECORM_CLOSE)) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->setOnClick(close_cmd);
                    btn->join(stateSig(), FbTk::MemFunIgnoreArgs(*btn, &WinButton::updateAll));
                }
                break;
            case WinButton::STICK:
                if (m_state.deco_mask & WindowState::DECORM_STICKY) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->join(stateSig(), FbTk::MemFunIgnoreArgs(*btn, &WinButton::updateAll));
                    btn->setOnClick(stick_cmd);
                }
                break;
            case WinButton::SHADE:
                if (m_state.deco_mask & WindowState::DECORM_SHADE) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->join(stateSig(), FbTk::MemFunIgnoreArgs(*btn, &WinButton::updateAll));
                    btn->setOnClick(shade_cmd);
                }
                break;
            case WinButton::MENUICON:
                if (m_state.deco_mask & WindowState::DECORM_MENU) {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    btn->join(titleSig(), FbTk::MemFunIgnoreArgs(*btn, &WinButton::updateAll));
                    btn->setOnClick(show_menu_cmd);
                }
                break;

            case WinButton::LEFT_HALF:
                {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    CommandRef lhalf_cmd(FbTk::CommandParser<void>::instance().parse("MacroCmd {MoveTo 0 0} {ResizeTo 50% 100%}"));
                    btn->setOnClick(lhalf_cmd);
                }
                break;

            case WinButton::RIGHT_HALF:
                {
                    btn = makeButton(*this, m_button_theme, buttons[j]);
                    CommandRef rhalf_cmd(FbTk::CommandParser<void>::instance().parse("MacroCmd {MoveTo 50% 0} {ResizeTo 50% 100%}"));
                    btn->setOnClick(rhalf_cmd);
                }
                break;

            }


            if (btn != 0) {
                btn->show();
                if (i == 0)
                    frame().addLeftButton(btn);
                else
                    frame().addRightButton(btn);
            }
        }
    }

    frame().reconfigure();
}
