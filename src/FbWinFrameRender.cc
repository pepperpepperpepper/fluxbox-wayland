// FbWinFrameRender.cc for Fluxbox Window Manager
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

#include "FbWinFrameTheme.hh"
#include "Screen.hh"
#include "FocusableTheme.hh"
#include "IconButton.hh"

#include "FbTk/ImageControl.hh"
#include "FbTk/Transparent.hh"
#include "FbTk/TextUtils.hh"

#include <X11/X.h>

#include <algorithm>

namespace {

enum { UNFOCUS = 0, FOCUS, PRESSED };

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

/// renders to pixmap or sets color
void render(FbTk::Color &col, Pixmap &pm, unsigned int width, unsigned int height,
            const FbTk::Texture &tex,
            FbTk::ImageControl& ictl,
            FbTk::Orientation orient = FbTk::ROT0) {

    Pixmap tmp = pm;
    if (!tex.usePixmap()) {
        pm = None;
        col = tex.color();
    } else {
        pm = ictl.renderImage(width, height, tex, orient);
    }

    if (tmp)
        ictl.removeImage(tmp);

}

void bg_pm_or_color(FbTk::FbWindow& win, const Pixmap& pm, const FbTk::Color& color) {
    if (pm) {
        win.setBackgroundPixmap(pm);
    } else {
        win.setBackgroundColor(color);
    }
}

} // end anonymous

void FbWinFrame::reconfigure() {
    if (m_tab_container.empty())
        return;

    int grav_x=0, grav_y=0;
    // negate gravity
    gravityTranslate(grav_x, grav_y, -sizeHints().win_gravity, m_active_orig_client_bw, false);

    m_bevel = theme()->bevelWidth();

    unsigned int orig_handle_h = handle().height();
    if (m_use_handle && orig_handle_h != theme()->handleWidth())
        m_window.resize(m_window.width(), m_window.height() -
                        orig_handle_h + theme()->handleWidth());

    handle().resize(handle().width(), theme()->handleWidth());
    gripLeft().resize(buttonHeight(), theme()->handleWidth());
    gripRight().resize(gripLeft().width(), gripLeft().height());

    // align titlebar and render it
    if (m_use_titlebar) {
        reconfigureTitlebar();
        m_titlebar.raise();
    } else
        m_titlebar.lower();

    if (m_tabmode == EXTERNAL) {
        unsigned int h = buttonHeight();
        unsigned int w = m_tab_container.width();
        if (!s_place[m_screen.getTabPlacement()].is_horizontal) {
            w = m_tab_container.height();
            std::swap(w, h);
        }
        m_tab_container.resize(w, h);
        alignTabs();
    }

    // leave client+grips alone if we're shaded (it'll get fixed when we unshade)
    if (!m_state.shaded || m_state.fullscreen) {
        int client_top = 0;
        int client_height = m_window.height();
        if (m_use_titlebar) {
            // only one borderwidth as titlebar is really at -borderwidth
            int titlebar_height = m_titlebar.height() + m_titlebar.borderWidth();
            client_top += titlebar_height;
            client_height -= titlebar_height;
        }

        // align handle and grips
        const int grip_height = m_handle.height();
        const int grip_width = 20; //TODO
        const int handle_bw = static_cast<signed>(m_handle.borderWidth());

        int ypos = m_window.height();

        // if the handle isn't on, it's actually below the window
        if (m_use_handle)
            ypos -= grip_height + handle_bw;

        // we do handle settings whether on or not so that if they get toggled
        // then things are ok...
        m_handle.invalidateBackground();
        m_handle.moveResize(-handle_bw, ypos,
                            m_window.width(), grip_height);

        m_grip_left.invalidateBackground();
        m_grip_left.moveResize(-handle_bw, -handle_bw,
                               grip_width, grip_height);

        m_grip_right.invalidateBackground();
        m_grip_right.moveResize(m_handle.width() - grip_width - handle_bw, -handle_bw,
                                grip_width, grip_height);

        if (m_use_handle) {
            m_handle.raise();
            client_height -= m_handle.height() + m_handle.borderWidth();
        } else {
            m_handle.lower();
        }

        m_clientarea.moveResize(0, client_top,
                                m_window.width(), client_height);
    }

    gravityTranslate(grav_x, grav_y, sizeHints().win_gravity, m_active_orig_client_bw, false);
    // if the location changes, shift it
    if (grav_x != 0 || grav_y != 0)
        move(grav_x + x(), grav_y + y());

    // render the theme
    if (isVisible()) {
        // update transparency settings
        if (FbTk::Transparent::haveRender()) {
            int alpha = getAlpha(m_state.focused);
            int opaque = 255;
            if (FbTk::Transparent::haveComposite()) {
                std::swap(alpha, opaque);
            }
            m_tab_container.setAlpha(alpha);
            m_window.setOpaque(opaque);
        }
        renderAll();
        applyAll();
        clearAll();
    } else {
        m_need_render = true;
    }

    m_shape.setPlaces(getShape());
    m_shape.setShapeOffsets(0, titlebarHeight());

    // titlebar stuff rendered already by reconftitlebar
}

