// FbWinFrame.cc for Fluxbox Window Manager
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
#include "FbWinFrameTheme.hh"
#include "Screen.hh"
#include "FocusableTheme.hh"
#include "IconButton.hh"
#include "RectangleUtil.hh"

#include "FbTk/ImageControl.hh"
#include "FbTk/EventManager.hh"
#include "FbTk/App.hh"
#include "FbTk/SimpleCommand.hh"
#include "FbTk/Compose.hh"
#include "FbTk/Transparent.hh"
#include "FbTk/STLUtil.hh"

#include <X11/X.h>

#include <algorithm>

using std::max;
using std::mem_fn;
using std::string;

using FbTk::STLUtil::forAll;

namespace {

enum { UNFOCUS = 0, FOCUS, PRESSED };

const int s_button_size = 26;
const long s_mask = ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | EnterWindowMask | LeaveWindowMask;

const struct {
    FbWinFrame::TabPlacement where;
    FbTk::Orientation orient;
    FbTk::Container::Alignment align;
    bool is_horizontal;
} s_place[] = {
    { /* unused */ },
    { FbWinFrame::TOPLEFT,    FbTk::ROT0,   FbTk::Container::LEFT,   true },
    { FbWinFrame::TOP,        FbTk::ROT0,   FbTk::Container::CENTER, true },
    { FbWinFrame::TOPRIGHT,   FbTk::ROT0,   FbTk::Container::RIGHT,  true },
    { FbWinFrame::BOTTOMLEFT, FbTk::ROT0,   FbTk::Container::LEFT,   true },
    { FbWinFrame::BOTTOM,     FbTk::ROT0,   FbTk::Container::CENTER, true },
    { FbWinFrame::BOTTOMRIGHT,FbTk::ROT0,   FbTk::Container::RIGHT,  true },
    { FbWinFrame::LEFTTOP,    FbTk::ROT270, FbTk::Container::RIGHT,  false },
    { FbWinFrame::LEFT,       FbTk::ROT270, FbTk::Container::CENTER, false },
    { FbWinFrame::LEFTBOTTOM, FbTk::ROT270, FbTk::Container::LEFT,   false },
    { FbWinFrame::RIGHTTOP,   FbTk::ROT90,  FbTk::Container::LEFT,   false },
    { FbWinFrame::RIGHT,      FbTk::ROT90,  FbTk::Container::LEFT,   false },
    { FbWinFrame::RIGHTBOTTOM,FbTk::ROT90,  FbTk::Container::LEFT,   false },
};

} // end anonymous

FbWinFrame::FbWinFrame(BScreen &screen, unsigned int client_depth,
                       WindowState &state,
                       FocusableTheme<FbWinFrameTheme> &theme):
    m_screen(screen),
    m_theme(theme),
    m_imagectrl(screen.imageControl()),
    m_state(state),
    m_window(theme->screenNum(), state.x, state.y, state.width, state.height, s_mask, true, false,
        client_depth, InputOutput,
        (client_depth == screen.rootWindow().maxDepth() ? screen.rootWindow().visual() : CopyFromParent),
        (client_depth == screen.rootWindow().maxDepth() ? screen.rootWindow().colormap() : CopyFromParent)),
    m_layeritem(window(), *screen.layerManager().getLayer(ResourceLayer::NORMAL)),
    m_titlebar(m_window, 0, 0, 100, 16, s_mask, false, false, 
        screen.rootWindow().decorationDepth(), InputOutput,
        screen.rootWindow().decorationVisual(),
        screen.rootWindow().decorationColormap()),
    m_tab_container(m_titlebar),
    m_label(m_titlebar, m_theme->font(), FbTk::BiDiString("")),
    m_handle(m_window, 0, 0, 100, 5, s_mask, false, false,
        screen.rootWindow().decorationDepth(), InputOutput,
        screen.rootWindow().decorationVisual(),
        screen.rootWindow().decorationColormap()),
    m_grip_right(m_handle, 0, 0, 10, 4, s_mask, false, false,
        screen.rootWindow().decorationDepth(), InputOutput,
        screen.rootWindow().decorationVisual(),
        screen.rootWindow().decorationColormap()),
    m_grip_left(m_handle, 0, 0, 10, 4, s_mask, false, false,
        screen.rootWindow().decorationDepth(), InputOutput,
        screen.rootWindow().decorationVisual(),
        screen.rootWindow().decorationColormap()),
    m_clientarea(m_window, 0, 0, 100, 100, s_mask),
    m_bevel(1),
    m_use_titlebar(true),
    m_use_tabs(true),
    m_use_handle(true),
    m_visible(false),
    m_tabmode(screen.getDefaultInternalTabs()?INTERNAL:EXTERNAL),
    m_active_orig_client_bw(0),
    m_need_render(true),
    m_button_size(1),
    m_shape(m_window, theme->shapePlace()) {

    init();
}

