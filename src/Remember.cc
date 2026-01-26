// Remember.cc for Fluxbox Window Manager
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
#include "RememberApp.hh"
#include "ClientPattern.hh"
#include "Screen.hh"
#include "Window.hh"
#include "WinClient.hh"
#include "FbMenu.hh"
#include "MenuCreator.hh"
#include "FbCommands.hh"
#include "fluxbox.hh"
#include "Layer.hh"
#include "Debug.hh"

#include "FbTk/I18n.hh"
#include "FbTk/FbString.hh"
#include "FbTk/StringUtil.hh"
#include "FbTk/FileUtil.hh"
#include "FbTk/MenuItem.hh"
#include "FbTk/App.hh"
#include "FbTk/stringstream.hh"
#include "FbTk/Transparent.hh"
#include "FbTk/AutoReloadHelper.hh"
#include "FbTk/RefCount.hh"
#include "FbTk/Util.hh"

#include <cstring>
#include <set>


using std::cerr;
using std::endl;
using std::string;
using std::list;
using std::set;
using std::make_pair;
using std::ifstream;
using std::ofstream;
using std::hex;
using std::dec;

using FbTk::StringUtil::getStringBetween;
using FbTk::StringUtil::removeFirstWhitespace;
using FbTk::StringUtil::removeTrailingWhitespace;
using FbTk::StringUtil::toLower;
using FbTk::StringUtil::toLower;
using FbTk::StringUtil::extractNumber;
using FbTk::StringUtil::expandFilename;

/*------------------------------------------------------------------*\
\*------------------------------------------------------------------*/
Application::Application(bool transient, bool grouped, ClientPattern *pat):
    is_transient(transient), is_grouped(grouped), group_pattern(pat)
{
    reset();
}

void Application::reset() {
    decostate_remember =
        dimensions_remember =
        focushiddenstate_remember =
        iconhiddenstate_remember =
        jumpworkspace_remember =
        layer_remember  =
        position_remember =
        shadedstate_remember =
        stuckstate_remember =
        focusprotection_remember =
        tabstate_remember =
        workspace_remember =
        head_remember =
        alpha_remember =
        minimizedstate_remember =
        maximizedstate_remember =
        fullscreenstate_remember =
        save_on_close_remember = false;
}

/*------------------------------------------------------------------*\
\*------------------------------------------------------------------*/

namespace {

// replace special chars like ( ) and [ ] with \( \) and \[ \]
string escapeRememberChars(const string& str) {
    if (str.empty())
        return str;

    string escaped_str;
    escaped_str.reserve(str.capacity());

    string::const_iterator i;
    for (i = str.begin(); i != str.end(); ++i) {
        switch (*i) {
            case '(': case ')': case '[': case ']':
                escaped_str += '\\';
            default:
                escaped_str += *i;
                break;
        }
    }

    return escaped_str;
}

} // end anonymous namespace

/*------------------------------------------------------------------*\
\*------------------------------------------------------------------*/

Remember *Remember::s_instance = 0;

Remember::Remember():
    m_pats(new Patterns()),
    m_reloader(new FbTk::AutoReloadHelper()) {

    setName("remember");

    if (s_instance != 0)
        throw string("Can not create more than one instance of Remember");

    s_instance = this;
    enableUpdate();

    m_reloader->setReloadCmd(FbTk::RefCount<FbTk::Command<void> >(new FbTk::SimpleCommand<Remember>(*this, &Remember::reload)));
    reconfigure();
}

Remember::~Remember() {

    // free our resources

    // the patterns free the "Application"s
    // the client mapping shouldn't need cleaning
    Patterns::iterator it;
    set<Application *> all_apps; // no duplicates
    while (!m_pats->empty()) {
        it = m_pats->begin();
        delete it->first; // ClientPattern
        all_apps.insert(it->second); // Application, not necessarily unique
        m_pats->erase(it);
    }

    set<Application *>::iterator ait = all_apps.begin(); // no duplicates
    for (; ait != all_apps.end(); ++ait) {
        delete (*ait);
    }

    delete(m_reloader);

    s_instance = 0;
}

Application* Remember::find(WinClient &winclient) {
    // if it is already associated with a application, return that one
    // otherwise, check it against every pattern that we've got
    Clients::iterator wc_it = m_clients.find(&winclient);
    if (wc_it != m_clients.end())
        return wc_it->second;
    else {
        Patterns::iterator it = m_pats->begin();
        for (; it != m_pats->end(); ++it)
            if (it->first->match(winclient) &&
                it->second->is_transient == winclient.isTransient()) {
                it->first->addMatch();
                m_clients[&winclient] = it->second;
                return it->second;
            }
    }
    // oh well, no matches
    return 0;
}

