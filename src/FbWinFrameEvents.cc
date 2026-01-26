// FbWinFrameEvents.cc for Fluxbox Window Manager
// Copyright (c) 2003 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
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

#include "FbWinFrame.hh"

#include "Keys.hh"
#include "RectangleUtil.hh"

#include "FbTk/EventManager.hh"
#include "FbTk/CompareEqual.hh"

#include <algorithm>

/**
   Set new event handler for the frame's windows
*/
void FbWinFrame::setEventHandler(FbTk::EventHandler &evh) {

    FbTk::EventManager &evm = *FbTk::EventManager::instance();
    evm.add(evh, m_tab_container);
    evm.add(evh, m_label);
    evm.add(evh, m_titlebar);
    evm.add(evh, m_handle);
    evm.add(evh, m_grip_right);
    evm.add(evh, m_grip_left);
    evm.add(evh, m_window);
}

/**
   remove event handler from windows
*/
void FbWinFrame::removeEventHandler() {
    FbTk::EventManager &evm = *FbTk::EventManager::instance();
    evm.remove(m_tab_container);
    evm.remove(m_label);
    evm.remove(m_titlebar);
    evm.remove(m_handle);
    evm.remove(m_grip_right);
    evm.remove(m_grip_left);
    evm.remove(m_window);
}

void FbWinFrame::exposeEvent(XExposeEvent &event) {
    FbTk::FbWindow* win = 0;
    if (m_titlebar == event.window) {
        win = &m_titlebar;
    } else if (m_tab_container == event.window) {
        win = &m_tab_container;
    } else if (m_label == event.window) {
        win = &m_label;
    } else if (m_handle == event.window) {
        win = &m_handle;
    } else if (m_grip_left == event.window) {
        win = &m_grip_left;
    } else if (m_grip_right == event.window) {
        win = &m_grip_right;
    } else {

        if (m_tab_container.tryExposeEvent(event))
            return;

        // create compare function
        // that we should use with find_if
        FbTk::CompareEqual_base<FbTk::FbWindow, Window> compare(&FbTk::FbWindow::window,
                                                                event.window);

        ButtonList::iterator it = find_if(m_buttons_left.begin(),
                                          m_buttons_left.end(),
                                          compare);
        if (it != m_buttons_left.end()) {
            (*it)->exposeEvent(event);
            return;
        }

        it = find_if(m_buttons_right.begin(),
                     m_buttons_right.end(),
                     compare);

        if (it != m_buttons_right.end())
            (*it)->exposeEvent(event);

        return;
    }

    win->clearArea(event.x, event.y, event.width, event.height);
}

void FbWinFrame::handleEvent(XEvent &event) {
    if (event.type == ConfigureNotify && event.xconfigure.window == window().window())
        configureNotifyEvent(event.xconfigure);
}

void FbWinFrame::configureNotifyEvent(XConfigureEvent &event) {
    resize(event.width, event.height);
}

bool FbWinFrame::insideTitlebar(Window win) const {
    return
        gripLeft().window() != win &&
        gripRight().window() != win &&
        window().window() != win;
}

int FbWinFrame::getContext(Window win, int x, int y, int last_x, int last_y, bool doBorders) {
    int context = 0;
    if (gripLeft().window()  == win) return Keys::ON_LEFTGRIP;
    if (gripRight().window() == win) return Keys::ON_RIGHTGRIP;
    if (doBorders) {
        using RectangleUtil::insideBorder;
        int borderw = window().borderWidth();
        if ( // if mouse is currently on the window border, ignore it
                (
                    ! insideBorder(window(), x, y, borderw)
                    && ( externalTabMode()
                        || ! insideBorder(tabcontainer(), x, y, borderw) )
                )
                || // or if mouse was on border when it was last clicked
                (
                    ! insideBorder(window(), last_x, last_y, borderw)
                    && ( externalTabMode()
                        || ! insideBorder(tabcontainer(), last_x, last_y, borderw ) )
                )
           ) context = Keys::ON_WINDOWBORDER;
    }

    if (window().window()    == win) return context | Keys::ON_WINDOW;
    // /!\\ old code: handle = titlebar in motionNotifyEvent but only there !
    // handle() as border ??
    if (handle().window()    == win) {
        const int px = x - this->x() - window().borderWidth();
        if (px < gripLeft().x() + gripLeft().width() || px > gripRight().x())
            return context; // one of the corners
        return Keys::ON_WINDOWBORDER | Keys::ON_WINDOW;
    }
    if (titlebar().window()  == win) {
        const int px = x - this->x() - window().borderWidth();
        if (px < label().x() || px > label().x() + label().width())
            return context; // one of the buttons, asked from a grabbed event
        return context | Keys::ON_TITLEBAR;
    }
    if (label().window()     == win) return context | Keys::ON_TITLEBAR;
    // internal tabs are on title bar
    if (tabcontainer().window() == win)
        return context | Keys::ON_TAB | (externalTabMode()?0:Keys::ON_TITLEBAR);


    FbTk::Container::ItemList::iterator it = tabcontainer().begin();
    FbTk::Container::ItemList::iterator it_end = tabcontainer().end();
    for (; it != it_end; ++it) {
        if ((*it)->window() == win)
            break;
    }
    // internal tabs are on title bar
    if (it != it_end)
        return context | Keys::ON_TAB | (externalTabMode()?0:Keys::ON_TITLEBAR);

    return context;
}