FbWinFrame::~FbWinFrame() {
    removeEventHandler();
    removeAllButtons();
}

bool FbWinFrame::setTabMode(TabMode tabmode) {
    if (m_tabmode == tabmode)
        return false;

    FbTk::Container& tabs = tabcontainer();
    bool ret = true;

    // setting tabmode to notset forces it through when
    // something is likely to change
    if (tabmode == NOTSET)
        tabmode = m_tabmode;

    m_tabmode = tabmode;

    // reparent tab container
    if (tabmode == EXTERNAL) {
        m_label.show();
        tabs.setBorderWidth(m_window.borderWidth());
        tabs.setEventMask(s_mask);
        alignTabs();

        // TODO: tab position
        if (m_use_tabs && m_visible)
            tabs.show();
        else {
            ret = false;
            tabs.hide();
        }

    } else {
        tabs.setUpdateLock(true);

        tabs.setAlignment(FbTk::Container::RELATIVE);
        tabs.setOrientation(FbTk::ROT0);
        if (tabs.parent()->window() == m_screen.rootWindow().window()) {
            m_layeritem.removeWindow(m_tab_container);
            tabs.hide();
            tabs.reparent(m_titlebar, m_label.x(), m_label.y());
            tabs.invalidateBackground();
            tabs.resize(m_label.width(), m_label.height());
            tabs.raise();
        }
        tabs.setBorderWidth(0);
        tabs.setMaxTotalSize(0);
        tabs.setUpdateLock(false);
        tabs.setMaxSizePerClient(0);

        renderTabContainer();
        applyTabContainer();

        tabs.clear();
        tabs.raise();
        tabs.show();

        if (!m_use_tabs)
            ret = false;

        m_label.hide();
    }

    return ret;
}

void FbWinFrame::hide() {
    m_window.hide();
    if (m_tabmode == EXTERNAL && m_use_tabs)
        m_tab_container.hide();

    m_visible = false;
}

void FbWinFrame::show() {
    m_visible = true;

    if (m_need_render) {
        renderAll();
        applyAll();
        clearAll();
    }

    if (m_tabmode == EXTERNAL && m_use_tabs)
        m_tab_container.show();

    m_window.showSubwindows();
    m_window.show();
}

void FbWinFrame::move(int x, int y) {
    moveResize(x, y, 0, 0, true, false);
}

void FbWinFrame::resize(unsigned int width, unsigned int height) {
    moveResize(0, 0, width, height, false, true);
}

// need an atomic moveresize where possible
void FbWinFrame::moveResizeForClient(int x, int y,
                                     unsigned int width, unsigned int height,
                                     int win_gravity,
                                     unsigned int client_bw,
                                     bool move, bool resize) {
    // total height for frame

    if (resize) // these fns check if the elements are "on"
        height += titlebarHeight() + handleHeight();

    gravityTranslate(x, y, win_gravity, client_bw, false);
    setActiveGravity(win_gravity, client_bw);
    moveResize(x, y, width, height, move, resize);
}

void FbWinFrame::resizeForClient(unsigned int width, unsigned int height,
                                 int win_gravity, unsigned int client_bw) {
    moveResizeForClient(0, 0, width, height, win_gravity, client_bw, false, true);
}