Application * Remember::add(WinClient &winclient) {
    ClientPattern *p = new ClientPattern();
    Application *app = new Application(winclient.isTransient(), false);

    // by default, we match against the WMClass of a window (instance and class strings)
    string win_name  = ::escapeRememberChars(p->getProperty(ClientPattern::NAME,  winclient));
    string win_class = ::escapeRememberChars(p->getProperty(ClientPattern::CLASS, winclient));
    string win_role  = ::escapeRememberChars(p->getProperty(ClientPattern::ROLE,  winclient));

    p->addTerm(win_name,  ClientPattern::NAME);
    p->addTerm(win_class, ClientPattern::CLASS);
    if (!win_role.empty())
        p->addTerm(win_role, ClientPattern::ROLE);
    m_clients[&winclient] = app;
    p->addMatch();
    m_pats->push_back(make_pair(p, app));
    return app;
}




void Remember::reconfigure() {
    m_reloader->setMainFile(Fluxbox::instance()->getAppsFilename());
}

void Remember::checkReload() {
    m_reloader->checkReload();
}
bool Remember::isRemembered(WinClient &winclient, Attribute attrib) {
    Application *app = find(winclient);
    if (!app) return false;
    switch (attrib) {
    case REM_WORKSPACE:
        return app->workspace_remember;
        break;
    case REM_HEAD:
        return app->head_remember;
        break;
    case REM_DIMENSIONS:
        return app->dimensions_remember;
        break;
    case REM_IGNORE_SIZEHINTS:
        return app->ignoreSizeHints_remember;
        break;
    case REM_POSITION:
        return app->position_remember;
        break;
    case REM_FOCUSHIDDENSTATE:
        return app->focushiddenstate_remember;
        break;
    case REM_ICONHIDDENSTATE:
        return app->iconhiddenstate_remember;
        break;
    case REM_STUCKSTATE:
        return app->stuckstate_remember;
        break;
    case REM_FOCUSPROTECTION:
        return app->focusprotection_remember;
        break;
    case REM_MINIMIZEDSTATE:
        return app->minimizedstate_remember;
        break;
    case REM_MAXIMIZEDSTATE:
        return app->maximizedstate_remember;
        break;
    case REM_FULLSCREENSTATE:
        return app->fullscreenstate_remember;
        break;
    case REM_DECOSTATE:
        return app->decostate_remember;
        break;
    case REM_SHADEDSTATE:
        return app->shadedstate_remember;
        break;
        //    case REM_TABSTATE:
        //        return app->tabstate_remember;
        //        break;
    case REM_JUMPWORKSPACE:
        return app->jumpworkspace_remember;
        break;
    case REM_LAYER:
        return app->layer_remember;
        break;
    case REM_SAVEONCLOSE:
        return app->save_on_close_remember;
        break;
    case REM_ALPHA:
        return app->alpha_remember;
    case REM_LASTATTRIB:
    default:
        return false; // should never get here
    }
}

void Remember::rememberAttrib(WinClient &winclient, Attribute attrib) {
    FluxboxWindow *win = winclient.fbwindow();
    if (!win) return;
    Application *app = find(winclient);
    if (!app) {
        app = add(winclient);
        if (!app) return;
    }
    int head, percx, percy;
    switch (attrib) {
    case REM_WORKSPACE:
        app->rememberWorkspace(win->workspaceNumber());
        break;
    case REM_HEAD:
        app->rememberHead(win->screen().getHead(win->fbWindow()));
        break;
    case REM_DIMENSIONS: {
        head = win->screen().getHead(win->fbWindow());
        percx = win->screen().calRelativeDimensionWidth(head, win->normalWidth());
        percy = win->screen().calRelativeDimensionHeight(head, win->normalHeight());
        app->rememberDimensions(percx, percy, true, true);
        break;
    }
    case REM_POSITION: {
        head = win->screen().getHead(win->fbWindow());
        percx = win->screen().calRelativePositionWidth(head, win->normalX());
        percy = win->screen().calRelativePositionHeight(head, win->normalY());
        app->rememberPosition(percx, percy, true, true);
        break;
    }
    case REM_FOCUSHIDDENSTATE:
        app->rememberFocusHiddenstate(win->isFocusHidden());
        break;
    case REM_ICONHIDDENSTATE:
        app->rememberIconHiddenstate(win->isIconHidden());
        break;
    case REM_SHADEDSTATE:
        app->rememberShadedstate(win->isShaded());
        break;
    case REM_DECOSTATE:
        app->rememberDecostate(win->decorationMask());
        break;
    case REM_STUCKSTATE:
        app->rememberStuckstate(win->isStuck());
        break;
    case REM_FOCUSPROTECTION:
        app->rememberFocusProtection(win->focusProtection());
        break;
    case REM_MINIMIZEDSTATE:
        app->rememberMinimizedstate(win->isIconic());
        break;
    case REM_MAXIMIZEDSTATE:
        app->rememberMaximizedstate(win->maximizedState());
        break;
    case REM_FULLSCREENSTATE:
        app->rememberFullscreenstate(win->isFullscreen());
        break;
    case REM_ALPHA:
        app->rememberAlpha(win->frame().getAlpha(true), win->frame().getAlpha(false));
        break;
        //    case REM_TABSTATE:
        //        break;
    case REM_JUMPWORKSPACE:
        app->rememberJumpworkspace(true);
        break;
    case REM_LAYER:
        app->rememberLayer(win->layerNum());
        break;
    case REM_SAVEONCLOSE:
        app->rememberSaveOnClose(true);
        break;
    case REM_LASTATTRIB:
    default:
        // nothing
        break;
    }
}