//--------------------- private area

/**
   aligns and redraws title
*/
void FbWinFrame::redrawTitlebar() {
    if (!m_use_titlebar || m_tab_container.empty())
        return;

    if (isVisible()) {
        m_tab_container.clear();
        m_label.clear();
        m_titlebar.clear();
    }
}

/**
   Align buttons with title text window
*/
void FbWinFrame::reconfigureTitlebar() {
    if (!m_use_titlebar)
        return;

    int orig_height = m_titlebar.height();
    // resize titlebar to window size with font height
    int title_height = theme()->font().height() == 0 ? 16 :
        theme()->font().height() + m_bevel*2 + 2;
    if (theme()->titleHeight() != 0)
        title_height = theme()->titleHeight();

    // if the titlebar grows in size, make sure the whole window does too
    if (orig_height != title_height)
        m_window.resize(m_window.width(), m_window.height()-orig_height+title_height);
    m_titlebar.invalidateBackground();
    m_titlebar.moveResize(-m_titlebar.borderWidth(), -m_titlebar.borderWidth(),
                          m_window.width(), title_height);

    // draw left buttons first
    unsigned int next_x = m_bevel;
    unsigned int button_size = buttonHeight();
    m_button_size = button_size;
    for (size_t i=0; i < m_buttons_left.size(); i++, next_x += button_size + m_bevel) {
        // probably on theme reconfigure, leave bg alone for now
        m_buttons_left[i]->invalidateBackground();
        m_buttons_left[i]->moveResize(next_x, m_bevel,
                                      button_size, button_size);
    }

    next_x += m_bevel;

    // space left on titlebar between left and right buttons
    int space_left = m_titlebar.width() - next_x;

    if (!m_buttons_right.empty())
        space_left -= m_buttons_right.size() * (button_size + m_bevel);

    space_left -= m_bevel;

    if (space_left <= 0)
        space_left = 1;

    m_label.invalidateBackground();
    m_label.moveResize(next_x, m_bevel, space_left, button_size);

    m_tab_container.invalidateBackground();
    if (m_tabmode == INTERNAL)
        m_tab_container.moveResize(next_x, m_bevel, space_left, button_size);
    else {
        if (m_use_tabs) {
            if (m_tab_container.orientation() == FbTk::ROT0) {
                m_tab_container.resize(m_tab_container.width(), button_size);
            } else {
                m_tab_container.resize(button_size, m_tab_container.height());
            }
        }
    }

    next_x += m_label.width() + m_bevel;

    // finaly set new buttons to the right
    for (size_t i=0; i < m_buttons_right.size();
         ++i, next_x += button_size + m_bevel) {
        m_buttons_right[i]->invalidateBackground();
        m_buttons_right[i]->moveResize(next_x, m_bevel,
                                       button_size, button_size);
    }

    m_titlebar.raise(); // always on top
}