void FbWinFrame::moveResize(int x, int y, unsigned int width, unsigned int height, bool move, bool resize, bool force) {
    if (!force && move && x == window().x() && y == window().y())
        move = false;

    if (!force && resize && width == FbWinFrame::width() &&
                  height == FbWinFrame::height())
        resize = false;

    if (!move && !resize)
        return;

    if (move && resize) {
        m_window.moveResize(x, y, width, height);
        notifyMoved(false); // will reconfigure
    } else if (move) {
        m_window.move(x, y);
        // this stuff will be caught by reconfigure if resized
        notifyMoved(true);
    } else {
        m_window.resize(width, height);
    }

    m_state.saveGeometry(window().x(), window().y(),
                         window().width(), window().height());

    if (move || (resize && m_screen.getTabPlacement() != TOPLEFT &&
                           m_screen.getTabPlacement() != LEFTTOP))
        alignTabs();

    if (resize) {
        if (m_tabmode == EXTERNAL) {
            unsigned int s = width;
            if (!s_place[m_screen.getTabPlacement()].is_horizontal) {
                s = height;
            }
            m_tab_container.setMaxTotalSize(s);
        }
        reconfigure();
    }
}

void FbWinFrame::quietMoveResize(int x, int y,
                                 unsigned int width, unsigned int height) {
    m_window.moveResize(x, y, width, height);
    m_state.saveGeometry(window().x(), window().y(),
                         window().width(), window().height());
    if (m_tabmode == EXTERNAL) {
        unsigned int s = width;
        if (!s_place[m_screen.getTabPlacement()].is_horizontal) {
            s = height;
        }
        m_tab_container.setMaxTotalSize(s);
        alignTabs();
    }
}

void FbWinFrame::alignTabs() {
    if (m_tabmode != EXTERNAL)
        return;


    FbTk::Container& tabs = tabcontainer();
    FbTk::Orientation orig_orient = tabs.orientation();
    unsigned int orig_tabwidth = tabs.maxWidthPerClient();

    if (orig_tabwidth != m_screen.getTabWidth())
        tabs.setMaxSizePerClient(m_screen.getTabWidth());

    int bw = window().borderWidth();
    int size = width();
    int tab_x = x();
    int tab_y = y();

    TabPlacement p = m_screen.getTabPlacement();
    if (orig_orient != s_place[p].orient) {
        tabs.hide();
    }
    if (!s_place[p].is_horizontal) {
        size = height();
    }
    tabs.setOrientation(s_place[p].orient);
    tabs.setAlignment(s_place[p].align);
    tabs.setMaxTotalSize(size);

    int w = static_cast<int>(width());
    int h = static_cast<int>(height());
    int xo = xOffset();
    int yo = yOffset();
    int tw = static_cast<int>(tabs.width());
    int th = static_cast<int>(tabs.height());

    switch (p) {
    case TOPLEFT:                          tab_y -= yo;         break;
    case TOP:         tab_x += (w - tw)/2; tab_y -= yo;         break;
    case TOPRIGHT:    tab_x +=  w - tw;    tab_y -= yo;         break;
    case BOTTOMLEFT:                       tab_y +=  h + bw;    break;
    case BOTTOM:      tab_x += (w - tw)/2; tab_y +=  h + bw;    break;
    case BOTTOMRIGHT: tab_x +=  w - tw;    tab_y +=  h + bw;    break;
    case LEFTTOP:     tab_x -=  xo;                             break;
    case LEFT:        tab_x -=  xo;        tab_y += (h - th)/2; break;
    case LEFTBOTTOM:  tab_x -=  xo;        tab_y +=  h - th;    break;
    case RIGHTTOP:    tab_x +=  w + bw;                         break;
    case RIGHT:       tab_x +=  w + bw;    tab_y += (h - th)/2; break;
    case RIGHTBOTTOM: tab_x +=  w + bw;    tab_y +=  h - th;    break;
    }

    if (tabs.orientation() != orig_orient ||
        tabs.maxWidthPerClient() != orig_tabwidth) {
        renderTabContainer();
        if (m_visible && m_use_tabs) {
            applyTabContainer();
            tabs.clear();
            tabs.show();
        }
    }

    if (tabs.parent()->window() != m_screen.rootWindow().window()) {
        tabs.reparent(m_screen.rootWindow(), tab_x, tab_y);
        tabs.clear();
        m_layeritem.addWindow(tabs);
    } else {
        tabs.move(tab_x, tab_y);
    }
}