void Remember::forgetAttrib(WinClient &winclient, Attribute attrib) {
    FluxboxWindow *win = winclient.fbwindow();
    if (!win) return;
    Application *app = find(winclient);
    if (!app) {
        app = add(winclient);
        if (!app) return;
    }
    switch (attrib) {
    case REM_WORKSPACE:
        app->forgetWorkspace();
        break;
    case REM_HEAD:
        app->forgetHead();
        break;
    case REM_DIMENSIONS:
        app->forgetDimensions();
        break;
    case REM_IGNORE_SIZEHINTS:
        app->ignoreSizeHints_remember = false;
        break;
    case REM_POSITION:
        app->forgetPosition();
        break;
    case REM_FOCUSHIDDENSTATE:
        app->forgetFocusHiddenstate();
        break;
    case REM_ICONHIDDENSTATE:
        app->forgetIconHiddenstate();
        break;
    case REM_STUCKSTATE:
        app->forgetStuckstate();
        break;
    case REM_FOCUSPROTECTION:
        app->forgetFocusProtection();
        break;
    case REM_MINIMIZEDSTATE:
        app->forgetMinimizedstate();
        break;
    case REM_MAXIMIZEDSTATE:
        app->forgetMaximizedstate();
        break;
    case REM_FULLSCREENSTATE:
        app->forgetFullscreenstate();
        break;
    case REM_DECOSTATE:
        app->forgetDecostate();
        break;
    case REM_SHADEDSTATE:
        app->forgetShadedstate();
        break;
    case REM_ALPHA:
        app->forgetAlpha();
        break;
//    case REM_TABSTATE:
//        break;
    case REM_JUMPWORKSPACE:
        app->forgetJumpworkspace();
        break;
    case REM_LAYER:
        app->forgetLayer();
        break;
    case REM_SAVEONCLOSE:
        app->forgetSaveOnClose();
        break;
    case REM_LASTATTRIB:
    default:
        // nothing
        break;
    }
}