void FbWinFrame::renderAll() {
    m_need_render = false;

    renderTitlebar();
    renderHandles();
    renderTabContainer();
}

void FbWinFrame::applyAll() {
    applyTitlebar();
    applyHandles();
    applyTabContainer();
}

void FbWinFrame::renderTitlebar() {
    if (!m_use_titlebar)
        return;

    if (!isVisible()) {
        m_need_render = true;
        return;
    }

    typedef FbTk::ThemeProxy<FbWinFrameTheme> TP;
    TP& ft = theme().focusedTheme();
    TP& uft = theme().unfocusedTheme();

    // render pixmaps
    render(m_title_face.color[FOCUS], m_title_face.pm[FOCUS], m_titlebar.width(), m_titlebar.height(),
           ft->titleTexture(), m_imagectrl);

    render(m_title_face.color[UNFOCUS], m_title_face.pm[UNFOCUS], m_titlebar.width(), m_titlebar.height(),
           uft->titleTexture(), m_imagectrl);

    //!! TODO: don't render label if internal tabs

    render(m_label_face.color[FOCUS], m_label_face.pm[FOCUS], m_label.width(), m_label.height(),
           ft->iconbarTheme()->texture(), m_imagectrl);

    render(m_label_face.color[UNFOCUS], m_label_face.pm[UNFOCUS], m_label.width(), m_label.height(),
           uft->iconbarTheme()->texture(), m_imagectrl);
}

void FbWinFrame::renderTabContainer() {
    if (!isVisible()) {
        m_need_render = true;
        return;
    }

    typedef FbTk::ThemeProxy<FbWinFrameTheme> TP;
    TP& ft = theme().focusedTheme();
    TP& uft = theme().unfocusedTheme();
    FbTk::Container& tabs = tabcontainer();
    const FbTk::Texture *tc_focused = &ft->iconbarTheme()->texture();
    const FbTk::Texture *tc_unfocused = &uft->iconbarTheme()->texture();

    if (m_tabmode == EXTERNAL && tc_focused->type() & FbTk::Texture::PARENTRELATIVE)
        tc_focused = &ft->titleTexture();
    if (m_tabmode == EXTERNAL && tc_unfocused->type() & FbTk::Texture::PARENTRELATIVE)
        tc_unfocused = &uft->titleTexture();

    render(m_tabcontainer_face.color[FOCUS], m_tabcontainer_face.pm[FOCUS],
           tabs.width(), tabs.height(), *tc_focused, m_imagectrl, tabs.orientation());

    render(m_tabcontainer_face.color[UNFOCUS], m_tabcontainer_face.pm[UNFOCUS],
           tabs.width(), tabs.height(), *tc_unfocused, m_imagectrl, tabs.orientation());

    renderButtons();

}

void FbWinFrame::applyTitlebar() {

    int f = m_state.focused;
    int alpha = getAlpha(f);
    m_titlebar.setAlpha(alpha);
    m_label.setAlpha(alpha);

    if (m_tabmode != INTERNAL) {
        m_label.setGC(theme()->iconbarTheme()->text().textGC());
        m_label.setJustify(theme()->iconbarTheme()->text().justify());

        bg_pm_or_color(m_label, m_label_face.pm[f], m_label_face.color[f]);
    }

    bg_pm_or_color(m_titlebar, m_title_face.pm[f], m_title_face.color[f]);
    applyButtons();
}