void FbWinFrame::notifyMoved(bool clear) {
    // not important if no alpha...
    int alpha = getAlpha(m_state.focused);
    if (alpha == 255)
        return;

    if ((m_tabmode == EXTERNAL && m_use_tabs) || m_use_titlebar) {
        m_tab_container.parentMoved();
        m_tab_container.for_each(mem_fn(&FbTk::Button::parentMoved));
    }

    if (m_use_titlebar) {
        if (m_tabmode != INTERNAL)
            m_label.parentMoved();

        m_titlebar.parentMoved();

        forAll(m_buttons_left, mem_fn(&FbTk::Button::parentMoved));
        forAll(m_buttons_right, mem_fn(&FbTk::Button::parentMoved));
    }

    if (m_use_handle) {
        m_handle.parentMoved();
        m_grip_left.parentMoved();
        m_grip_right.parentMoved();
    }

    if (clear && (m_use_handle || m_use_titlebar)) {
        clearAll();
    } else if (clear && m_tabmode == EXTERNAL && m_use_tabs)
        m_tab_container.clear();
}

void FbWinFrame::clearAll() {

    if  (m_use_titlebar) {
        redrawTitlebar();
        forAll(m_buttons_left, mem_fn(&FbTk::Button::clear));
        forAll(m_buttons_right, mem_fn(&FbTk::Button::clear));
    } else if (m_tabmode == EXTERNAL && m_use_tabs)
        m_tab_container.clear();

    if (m_use_handle) {
        m_handle.clear();
        m_grip_left.clear();
        m_grip_right.clear();
    }
}

void FbWinFrame::setFocus(bool newvalue) {
    if (m_state.focused == newvalue)
        return;

    m_state.focused = newvalue;

    if (FbTk::Transparent::haveRender() &&
        getAlpha(true) != getAlpha(false)) { // different alpha for focused and unfocused

        int alpha = getAlpha(m_state.focused);
        int opaque = 255;
        if (FbTk::Transparent::haveComposite()) {
            std::swap(alpha, opaque);
        }
        m_tab_container.setAlpha(alpha);
        m_window.setOpaque(opaque);
    }

    setBorderWidth();

    applyAll();
    clearAll();
}

void FbWinFrame::applyState() {
    applyDecorations(false);

    const int head = m_screen.getHead(window());
    int new_x = m_state.x, new_y = m_state.y;
    unsigned int new_w = m_state.width, new_h = m_state.height;

    if (m_state.isMaximizedVert()) {
        new_y = m_screen.maxTop(head);
        new_h = m_screen.maxBottom(head) - new_y - 2*window().borderWidth();
        if (!m_screen.getMaxOverTabs()) {
            new_y += yOffset();
            new_h -= heightOffset();
        }
    }
    if (m_state.isMaximizedHorz()) {
        new_x = m_screen.maxLeft(head);
        new_w = m_screen.maxRight(head) - new_x - 2*window().borderWidth();
        if (!m_screen.getMaxOverTabs()) {
            new_x += xOffset();
            new_w -= widthOffset();
        }
    }

    if (m_state.shaded) {
        new_h = m_titlebar.height();
    } else if (m_state.fullscreen) {
        new_x = m_screen.getHeadX(head);
        new_y = m_screen.getHeadY(head);
        new_w = m_screen.getHeadWidth(head);
        new_h = m_screen.getHeadHeight(head);
    } else if (m_state.maximized == WindowState::MAX_NONE || !m_screen.getMaxIgnoreIncrement()) {
        applySizeHints(new_w, new_h, true);
    }

    moveResize(new_x, new_y, new_w, new_h, true, true, true);
    frameExtentSig().emit();
}

void FbWinFrame::setAlpha(bool focused, int alpha) {
    m_alpha[focused] = alpha;
    if (m_state.focused == focused)
        applyAlpha();
}

void FbWinFrame::applyAlpha() {
    int alpha = getAlpha(m_state.focused);
    if (FbTk::Transparent::haveComposite())
        m_window.setOpaque(alpha);
    else {
        // don't need to setAlpha, since apply updates them anyway
        applyAll();
        clearAll();
    }
}

int FbWinFrame::getAlpha(bool focused) const {
    return m_alpha[focused];
}