void Remember::setupFrame(FluxboxWindow &win) {
    WinClient &winclient = win.winClient();
    Application *app = find(winclient);
    if (app == 0)
        return; // nothing to do

    // first, set the options that aren't preserved as window properties on
    // restart, then return if fluxbox is restarting -- we want restart to
    // disturb the current window state as little as possible

    if (app->focushiddenstate_remember)
        win.setFocusHidden(app->focushiddenstate);
    if (app->iconhiddenstate_remember)
        win.setIconHidden(app->iconhiddenstate);
    if (app->layer_remember)
        win.moveToLayer(app->layer);
    if (app->decostate_remember)
        win.setDecorationMask(app->decostate);

    if (app->alpha_remember) {
        win.frame().setDefaultAlpha();
        win.frame().setAlpha(true,app->focused_alpha);
        win.frame().setAlpha(false,app->unfocused_alpha);
    }

    BScreen &screen = winclient.screen();

    // now check if fluxbox is restarting
    if (screen.isRestart())
        return;

    if (app->workspace_remember) {
        // we use setWorkspace and not reassoc because we're still initialising
        win.setWorkspace(app->workspace);
        if (app->jumpworkspace_remember && app->jumpworkspace)
            screen.changeWorkspaceID(app->workspace);
    }

    if (app->head_remember) {
        win.setOnHead(app->head);
    }

    if (app->dimensions_remember) {

        int win_w = app->w;
        int win_h = app->h;
        int head = screen.getHead(win.fbWindow());
        int border_w = win.frame().window().borderWidth();

        if (app->dimension_is_w_relative)
            win_w = screen.calRelativeWidth(head, win_w);
        if (app->dimension_is_h_relative)
            win_h = screen.calRelativeHeight(head, win_h);

        win.resize(win_w - 2 * border_w, win_h - 2 * border_w);
    }

    if (app->position_remember) {

        int newx = app->x;
        int newy = app->y;
        int head = screen.getHead(win.fbWindow());

        if (app->position_is_x_relative)
            newx = screen.calRelativeWidth(head, newx);
        if (app->position_is_y_relative)
            newy = screen.calRelativeHeight(head, newy);

        win.translateCoords(newx, newy, app->refc);
        win.move(newx, newy);
    }

    if (app->shadedstate_remember)
        // if inconsistent...
        if ((win.isShaded() && !app->shadedstate) ||
            (!win.isShaded() && app->shadedstate))
            win.shade(); // toggles

    // external tabs aren't available atm...
    //if (app->tabstate_remember) ...

    if (app->stuckstate_remember)
        // if inconsistent...
        if ((win.isStuck() && !app->stuckstate) ||
            (!win.isStuck() && app->stuckstate))
            win.stick(); // toggles

    if (app->focusprotection_remember) {
        win.setFocusProtection(app->focusprotection);
    }

    if (app->minimizedstate_remember) {
        // if inconsistent...
        // this one doesn't actually work, but I can't imagine needing it
        if (win.isIconic() && !app->minimizedstate)
            win.deiconify();
        else if (!win.isIconic() && app->minimizedstate)
            win.iconify();
    }

    // I can't really test the "no" case of this
    if (app->maximizedstate_remember)
        win.setMaximizedState(app->maximizedstate);

    // I can't really test the "no" case of this
    if (app->fullscreenstate_remember)
        win.setFullscreen(app->fullscreenstate);
}

void Remember::setupClient(WinClient &winclient) {

    // leave windows alone on restart
    if (winclient.screen().isRestart())
        return;

    // check if apps file has changed
    checkReload();

    Application *app = find(winclient);
    if (app == 0)
        return; // nothing to do

    FluxboxWindow *group;
    if (winclient.fbwindow() == 0 && app->is_grouped &&
        (group = findGroup(app, winclient.screen()))) {
        group->attachClient(winclient);
        if (app->jumpworkspace_remember && app->jumpworkspace)
            // jump to window, not saved workspace
            winclient.screen().changeWorkspaceID(group->workspaceNumber());
    }
}

FluxboxWindow *Remember::findGroup(Application *app, BScreen &screen) {
    if (!app || !app->is_grouped)
        return 0;

    // find the first client associated with the app and return its fbwindow
    Clients::iterator it = m_clients.begin();
    Clients::iterator it_end = m_clients.end();
    for (; it != it_end; ++it) {
        if (it->second == app && it->first->fbwindow() &&
            &screen == &it->first->screen() &&
            (!app->group_pattern || app->group_pattern->match(*it->first)))
            return it->first->fbwindow();
    }

    // there weren't any open, but that's ok
    return 0;
}

void Remember::updateDecoStateFromClient(WinClient& winclient) {

    Application* app= find(winclient);

    if ( app && isRemembered(winclient, REM_DECOSTATE)) {
        winclient.fbwindow()->setDecorationMask(app->decostate);
    }
}

void Remember::updateClientClose(WinClient &winclient) {
    checkReload(); // reload if it's changed
    Application *app = find(winclient);

    if (app) {
        Patterns::iterator it = m_pats->begin();
        for (; it != m_pats->end(); ++it) {
            if (it->second == app) {
                it->first->removeMatch();
                break;
            }
        }
    }

    if (app && (app->save_on_close_remember && app->save_on_close)) {

        for (int attrib = 0; attrib < REM_LASTATTRIB; attrib++) {
            if (isRemembered(winclient, (Attribute) attrib)) {
                rememberAttrib(winclient, (Attribute) attrib);
            }
        }

        save();
    }

    // we need to get rid of references to this client
    Clients::iterator wc_it = m_clients.find(&winclient);

    if (wc_it != m_clients.end()) {
        m_clients.erase(wc_it);
    }

}

void Remember::initForScreen(BScreen &screen) { }
