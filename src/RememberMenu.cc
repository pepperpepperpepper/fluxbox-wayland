// RememberMenu.cc for Fluxbox Window Manager
// Copyright (c) 2003 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//                     and Simon Bowden    (rathnor at users.sourceforge.net)
// Copyright (c) 2002 Xavier Brouckaert
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

#include "Remember.hh"

#include "FbMenu.hh"
#include "MenuCreator.hh"

#include "FbTk/I18n.hh"
#include "FbTk/MenuItem.hh"
#include "FbTk/Transparent.hh"

namespace {

class RememberMenuItem : public FbTk::MenuItem {
public:
    RememberMenuItem(const FbTk::BiDiString &label,
                     const Remember::Attribute attrib) :
        FbTk::MenuItem(label),
        m_attrib(attrib) {
        setToggleItem(true);
        setCloseOnClick(false);
    }

    bool isSelected() const {
        if (FbMenu::window() == 0)
            return false;

        if (FbMenu::window()->numClients()) // ensure it HAS clients
            return Remember::instance().isRemembered(FbMenu::window()->winClient(), m_attrib);
        return false;
    }

    bool isEnabled() const {
        if (FbMenu::window() == 0)
            return false;

        if (m_attrib != Remember::REM_JUMPWORKSPACE)
            return true;
        else if (FbMenu::window()->numClients())
            return (Remember::instance().isRemembered(FbMenu::window()->winClient(), Remember::REM_WORKSPACE));
        return false;
    }

    void click(int button, int time, unsigned int mods) {
        // reconfigure only does stuff if the apps file has changed
        Remember& r = Remember::instance();
        r.checkReload();
        if (FbMenu::window() != 0) {
            WinClient& wc = FbMenu::window()->winClient();
            if (isSelected()) {
                r.forgetAttrib(wc, m_attrib);
            } else {
                r.rememberAttrib(wc, m_attrib);
            }
        }
        r.save();
        FbTk::MenuItem::click(button, time, mods);
    }

private:
    Remember::Attribute m_attrib;
};

FbTk::Menu *createRememberMenu(BScreen &screen) {

    _FB_USES_NLS;

    static const struct { bool is_alpha; const FbTk::BiDiString label; Remember::Attribute attr; } _entries[] = {
        { false, _FB_XTEXT(Remember, Workspace, "Workspace", "Remember Workspace"), Remember::REM_WORKSPACE },
        { false, _FB_XTEXT(Remember, JumpToWorkspace, "Jump to workspace", "Change active workspace to remembered one on open"), Remember::REM_JUMPWORKSPACE },
        { false, _FB_XTEXT(Remember, Head, "Head", "Remember Head"), Remember::REM_HEAD},
        { false, _FB_XTEXT(Remember, Dimensions, "Dimensions", "Remember Dimensions - with width and height"), Remember::REM_DIMENSIONS},
        { false, _FB_XTEXT(Remember, Position, "Position", "Remember position - window co-ordinates"), Remember::REM_POSITION},
        { false, _FB_XTEXT(Remember, Sticky, "Sticky", "Remember Sticky"), Remember::REM_STUCKSTATE},
        { false, _FB_XTEXT(Remember, Decorations, "Decorations", "Remember window decorations"), Remember::REM_DECOSTATE},
        { false, _FB_XTEXT(Remember, Shaded, "Shaded", "Remember shaded"), Remember::REM_SHADEDSTATE},
        { false, _FB_XTEXT(Remember, Minimized, "Minimized", "Remember minimized"), Remember::REM_MINIMIZEDSTATE},
        { false, _FB_XTEXT(Remember, Maximized, "Maximized", "Remember maximized"), Remember::REM_MAXIMIZEDSTATE},
        { false, _FB_XTEXT(Remember, Fullscreen, "Fullscreen", "Remember fullscreen"), Remember::REM_FULLSCREENSTATE},
        { true,  _FB_XTEXT(Remember, Alpha, "Transparency", "Remember window tranparency settings"), Remember::REM_ALPHA},
        { false, _FB_XTEXT(Remember, Layer, "Layer", "Remember Layer"), Remember::REM_LAYER},
        { false, _FB_XTEXT(Remember, SaveOnClose, "Save on close", "Save remembered attributes on close"), Remember::REM_SAVEONCLOSE}
    };
    bool needs_alpha = (FbTk::Transparent::haveComposite() || FbTk::Transparent::haveRender());

    // each fluxboxwindow has its own windowmenu
    // so we also create a remember menu just for it...
    FbTk::Menu *menu = MenuCreator::createMenu("Remember", screen);
    size_t i;
    for (i = 0; i < sizeof(_entries)/sizeof(_entries[0]); i++) {
        if (_entries[i].is_alpha && !needs_alpha) { // skip alpha-entry when not needed
            continue;
        }
        menu->insertItem(new RememberMenuItem(_entries[i].label, _entries[i].attr));
    }

    menu->updateMenu();
    return menu;
}

} // end anonymous namespace

FbTk::Menu* Remember::createMenu(BScreen& screen) {
    return createRememberMenu(screen);
}