void FbWinFrame::setDefaultAlpha() {
    if (getUseDefaultAlpha())
        return;
    m_alpha[UNFOCUS] = theme().unfocusedTheme()->alpha();
    m_alpha[FOCUS] = theme().unfocusedTheme()->alpha();
    applyAlpha();
}

bool FbWinFrame::getUseDefaultAlpha() const {
    if (m_alpha[UNFOCUS] != theme().unfocusedTheme()->alpha()) {
        return false;
    } else if (m_alpha[FOCUS] != theme().focusedTheme()->alpha()) {
        return false;
    }

    return true;
}

void FbWinFrame::addLeftButton(FbTk::Button *btn) {
    if (btn == 0) // valid button?
        return;

    applyButton(*btn); // setup theme and other stuff

    m_buttons_left.push_back(btn);
}

void FbWinFrame::addRightButton(FbTk::Button *btn) {
    if (btn == 0) // valid button?
        return;

    applyButton(*btn); // setup theme and other stuff

    m_buttons_right.push_back(btn);
}

void FbWinFrame::removeAllButtons() {

    FbTk::STLUtil::destroyAndClear(m_buttons_left);
    FbTk::STLUtil::destroyAndClear(m_buttons_right);
}

void FbWinFrame::createTab(FbTk::Button &button) {
    button.show();
    button.setEventMask(ExposureMask | ButtonPressMask |
                        ButtonReleaseMask | ButtonMotionMask |
                        EnterWindowMask);
    FbTk::EventManager::instance()->add(button, button.window());

    m_tab_container.insertItem(&button);
}

void FbWinFrame::removeTab(IconButton *btn) {
    if (m_tab_container.removeItem(btn))
        delete btn;
}


void FbWinFrame::moveLabelButtonLeft(FbTk::TextButton &btn) {
    m_tab_container.moveItem(&btn, -1);
}

void FbWinFrame::moveLabelButtonRight(FbTk::TextButton &btn) {
    m_tab_container.moveItem(&btn, +1);
}

void FbWinFrame::moveLabelButtonTo(FbTk::TextButton &btn, int x, int y) {
    m_tab_container.moveItemTo(&btn, x, y);
}



void FbWinFrame::moveLabelButtonLeftOf(FbTk::TextButton &btn, const FbTk::TextButton &dest) {
    int dest_pos = m_tab_container.find(&dest);
    int cur_pos = m_tab_container.find(&btn);
    if (dest_pos < 0 || cur_pos < 0)
        return;
    int movement=dest_pos - cur_pos;
    if(movement>0)
        movement-=1;
//    else
  //      movement-=1;

    m_tab_container.moveItem(&btn, movement);
}

void FbWinFrame::moveLabelButtonRightOf(FbTk::TextButton &btn, const FbTk::TextButton &dest) {
    int dest_pos = m_tab_container.find(&dest);
    int cur_pos = m_tab_container.find(&btn);
    if (dest_pos < 0 || cur_pos < 0 )
        return;
    int movement=dest_pos - cur_pos;
    if(movement<0)
        movement+=1;

    m_tab_container.moveItem(&btn, movement);
}

void FbWinFrame::setClientWindow(FbTk::FbWindow &win) {

    win.setBorderWidth(0);

    XChangeSaveSet(win.display(), win.window(), SetModeInsert);

    m_window.setEventMask(NoEventMask);

    // we need to mask this so we don't get unmap event
    win.setEventMask(NoEventMask);
    win.reparent(m_window, clientArea().x(), clientArea().y());

    m_window.setEventMask(ButtonPressMask | ButtonReleaseMask |
                          ButtonMotionMask | EnterWindowMask |
                          LeaveWindowMask | SubstructureRedirectMask);

    XFlush(win.display());

    // remask window so we get events
    XSetWindowAttributes attrib_set;
    attrib_set.event_mask = PropertyChangeMask | StructureNotifyMask | FocusChangeMask | KeyPressMask;
    attrib_set.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask |
        ButtonMotionMask;

    XChangeWindowAttributes(win.display(), win.window(), CWEventMask|CWDontPropagate, &attrib_set);

    if (isVisible())
        win.show();
    win.raise();
    m_window.showSubwindows();

}