void FbWinFrame::renderHandles() {
    if (!m_use_handle)
        return;

    if (!isVisible()) {
        m_need_render = true;
        return;
    }

    typedef FbTk::ThemeProxy<FbWinFrameTheme> TP;
    TP& ft = theme().focusedTheme();
    TP& uft = theme().unfocusedTheme();

    render(m_handle_face.color[FOCUS], m_handle_face.pm[FOCUS],
           m_handle.width(), m_handle.height(),
           ft->handleTexture(), m_imagectrl);

    render(m_handle_face.color[UNFOCUS], m_handle_face.pm[UNFOCUS],
           m_handle.width(), m_handle.height(),
           uft->handleTexture(), m_imagectrl);

    render(m_grip_face.color[FOCUS], m_grip_face.pm[FOCUS],
           m_grip_left.width(), m_grip_left.height(),
           ft->gripTexture(), m_imagectrl);

    render(m_grip_face.color[UNFOCUS], m_grip_face.pm[UNFOCUS],
           m_grip_left.width(), m_grip_left.height(),
           uft->gripTexture(), m_imagectrl);
}

void FbWinFrame::applyHandles() {

    bool f = m_state.focused;
    int alpha = getAlpha(f);

    m_handle.setAlpha(alpha);
    bg_pm_or_color(m_handle, m_handle_face.pm[f], m_handle_face.color[f]);

    m_grip_left.setAlpha(alpha);
    m_grip_right.setAlpha(alpha);

    bg_pm_or_color(m_grip_left, m_grip_face.pm[f], m_grip_face.color[f]);
    bg_pm_or_color(m_grip_right, m_grip_face.pm[f], m_grip_face.color[f]);
}

void FbWinFrame::renderButtons() {

    if (!isVisible()) {
        m_need_render = true;
        return;
    }

    typedef FbTk::ThemeProxy<FbWinFrameTheme> TP;
    TP& ft = theme().focusedTheme();
    TP& uft = theme().unfocusedTheme();

    render(m_button_face.color[UNFOCUS], m_button_face.pm[UNFOCUS],
           m_button_size, m_button_size,
           uft->buttonTexture(), m_imagectrl);

    render(m_button_face.color[FOCUS], m_button_face.pm[FOCUS],
           m_button_size, m_button_size,
           ft->buttonTexture(), m_imagectrl);

    render(m_button_face.color[PRESSED], m_button_face.pm[PRESSED],
           m_button_size, m_button_size,
           theme()->buttonPressedTexture(), m_imagectrl);

}

void FbWinFrame::applyButtons() {
    // setup left and right buttons
    for (size_t i=0; i < m_buttons_left.size(); ++i)
        applyButton(*m_buttons_left[i]);

    for (size_t i=0; i < m_buttons_right.size(); ++i)
        applyButton(*m_buttons_right[i]);
}

/**
   Setups upp background, pressed pixmap/color of the button to current theme
*/
void FbWinFrame::applyButton(FbTk::Button &btn) {

    FbWinFrame::BtnFace& face = m_button_face;

    if (m_button_face.pm[PRESSED]) {
        btn.setPressedPixmap(face.pm[PRESSED]);
    } else {
        btn.setPressedColor(face.color[PRESSED]);
    }

    bool f = m_state.focused;

    btn.setAlpha(getAlpha(f));
    btn.setGC(theme()->buttonPicGC());

    bg_pm_or_color(btn, face.pm[f], face.color[f]);
}


void FbWinFrame::applyTabContainer() {

    FbTk::Container& tabs = tabcontainer();
    FbWinFrame::Face& face = m_tabcontainer_face;

    tabs.setAlpha(getAlpha(m_state.focused));
    bg_pm_or_color(tabs, face.pm[m_state.focused], face.color[m_state.focused]);

    // and the labelbuttons in it
    FbTk::Container::ItemList::iterator btn_it = m_tab_container.begin();
    FbTk::Container::ItemList::iterator btn_it_end = m_tab_container.end();
    for (; btn_it != btn_it_end; ++btn_it) {
        IconButton *btn = static_cast<IconButton *>(*btn_it);
        btn->reconfigTheme();
    }
}