bool FbWinFrame::hideTabs() {
    if (m_tabmode == INTERNAL || !m_use_tabs) {
        m_use_tabs = false;
        return false;
    }

    m_use_tabs = false;
    m_tab_container.hide();
    return true;
}

bool FbWinFrame::showTabs() {
    if (m_tabmode == INTERNAL || m_use_tabs) {
        m_use_tabs = true;
        return false; // nothing changed
    }

    m_use_tabs = true;
    if (m_visible)
        m_tab_container.show();
    return true;
}

bool FbWinFrame::hideTitlebar() {
    if (!m_use_titlebar)
        return false;

    m_titlebar.hide();
    m_use_titlebar = false;

    int h = height();
    int th = m_titlebar.height();
    int tbw = m_titlebar.borderWidth();

    // only take away one borderwidth (as the other border is still the "top"
    // border)
    h = std::max(1, h - th - tbw);
    m_window.resize(m_window.width(), h);

    return true;
}

bool FbWinFrame::showTitlebar() {
    if (m_use_titlebar)
        return false;

    m_titlebar.show();
    m_use_titlebar = true;

    // only add one borderwidth (as the other border is still the "top"
    // border)
    m_window.resize(m_window.width(), m_window.height() + m_titlebar.height() +
                    m_titlebar.borderWidth());

    return true;

}

bool FbWinFrame::hideHandle() {
    if (!m_use_handle)
        return false;

    m_handle.hide();
    m_grip_left.hide();
    m_grip_right.hide();
    m_use_handle = false;

    int h = m_window.height();
    int hh = m_handle.height();
    int hbw = m_handle.borderWidth();

    // only take away one borderwidth (as the other border is still the "top"
    // border)
    h = std::max(1, h - hh - hbw);
    m_window.resize(m_window.width(), h);

    return true;
}

bool FbWinFrame::showHandle() {
    if (m_use_handle || theme()->handleWidth() == 0)
        return false;

    m_use_handle = true;

    // weren't previously rendered...
    renderHandles();
    applyHandles();

    m_handle.show();
    m_handle.showSubwindows(); // shows grips

    m_window.resize(m_window.width(), m_window.height() + m_handle.height() +
                    m_handle.borderWidth());
    return true;
}

void FbWinFrame::setShapingClient(FbTk::FbWindow *win, bool always_update) {
    m_shape.setShapeSource(win, 0, titlebarHeight(), always_update);
}

unsigned int FbWinFrame::buttonHeight() const {
    return m_titlebar.height() - m_bevel*2;
}

//--------------------- private area
void FbWinFrame::init() {

    if (theme()->handleWidth() == 0)
        m_use_handle = false;

    m_alpha[UNFOCUS] = theme().unfocusedTheme()->alpha();
    m_alpha[FOCUS] = theme().focusedTheme()->alpha();

    m_handle.showSubwindows();

    // clear pixmaps
    m_title_face.pm[UNFOCUS] = m_title_face.pm[FOCUS] = 0;
    m_label_face.pm[UNFOCUS] = m_label_face.pm[FOCUS] = 0;
    m_tabcontainer_face.pm[UNFOCUS] = m_tabcontainer_face.pm[FOCUS] = 0;
    m_handle_face.pm[UNFOCUS] = m_handle_face.pm[FOCUS] = 0;
    m_button_face.pm[UNFOCUS] = m_button_face.pm[FOCUS] = m_button_face.pm[PRESSED] = 0;
    m_grip_face.pm[UNFOCUS] = m_grip_face.pm[FOCUS] = 0;

    m_button_size = s_button_size;

    m_label.setBorderWidth(0);

    setTabMode(NOTSET);

    m_label.setEventMask(ExposureMask | ButtonPressMask |
                         ButtonReleaseMask | ButtonMotionMask |
                         EnterWindowMask);

    showHandle();
    showTitlebar();

    // Note: we don't show clientarea yet

    setEventHandler(*this);

    // setup cursors for resize grips
    gripLeft().setCursor(theme()->lowerLeftAngleCursor());
    gripRight().setCursor(theme()->lowerRightAngleCursor());
}

int FbWinFrame::getShape() const {
    int shape = theme()->shapePlace();
    if (!m_state.useTitlebar())
        shape &= ~(FbTk::Shape::TOPRIGHT|FbTk::Shape::TOPLEFT);
    if (!m_state.useHandle())
        shape &= ~(FbTk::Shape::BOTTOMRIGHT|FbTk::Shape::BOTTOMLEFT);
    return shape;
}

void FbWinFrame::applyDecorations(bool do_move) {
    int grav_x=0, grav_y=0;
    // negate gravity
    gravityTranslate(grav_x, grav_y, -sizeHints().win_gravity, m_active_orig_client_bw,
                     false);

    bool client_move = setBorderWidth(false);

    // tab deocration only affects if we're external
    // must do before the setTabMode in case it goes
    // to external and is meant to be hidden
    if (m_state.useTabs())
        client_move |= showTabs();
    else
        client_move |= hideTabs();

    // we rely on frame not doing anything if it is already shown/hidden
    if (m_state.useTitlebar()) {
        client_move |= showTitlebar();
        if (m_screen.getDefaultInternalTabs())
            client_move |= setTabMode(INTERNAL);
        else
            client_move |= setTabMode(EXTERNAL);
    } else {
        client_move |= hideTitlebar();
        if (m_state.useTabs())
            client_move |= setTabMode(EXTERNAL);
    }

    if (m_state.useHandle())
        client_move |= showHandle();
    else
        client_move |= hideHandle();

    // apply gravity once more
    gravityTranslate(grav_x, grav_y, sizeHints().win_gravity, m_active_orig_client_bw,
                     false);

    // if the location changes, shift it
    if (do_move && (grav_x != 0 || grav_y != 0)) {
        move(grav_x + x(), grav_y + y());
        client_move = true;
    }

    if (do_move) {
        reconfigure();
        m_state.saveGeometry(x(), y(), width(), height());
    }
    if (client_move)
        frameExtentSig().emit();
}

bool FbWinFrame::setBorderWidth(bool do_move) {
    unsigned int border_width = theme()->border().width();
    unsigned int win_bw = m_state.useBorder() ? border_width : 0;

    if (border_width &&
        theme()->border().color().pixel() != window().borderColor()) {
        FbTk::Color c = theme()->border().color();
        window().setBorderColor(c);
        titlebar().setBorderColor(c);
        handle().setBorderColor(c);
        gripLeft().setBorderColor(c);
        gripRight().setBorderColor(c);
        tabcontainer().setBorderColor(c);
    }

    if (border_width == handle().borderWidth() &&
        win_bw == window().borderWidth())
        return false;

    int grav_x=0, grav_y=0;
    // negate gravity
    if (do_move)
        gravityTranslate(grav_x, grav_y, -sizeHints().win_gravity,
                         m_active_orig_client_bw, false);

    int bw_changes = 0;
    // we need to change the size of the window
    // if the border width changes...
    if (m_use_titlebar)
        bw_changes += static_cast<signed>(border_width - titlebar().borderWidth());
    if (m_use_handle)
        bw_changes += static_cast<signed>(border_width - handle().borderWidth());

    window().setBorderWidth(win_bw);

    setTabMode(NOTSET);

    titlebar().setBorderWidth(border_width);
    handle().setBorderWidth(border_width);
    gripLeft().setBorderWidth(border_width);
    gripRight().setBorderWidth(border_width);

    if (bw_changes != 0)
        resize(width(), height() + bw_changes);

    if (m_tabmode == EXTERNAL)
        alignTabs();

    if (do_move) {
        frameExtentSig().emit();
        gravityTranslate(grav_x, grav_y, sizeHints().win_gravity,
                         m_active_orig_client_bw, false);
        // if the location changes, shift it
        if (grav_x != 0 || grav_y != 0)
            move(grav_x + x(), grav_y + y());
    }

    return true;
}

// this function translates its arguments according to win_gravity
// if win_gravity is negative, it does an inverse translation
// This function should be used when a window is mapped/unmapped/pos configured
void FbWinFrame::gravityTranslate(int &x, int &y,
                                  int win_gravity, unsigned int client_bw, bool move_frame) {
    bool invert = false;
    if (win_gravity < 0) {
        invert = true;
        win_gravity = -win_gravity; // make +ve
    }

    /* Ok, so, gravity says which point of the frame is put where the
     * corresponding bit of window would have been
     * Thus, x,y always refers to where top left of the WINDOW would be placed
     * but given that we're wrapping it in a frame, we actually place
     * it so that the given reference point is in the same spot as the
     * window's reference point would have been.
     * i.e. east gravity says that the centre of the right hand side of the
     * frame is placed where the centre of the rhs of the window would
     * have been if there was no frame.
     * Hope that makes enough sense.
     *
     * NOTE: the gravity calculations are INDEPENDENT of the client
     *       window width/height.
     *
     * If you get confused with the calculations, draw a picture.
     *
     */

    // We calculate offsets based on the gravity and frame aspects
    // and at the end apply those offsets +ve or -ve depending on 'invert'

    // These will be set to the resulting offsets for adjusting the frame position
    int x_offset = 0;
    int y_offset = 0;

    // These are the amount that the frame is larger than the client window
    // Note that the client window's x,y is offset by it's borderWidth, which
    // is removed by fluxbox, so the gravity needs to account for this change

    // these functions already check if the title/handle is used
    int bw = static_cast<int>(m_window.borderWidth());
    int bw_diff = static_cast<int>(client_bw) - bw;
    int height_diff = 2*bw_diff - static_cast<int>(titlebarHeight()) - static_cast<int>(handleHeight());
    int width_diff = 2*bw_diff;

    if (win_gravity == SouthWestGravity || win_gravity == SouthGravity ||
        win_gravity == SouthEastGravity)
        y_offset = height_diff;

    if (win_gravity == WestGravity || win_gravity == CenterGravity ||
        win_gravity == EastGravity)
        y_offset = height_diff/2;

    if (win_gravity == NorthEastGravity || win_gravity == EastGravity ||
        win_gravity == SouthEastGravity)
        x_offset = width_diff;

    if (win_gravity == NorthGravity || win_gravity == CenterGravity ||
        win_gravity == SouthGravity)
        x_offset = width_diff/2;

    if (win_gravity == StaticGravity) {
        x_offset = bw_diff;
        y_offset = bw_diff - titlebarHeight();
    }

    if (invert) {
        x_offset = -x_offset;
        y_offset = -y_offset;
    }

    x += x_offset;
    y += y_offset;

    if (move_frame && (x_offset != 0 || y_offset != 0)) {
        move(x, y);
    }
}

int FbWinFrame::widthOffset() const {
    if (m_tabmode != EXTERNAL || !m_use_tabs)
        return 0;
    if (s_place[m_screen.getTabPlacement()].is_horizontal) {
        return 0;
    }
    return m_tab_container.width() + m_window.borderWidth();
}

int FbWinFrame::heightOffset() const {
    if (m_tabmode != EXTERNAL || !m_use_tabs)
        return 0;

    if (!s_place[m_screen.getTabPlacement()].is_horizontal) {
        return 0;
    }
    return m_tab_container.height() + m_window.borderWidth();
}

int FbWinFrame::xOffset() const {
    if (m_tabmode != EXTERNAL || !m_use_tabs)
        return 0;
    TabPlacement p = m_screen.getTabPlacement();
    if (p == LEFTTOP || p == LEFT || p == LEFTBOTTOM) {
        return m_tab_container.width() + m_window.borderWidth();
    }
    return 0;
}

int FbWinFrame::yOffset() const {
    if (m_tabmode != EXTERNAL || !m_use_tabs)
        return 0;
    TabPlacement p = m_screen.getTabPlacement();
    if (p == TOPLEFT || p == TOP || p == TOPRIGHT) {
        return m_tab_container.height() + m_window.borderWidth();
    }
    return 0;
}

void FbWinFrame::applySizeHints(unsigned int &width, unsigned int &height,
                                bool maximizing) const {
    const int h = height - titlebarHeight() - handleHeight();
    height = max(h, static_cast<int>(titlebarHeight() + handleHeight()));
    sizeHints().apply(width, height, maximizing);
    height += titlebarHeight() + handleHeight();
}

void FbWinFrame::displaySize(unsigned int width, unsigned int height) const {
    unsigned int i, j;
    sizeHints().displaySize(i, j,
                            width, height - titlebarHeight() - handleHeight());
    m_screen.showGeometry(i, j);
}
