// Screen.cc for Fluxbox Window Manager
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
#include "WindowCmd.hh"
#include "Workspace.hh"

#include "Layer.hh"
#include "FocusControl.hh"
#include "ScreenPlacement.hh"

// menus
#include "ConfigMenu.hh"
#include "FbMenu.hh"
#include "LayerMenu.hh"

#include "MenuCreator.hh"

#include "WinClient.hh"
#include "FbWinFrame.hh"
#include "Strut.hh"
#include "FbTk/CommandParser.hh"
#include "AtomHandler.hh"
#include "HeadArea.hh"
#include "RectangleUtil.hh"
#include "FbCommands.hh"
#ifdef USE_SYSTRAY
#include "SystemTray.hh"
#endif
#include "Debug.hh"

#include "FbTk/I18n.hh"
#include "FbTk/FbWindow.hh"
#include "FbTk/SimpleCommand.hh"
#include "FbTk/MultLayers.hh"
#include "FbTk/LayerItem.hh"
#include "FbTk/MacroCommand.hh"
#include "FbTk/StringUtil.hh"
#include "FbTk/ImageControl.hh"
#include "FbTk/EventManager.hh"
#include "FbTk/Transparent.hh"
#include "FbTk/Select2nd.hh"
#include "FbTk/Compose.hh"
#include "FbTk/FbString.hh"
#include "FbTk/STLUtil.hh"
#include "FbTk/KeyUtil.hh"
#include "FbTk/Util.hh"

#ifdef USE_SLIT
#include "Slit.hh"
#include "SlitClient.hh"
#else
// fill it in
class Slit {};
#endif // USE_SLIT

#ifdef USE_TOOLBAR
#include "Toolbar.hh"
#else
class Toolbar {};
#endif // USE_TOOLBAR

#ifdef STDC_HEADERS
#include <sys/types.h>
#endif // STDC_HEADERS

#ifdef HAVE_UNISTD_H
#include <sys/types.h>
#include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else // !TIME_WITH_SYS_TIME
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else // !HAVE_SYS_TIME_H
#include <time.h>
#endif // HAVE_SYS_TIME_H
#endif // TIME_WITH_SYS_TIME

#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#ifdef XINERAMA
extern  "C" {
#include <X11/extensions/Xinerama.h>
}
#endif // XINERAMA

#if defined(HAVE_RANDR) || defined(HAVE_RANDR1_2)
#include <X11/extensions/Xrandr.h>
#endif // HAVE_RANDR

#include <iostream>
#include <algorithm>
#include <functional>
#include <stack>
#include <cstdarg>
#include <cstring>

using std::cerr;
using std::endl;
using std::string;
using std::make_pair;
using std::pair;
using std::list;
using std::vector;
using std::mem_fn;
using std::equal_to;

using std::hex;
using std::dec;

using namespace std::placeholders;

static bool running = true;
namespace {

int anotherWMRunning(Display *display, XErrorEvent *) {
    _FB_USES_NLS;
    cerr<<_FB_CONSOLETEXT(Screen, AnotherWMRunning,
                  "BScreen::BScreen: an error occured while querying the X server.\n"
                  "	another window manager already running on display ",
                  "Message when another WM is found already active on all screens")
        <<DisplayString(display)<<endl;

    running = false;

    return -1;
}


void clampMenuDelay(int& delay) {
    delay = FbTk::Util::clamp(delay, 0, 5000);
}


Atom atom_fbcmd = 0;
Atom atom_wm_check = 0;
Atom atom_net_desktop = 0;
Atom atom_utf8_string = 0;
Atom atom_kde_systray = 0;
Atom atom_kwm1 = 0;

void initAtoms(Display* dpy) {
    atom_wm_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    atom_net_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    atom_fbcmd = XInternAtom(dpy, "_FLUXBOX_ACTION", False);
    atom_utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    atom_kde_systray = XInternAtom(dpy, "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR", False);
    atom_kwm1 = XInternAtom(dpy, "KWM_DOCKWINDOW", False);
}

} // end anonymous namespace




BScreen::BScreen(FbTk::ResourceManager &rm,
                 const string &screenname,
                 const string &altscreenname,
                 int scrn, int num_layers,
                 unsigned int opts) :
    m_layermanager(num_layers),
    root_colormap_installed(false),
    m_current_workspace(0),
    m_former_workspace(0),
    m_focused_windowtheme(new FbWinFrameTheme(scrn, ".focus", ".Focus")),
    m_unfocused_windowtheme(new FbWinFrameTheme(scrn, ".unfocus", ".Unfocus")),
    // the order of windowtheme and winbutton theme is important
    // because winbutton need to rescale the pixmaps in winbutton theme
    // after fbwinframe have resized them
    m_focused_winbutton_theme(new WinButtonTheme(scrn, "", "", *m_focused_windowtheme)),
    m_unfocused_winbutton_theme(new WinButtonTheme(scrn, ".unfocus", ".Unfocus", *m_unfocused_windowtheme)),
    m_pressed_winbutton_theme(new WinButtonTheme(scrn, ".pressed", ".Pressed", *m_focused_windowtheme)),
    m_menutheme(new FbTk::MenuTheme(scrn)),
    m_root_window(scrn),
    m_geom_window(new OSDWindow(m_root_window, *this, *m_focused_windowtheme)),
    m_pos_window(new OSDWindow(m_root_window, *this, *m_focused_windowtheme)),
    m_tooltip_window(new TooltipWindow(m_root_window, *this, *m_focused_windowtheme)),
    m_dummy_window(scrn, -1, -1, 1, 1, 0, true, false, CopyFromParent, InputOnly),
    resource(rm, screenname, altscreenname),
    m_resource_manager(rm),
    m_name(screenname),
    m_altname(altscreenname),
    m_focus_control(new FocusControl(*this)),
    m_placement_strategy(new ScreenPlacement(*this)),
    m_opts(opts) {


    m_state.cycling = false;
    m_state.restart = false;
    m_state.shutdown = false;
    m_state.managed = false;

    Fluxbox *fluxbox = Fluxbox::instance();
    Display *disp = fluxbox->display();

    initAtoms(disp);

    // Create the first one, initXinerama will expand this if needed.
    m_head_areas.resize(1);
    m_head_areas[0] = new HeadArea();

    initXinerama();

    // setup error handler to catch "screen already managed by other wm"
    XErrorHandler old = XSetErrorHandler((XErrorHandler) anotherWMRunning);

    rootWindow().setEventMask(ColormapChangeMask | EnterWindowMask | PropertyChangeMask |
                              SubstructureRedirectMask | KeyPressMask | KeyReleaseMask |
                              ButtonPressMask | ButtonReleaseMask| SubstructureNotifyMask);

    fluxbox->sync(false);

    XSetErrorHandler((XErrorHandler) old);

    m_state.managed = running;
    if (!m_state.managed) {
        delete m_placement_strategy; m_placement_strategy = 0;
        delete m_focus_control; m_focus_control = 0;
        return;
    }

    // we're going to manage the screen, so now add our pid
#ifdef HAVE_GETPID
    unsigned long bpid = static_cast<unsigned long>(getpid());

    rootWindow().changeProperty(fluxbox->getFluxboxPidAtom(), XA_CARDINAL,
                                sizeof(pid_t) * 8, PropModeReplace,
                                (unsigned char *) &bpid, 1);
#endif // HAVE_GETPID

    // check if we're the first EWMH compliant window manager on this screen
    union { Atom atom; unsigned long ul; int i; } ignore;
    unsigned char *ret_prop;
    if (rootWindow().property(atom_wm_check, 0l, 1l,
            False, XA_WINDOW, &ignore.atom, &ignore.i, &ignore.ul,
            &ignore.ul, &ret_prop) ) {
        m_state.restart = (ret_prop != NULL);
        XFree(ret_prop);
    }


// setup RANDR for this screens root window
#if defined(HAVE_RANDR)
    int randr_mask = RRScreenChangeNotifyMask;
# ifdef RRCrtcChangeNotifyMask
    randr_mask |= RRCrtcChangeNotifyMask;
# endif
# ifdef RROutputChangeNotifyMask
    randr_mask |= RROutputChangeNotifyMask;
# endif
# ifdef RROutputPropertyNotifyMask
    randr_mask |= RROutputPropertyNotifyMask;
# endif
    XRRSelectInput(disp, rootWindow().window(), randr_mask);
#endif // HAVE_RANDR


    _FB_USES_NLS;

#ifdef DEBUG
    fprintf(stderr, _FB_CONSOLETEXT(Screen, ManagingScreen,
                            "BScreen::BScreen: managing screen %d "
                            "using visual 0x%lx, depth %d\n",
                            "informational message saying screen number (%d), visual (%lx), and colour depth (%d)").c_str(),
            screenNumber(), XVisualIDFromVisual(rootWindow().visual()),
            rootWindow().maxDepth());
#endif // DEBUG

    FbTk::EventManager *evm = FbTk::EventManager::instance();
    evm->add(*this, rootWindow());
    Keys *keys = fluxbox->keys();
    if (keys)
        keys->registerWindow(rootWindow().window(), *this,
                             Keys::GLOBAL|Keys::ON_DESKTOP);
    rootWindow().setCursor(XCreateFontCursor(disp, XC_left_ptr));

    // load this screens resources
    fluxbox->load_rc(*this);

    // setup image cache engine
    m_image_control.reset(new FbTk::ImageControl(scrn,
                                                 fluxbox->colorsPerChannel(),
                                                 fluxbox->getCacheLife(), fluxbox->getCacheMax()));
    imageControl().installRootColormap();
    root_colormap_installed = true;

    m_root_theme.reset(new RootTheme(imageControl()));
    m_root_theme->reconfigTheme();

    focusedWinFrameTheme()->setAlpha(*resource.focused_alpha);
    unfocusedWinFrameTheme()->setAlpha(*resource.unfocused_alpha);
    m_menutheme->setAlpha(*resource.menu_alpha);

    clampMenuDelay(*resource.menu_delay);

    m_menutheme->setDelay(*resource.menu_delay);

    m_tracker.join(focusedWinFrameTheme()->reconfigSig(),
            FbTk::MemFun(*this, &BScreen::focusedWinFrameThemeReconfigured));


    renderGeomWindow();
    renderPosWindow();
    m_tooltip_window->setDelay(*resource.tooltip_delay);

    // setup workspaces and workspace menu
    int nr_ws = *resource.workspaces;
    addWorkspace(); // at least one
    for (int i = 1; i < nr_ws; ++i) {
        addWorkspace();
    }

    m_current_workspace = m_workspaces_list.front();

    m_windowmenu.reset(MenuCreator::createMenu("", *this));
    m_windowmenu->setInternalMenu();
    m_windowmenu->setReloadHelper(new FbTk::AutoReloadHelper());
    m_windowmenu->reloadHelper()->setReloadCmd(FbTk::RefCount<FbTk::Command<void> >(new FbTk::SimpleCommand<BScreen>(*this, &BScreen::rereadWindowMenu)));

    m_rootmenu.reset(MenuCreator::createMenu("", *this));
    m_rootmenu->setReloadHelper(new FbTk::AutoReloadHelper());
    m_rootmenu->reloadHelper()->setReloadCmd(FbTk::RefCount<FbTk::Command<void> >(new FbTk::SimpleCommand<BScreen>(*this, &BScreen::rereadMenu)));

    m_configmenu.reset(MenuCreator::createMenu(_FB_XTEXT(Menu, Configuration,
                                  "Configuration", "Title of configuration menu"), *this));
    m_configmenu->setInternalMenu();
    setupConfigmenu(*m_configmenu.get());

    // check which desktop we should start on
    int first_desktop = 0;
    if (m_state.restart) {
        bool exists;
        int ret = (rootWindow().cardinalProperty(atom_net_desktop, &exists));
        if (exists) {
            first_desktop = FbTk::Util::clamp<int>(ret, 0, nr_ws);
        }
    }

    changeWorkspaceID(first_desktop);

#ifdef USE_SLIT
    if (opts & Fluxbox::OPT_SLIT) {
        Slit* slit = new Slit(*this, *layerManager().getLayer(ResourceLayer::DESKTOP), fluxbox->getSlitlistFilename().c_str());
        m_slit.reset(slit);
    }
#endif // USE_SLIT

    rm.unlock();

    XFlush(disp);
}



BScreen::~BScreen() {

    if (!m_state.managed)
        return;

    m_toolbar.reset(0);
    m_toolButtonMap.reset(0);

    FbTk::EventManager *evm = FbTk::EventManager::instance();
    evm->remove(rootWindow());

    Keys *keys = Fluxbox::instance()->keys();
    if (keys)
        keys->unregisterWindow(rootWindow().window());

    if (m_rootmenu.get() != 0)
        m_rootmenu->removeAll();

    // Since workspacemenu holds client list menus (from workspace)
    // we need to destroy it before we destroy workspaces
    m_workspacemenu.reset(0);

    removeWorkspaceNames();
    using namespace FbTk::STLUtil;
    destroyAndClear(m_workspaces_list);
    destroyAndClear(m_managed_resources);

    //why not destroyAndClear(m_icon_list); ?
    //problem with that: a delete FluxboxWindow* calls m_diesig.notify()
    //which leads to screen.removeWindow() which leads to removeIcon(win)
    //which would modify the m_icon_list anyways...
    Icons tmp;
    tmp = m_icon_list;
    while(!tmp.empty()) {
        removeWindow(tmp.back());
        tmp.back()->restore(true);
        delete (tmp.back());
        tmp.pop_back();
    }

    if (hasXinerama()) {
        m_xinerama.heads.clear();
    }

    // slit must be destroyed before headAreas (Struts)
    m_slit.reset(0);

    m_windowmenu.reset(0);
    m_rootmenu.reset(0);
    m_workspacemenu.reset(0);
    m_configmenu.reset(0);

    // TODO fluxgen: check if this is the right place
    for (size_t i = 0; i < m_head_areas.size(); i++)
        delete m_head_areas[i];

    delete m_focus_control;
    delete m_placement_strategy;

}

bool BScreen::isRestart() {
    return Fluxbox::instance()->isStartup() && m_state.restart;
}

void BScreen::initWindows() {

#ifdef USE_TOOLBAR
    if (m_opts & Fluxbox::OPT_TOOLBAR) {
        m_toolButtonMap.reset(new ToolButtonMap());
        Toolbar* tb = new Toolbar(*this, *layerManager().getLayer(::ResourceLayer::NORMAL));
        m_toolbar.reset(tb);
    }
#endif // USE_TOOLBAR

    unsigned int nchild;
    Window r, p, *children;
    Fluxbox* fluxbox = Fluxbox::instance();
    Display* disp = fluxbox->display();

    XQueryTree(disp, rootWindow().window(), &r, &p, &children, &nchild);

    // preen the window list of all icon windows... for better dockapp support
    for (unsigned int i = 0; i < nchild; i++) {

        if (children[i] == None)
            continue;

        XWMHints *wmhints = XGetWMHints(disp, children[i]);

        if (wmhints) {
            if ((wmhints->flags & IconWindowHint) &&
                (wmhints->icon_window != children[i]))
                for (unsigned int j = 0; j < nchild; j++) {
                    if (children[j] == wmhints->icon_window) {

                        fbdbg<<"BScreen::initWindows(): children[j] = 0x"<<hex<<children[j]<<dec<<endl;
                        fbdbg<<"BScreen::initWindows(): = icon_window"<<endl;

                        children[j] = None;
                        break;
                    }
                }
            XFree(wmhints);
        }
    }


    // manage shown windows
    Window transient_for = 0;
    bool safety_flag = false;
    unsigned int num_transients = 0;
    for (unsigned int i = 0; i <= nchild; ++i) {
        if (i == nchild) {
            if (num_transients) {
                if (num_transients == nchild)
                    safety_flag = true;
                nchild = num_transients;
                i = num_transients = 0;
            } else
                break;
        }

        if (children[i] == None)
            continue;
        else if (!fluxbox->validateWindow(children[i])) {

            fbdbg<<"BScreen::initWindows(): not valid window = "<<hex<<children[i]<<dec<<endl;

            children[i] = None;
            continue;
        }

        // if we have a transient_for window and it isn't created yet...
        // postpone creation of this window until after all others
        if (XGetTransientForHint(disp, children[i], &transient_for) &&
            fluxbox->searchWindow(transient_for) == 0 && !safety_flag) {
            // add this window back to the beginning of the list of children
            children[num_transients] = children[i];
            num_transients++;

            fbdbg<<"BScreen::initWindows(): postpone creation of 0x"<<hex<<children[i]<<dec<<endl;
            fbdbg<<"BScreen::initWindows(): transient_for = 0x"<<hex<<transient_for<<dec<<endl;

            continue;
        }


        XWindowAttributes attrib;
        if (XGetWindowAttributes(disp, children[i],
                                 &attrib)) {
            if (attrib.override_redirect) {
                children[i] = None; // we dont need this anymore, since we already created a window for it
                continue;
            }

            if (attrib.map_state != IsUnmapped)
                createWindow(children[i]);

        }
        children[i] = None; // we dont need this anymore, since we already created a window for it
    }

    XFree(children);

    // now, show slit and toolbar
#ifdef USE_SLIT
    if (slit())
        slit()->show();
#endif // USE_SLIT

}

void BScreen::focusedWinFrameThemeReconfigured() {
    renderGeomWindow();
    renderPosWindow();

    Fluxbox *fluxbox = Fluxbox::instance();
    const std::list<Focusable *> winlist =
            focusControl().focusedOrderWinList().clientList();
    std::list<Focusable *>::const_iterator it = winlist.begin();
    std::list<Focusable *>::const_iterator it_end = winlist.end();
    for (; it != it_end; ++it)
        fluxbox->updateFrameExtents(*(*it)->fbwindow());

}

void BScreen::propertyNotify(Atom atom) {

    if (allowRemoteActions() && atom == atom_fbcmd) {
        Atom xa_ret_type;
        int ret_format;
        unsigned long ret_nitems, ret_bytes_after;
        char *str;
        if (rootWindow().property(atom_fbcmd, 0l, 64l,
                True, XA_STRING, &xa_ret_type, &ret_format, &ret_nitems,
                &ret_bytes_after, (unsigned char **)&str) && str) {

            if (ret_bytes_after) {
                XFree(str);
                long len = 64 + (ret_bytes_after + 3)/4;
                rootWindow().property(atom_fbcmd, 0l, len,
                    True, XA_STRING, &xa_ret_type, &ret_format, &ret_nitems,
                    &ret_bytes_after, (unsigned char **)&str);
            }

            static std::unique_ptr<FbTk::Command<void> > cmd;
            cmd.reset(FbTk::CommandParser<void>::instance().parse(str, false));
            if (cmd.get()) {
                cmd->execute();
            }
            XFree(str);

        }
    // TODO: this doesn't belong in FbPixmap
    } else if (FbTk::FbPixmap::rootwinPropertyNotify(screenNumber(), atom))
        m_bg_change_sig.emit(*this);
}

void BScreen::keyPressEvent(XKeyEvent &ke) {
    if (Fluxbox::instance()->keys()->doAction(ke.type, ke.state, ke.keycode,
                Keys::GLOBAL|(ke.subwindow ? 0 : Keys::ON_DESKTOP), 0, ke.time)) {

        // re-grab keyboard, so we don't pass KeyRelease to clients
        // also for catching invalid keys in the middle of keychains
        FbTk::EventManager::instance()->grabKeyboard(rootWindow().window());
        XAllowEvents(Fluxbox::instance()->display(), SyncKeyboard, CurrentTime);
    } else {
        XAllowEvents(Fluxbox::instance()->display(), ReplayKeyboard, CurrentTime);
    }
}

void BScreen::keyReleaseEvent(XKeyEvent &ke) {
    if (m_state.cycling) {

        unsigned int state = FbTk::KeyUtil::instance().cleanMods(ke.state);
        state &= ~FbTk::KeyUtil::instance().keycodeToModmask(ke.keycode);

        if (state) // still cycling
            return;

        m_state.cycling = false;
        focusControl().stopCyclingFocus();
    }
    if (!Fluxbox::instance()->keys()->inKeychain())
        FbTk::EventManager::instance()->ungrabKeyboard();
}

void BScreen::buttonPressEvent(XButtonEvent &be) {
    if (be.button == 1 && !isRootColormapInstalled())
        imageControl().installRootColormap();

    Keys *keys = Fluxbox::instance()->keys();
    if (keys->doAction(be.type, be.state, be.button, Keys::GLOBAL|Keys::ON_DESKTOP,
                   0, be.time)) {
        XAllowEvents(Fluxbox::instance()->display(), SyncPointer, CurrentTime);
    } else {
        XAllowEvents(Fluxbox::instance()->display(), ReplayPointer, CurrentTime);
    }
}

void BScreen::cycleFocus(int options, const ClientPattern *pat, bool reverse) {
    // get modifiers from event that causes this for focus order cycling
    XEvent ev = Fluxbox::instance()->lastEvent();
    unsigned int mods = 0;
    if (ev.type == KeyPress)
        mods = FbTk::KeyUtil::instance().cleanMods(ev.xkey.state);
    else if (ev.type == ButtonPress)
        mods = FbTk::KeyUtil::instance().cleanMods(ev.xbutton.state);

    if (!m_state.cycling && mods) {
        m_state.cycling = true;
        FbTk::EventManager::instance()->grabKeyboard(rootWindow().window());
    }

    if (mods == 0) // can't stacked cycle unless there is a mod to grab
        options |= FocusableList::STATIC_ORDER;

    const FocusableList *win_list =
        FocusableList::getListFromOptions(*this, options);
    focusControl().cycleFocus(*win_list, pat, reverse);

}

void BScreen::reconfigure() {
    Fluxbox *fluxbox = Fluxbox::instance();

    focusedWinFrameTheme()->setAlpha(*resource.focused_alpha);
    unfocusedWinFrameTheme()->setAlpha(*resource.unfocused_alpha);
    m_menutheme->setAlpha(*resource.menu_alpha);

    clampMenuDelay(*resource.menu_delay);

    m_menutheme->setDelay(*resource.menu_delay);

    // provide the number of workspaces from the init-file
    const unsigned int nr_ws = *resource.workspaces;
    if (nr_ws > m_workspaces_list.size()) {
        while(nr_ws != m_workspaces_list.size()) {
            addWorkspace();
        }
    } else if (nr_ws < m_workspaces_list.size()) {
        while(nr_ws != m_workspaces_list.size()) {
            removeLastWorkspace();
        }
    }

    // update menu filenames
    m_rootmenu->reloadHelper()->setMainFile(fluxbox->getMenuFilename());
    m_windowmenu->reloadHelper()->setMainFile(windowMenuFilename());

    // reconfigure workspaces
    for_each(m_workspaces_list.begin(),
             m_workspaces_list.end(),
             mem_fn(&Workspace::reconfigure));

    // reconfigure Icons
    for_each(m_icon_list.begin(),
             m_icon_list.end(),
             mem_fn(&FluxboxWindow::reconfigure));

    imageControl().cleanCache();
    // notify objects that the screen is reconfigured
    m_reconfigure_sig.emit(*this);

    // Reload style
    FbTk::ThemeManager::instance().load(fluxbox->getStyleFilename(),
                                        fluxbox->getStyleOverlayFilename(),
                                        m_root_theme->screenNum());

    reconfigureTabs();
    reconfigureStruts();
}

void BScreen::reconfigureTabs() {
    const std::list<Focusable *> winlist =
            focusControl().focusedOrderWinList().clientList();
    std::list<Focusable *>::const_iterator it = winlist.begin(),
                                           it_end = winlist.end();
    for (; it != it_end; ++it)
        (*it)->fbwindow()->applyDecorations();
}

void BScreen::addIcon(FluxboxWindow *w) {
    if (w == 0)
        return;

    // make sure we have a unique list
    if (find(iconList().begin(), iconList().end(), w) != iconList().end())
        return;

    iconList().push_back(w);

    // notify listeners
    iconListSig().emit(*this);
}


void BScreen::removeIcon(FluxboxWindow *w) {
    if (w == 0)
        return;

    Icons::iterator erase_it = find_if(iconList().begin(),
                                       iconList().end(),
                                       std::bind(equal_to<FluxboxWindow *>(), _1, w));
    // no need to send iconlist signal if we didn't
    // change the iconlist
    if (erase_it != m_icon_list.end()) {
        iconList().erase(erase_it);
        iconListSig().emit(*this);
    }
}

void BScreen::removeWindow(FluxboxWindow *win) {

    fbdbg<<"BScreen::removeWindow("<<win<<")"<<endl;

    // extra precaution, if for some reason, the
    // icon list should be out of sync
    removeIcon(win);
    // remove from workspace
    Workspace *space = getWorkspace(win->workspaceNumber());
    if (space != 0)
        space->removeWindow(win, false);
}


void BScreen::removeClient(WinClient &client) {

    focusControl().removeClient(client);

    if (client.fbwindow() && client.fbwindow()->isIconic())
        iconListSig().emit(*this);

    using namespace FbTk;

    // remove any grouping this is expecting
    Groupables::iterator erase_it = find_if(m_expecting_groups.begin(),
                                            m_expecting_groups.end(),
                                            Compose(std::bind(equal_to<WinClient *>(), _1, &client),
                                                    Select2nd<Groupables::value_type>()));

    if (erase_it != m_expecting_groups.end())
        m_expecting_groups.erase(erase_it);

    // the client could be on icon menu so we update it
    //!! TODO: check this with the new icon menu
    //    updateIconMenu();

}

bool BScreen::isKdeDockapp(Window client) const {
    //Check and see if client is KDE dock applet.
    bool iskdedockapp = false;
    Atom ajunk;
    int ijunk;
    unsigned long *data = 0, uljunk;
    Display *disp = FbTk::App::instance()->display();
    // Check if KDE v2.x dock applet
    if (XGetWindowProperty(disp, client, atom_kde_systray,
                           0l, 1l, False,
                           XA_WINDOW, &ajunk, &ijunk, &uljunk,
                           &uljunk, (unsigned char **) &data) == Success) {

        if (data)
            iskdedockapp = true;
        XFree((void *) data);
        data = 0;
    }

    // Check if KDE v1.x dock applet
    if (!iskdedockapp) {
        if (XGetWindowProperty(disp, client,
                               atom_kwm1, 0l, 1l, False,
                               atom_kwm1, &ajunk, &ijunk, &uljunk,
                               &uljunk, (unsigned char **) &data) == Success && data) {
            iskdedockapp = (data && data[0] != 0);
            XFree((void *) data);
            data = 0;
        }
    }

    return iskdedockapp;
}

bool BScreen::addKdeDockapp(Window client) {

    XSelectInput(FbTk::App::instance()->display(), client, StructureNotifyMask);
    FbTk::EventHandler *evh  = 0;
    FbTk::EventManager *evm = FbTk::EventManager::instance();

    AtomHandler* handler = 0;
#if USE_SYSTRAY
    handler = Fluxbox::instance()->getAtomHandler(SystemTray::getNetSystemTrayAtom(screenNumber()));
#endif
    if (handler == 0) {
#ifdef USE_SLIT
        if (slit() != 0 && slit()->acceptKdeDockapp())
            slit()->addClient(client);
        else
#endif // USE_SLIT
            return false;
    } else {
        // this handler is a special case
        // so we call setupClient in it
        WinClient winclient(client, *this);
        handler->setupClient(winclient);
        // we need to save old handler and re-add it later
        evh = evm->find(client);
    }

    if (evh != 0) // re-add handler
        evm->add(*evh, client);

    return true;
}

FluxboxWindow *BScreen::createWindow(Window client) {

    Fluxbox* fluxbox = Fluxbox::instance();
    fluxbox->sync(false);

    if (isKdeDockapp(client) && addKdeDockapp(client)) {
        return 0; // dont create a FluxboxWindow for this one
    }

    WinClient *winclient = new WinClient(client, *this);

    if (winclient->initial_state == WithdrawnState ||
        winclient->getWMClassClass() == "DockApp") {
        delete winclient;
#ifdef USE_SLIT
        if (slit() && !isKdeDockapp(client))
            slit()->addClient(client);
#endif // USE_SLIT
        return 0;
    }

    // check if it should be grouped with something else
    WinClient*      other = findGroupLeft(*winclient);
    if (!other && m_placement_strategy->placementPolicy() == ScreenPlacement::AUTOTABPLACEMENT)
        other = FocusControl::focusedWindow();
    FluxboxWindow*  win = other ? other->fbwindow() : 0;

    if (other && win) {
        win->attachClient(*winclient);
        fluxbox->attachSignals(*winclient);
    } else {
        fluxbox->attachSignals(*winclient);
        if (winclient->fbwindow()) { // may have been set in an atomhandler
            win = winclient->fbwindow();
            Workspace *workspace = getWorkspace(win->workspaceNumber());
            if (workspace)
                workspace->updateClientmenu();
        } else {
            win = new FluxboxWindow(*winclient);

            if (!win->isManaged()) {
                delete win;
                return 0;
            }
        }
    }

    // add the window to the focus list
    // always add to front on startup to keep the focus order the same
    if (win->isFocused() || fluxbox->isStartup())
        focusControl().addFocusFront(*winclient);
    else
        focusControl().addFocusBack(*winclient);

    // we also need to check if another window expects this window to the left
    // and if so, then join it.
    if ((other = findGroupRight(*winclient)) && other->fbwindow() != win)
        win->attachClient(*other);
    else if (other) // should never happen
        win->moveClientRightOf(*other, *winclient);

    m_clientlist_sig.emit(*this);

    fluxbox->sync(false);
    return win;
}


FluxboxWindow *BScreen::createWindow(WinClient &client) {

    if (isKdeDockapp(client.window()) && addKdeDockapp(client.window())) {
        return 0;
    }

    FluxboxWindow *win = new FluxboxWindow(client);

#ifdef SLIT
    if (slit() != 0) {

        if (win->initialState() == WithdrawnState) {
            slit()->addClient(client.window());
        } else if (client->getWMClassClass() == "DockApp") {
            slit()->addClient(client.window());
        }
    }
#endif // SLIT


    if (!win->isManaged()) {
        delete win;
        return 0;
    }

    win->show();
    // don't ask me why, but client doesn't seem to keep focus in new window
    // and we don't seem to get a FocusIn event from setInputFocus
    if ((focusControl().focusNew() || FocusControl::focusedWindow() == &client)
            && win->focus())
        FocusControl::setFocusedWindow(&client);

    m_clientlist_sig.emit(*this);

    return win;
}

#if USE_TOOLBAR

void BScreen::clearToolButtonMap() {
    m_toolButtonMap->clear();
}

void BScreen::mapToolButton(std::string name, FbTk::TextButton *button) {
    m_toolButtonMap->insert(std::pair<std::string, FbTk::TextButton*>(name, button));
}

bool BScreen::relabelToolButton(std::string button, std::string label) {
    ToolButtonMap::const_iterator it = m_toolButtonMap->find(button);
    if (it != m_toolButtonMap->end() && it->second) {
        it->second->setText(label);
        m_toolbar->relayout();
        return true;
    }
    return false;
}

#endif

void BScreen::initMenus() {
    m_workspacemenu.reset(MenuCreator::createMenuType("workspacemenu", screenNumber()));
    m_rootmenu->reloadHelper()->setMainFile(Fluxbox::instance()->getMenuFilename());
    m_windowmenu->reloadHelper()->setMainFile(windowMenuFilename());
}


void BScreen::rereadMenu() {

    m_rootmenu->removeAll();
    m_rootmenu->setLabel(FbTk::BiDiString(""));

    Fluxbox * const fb = Fluxbox::instance();
    if (!fb->getMenuFilename().empty())
        MenuCreator::createFromFile(fb->getMenuFilename(), *m_rootmenu,
                                    m_rootmenu->reloadHelper());

    if (m_rootmenu->numberOfItems() == 0) {
        _FB_USES_NLS;
        m_rootmenu->setLabel(_FB_XTEXT(Menu, DefaultRootMenu, "Fluxbox default menu", "Title of fallback root menu"));
        FbTk::RefCount<FbTk::Command<void> > restart_fb(FbTk::CommandParser<void>::instance().parse("restart"));
        FbTk::RefCount<FbTk::Command<void> > exit_fb(FbTk::CommandParser<void>::instance().parse("exit"));
        FbTk::RefCount<FbTk::Command<void> > execute_xterm(FbTk::CommandParser<void>::instance().parse("exec xterm"));
        m_rootmenu->setInternalMenu();
        m_rootmenu->insertCommand("xterm", execute_xterm);
        m_rootmenu->insertCommand(_FB_XTEXT(Menu, Restart, "Restart", "Restart command"),
                           restart_fb);
        m_rootmenu->insertCommand(_FB_XTEXT(Menu, Exit, "Exit", "Exit command"),
                           exit_fb);
    }

}

const std::string BScreen::windowMenuFilename() const {
    std::string name = *resource.windowmenufile;
    if (name.empty()) {
        name = Fluxbox::instance()->getDefaultDataFilename("windowmenu");
    }
    return name;
}

void BScreen::rereadWindowMenu() {

    m_windowmenu->removeAll();
    if (!windowMenuFilename().empty())
        MenuCreator::createFromFile(windowMenuFilename(), *m_windowmenu,
                                    m_windowmenu->reloadHelper());

}

void BScreen::addConfigMenu(const FbTk::FbString &label, FbTk::Menu &menu) {

    FbTk::Menu* cm = m_configmenu.get();
    if (cm) {
        int pos = cm->findSubmenuIndex(&menu);
        if (pos == -1) { // not found? add
            cm->insertSubmenu(label, &menu, pos);
        }
    }
}

void BScreen::removeConfigMenu(FbTk::Menu &menu) {

    FbTk::Menu* cm = m_configmenu.get();
    if (cm) {
        int pos = cm->findSubmenuIndex(&menu);
        if (pos > -1) {
            cm->remove(pos);
        }
    }
}


void BScreen::addManagedResource(FbTk::Resource_base *resource) {
    m_managed_resources.push_back(resource);
}

void BScreen::setupConfigmenu(FbTk::Menu &menu) {

    struct ConfigMenu::SetupHelper sh(*this, m_resource_manager, resource);
    menu.removeAll();
    ConfigMenu::setup(menu, sh);
    menu.updateMenu();
}


void BScreen::shutdown() {
    rootWindow().setEventMask(NoEventMask);
    FbTk::App::instance()->sync(false);
    m_state.shutdown = true;
    m_focus_control->shutdown();
    for_each(m_workspaces_list.begin(),
             m_workspaces_list.end(),
             mem_fn(&Workspace::shutdown));
}


void BScreen::showPosition(int x, int y) {
    if (!doShowWindowPos())
        return;

    char buf[256];
    snprintf(buf, sizeof(buf), "X:%5d x Y:%5d", x, y);

    FbTk::BiDiString label(buf);
    m_pos_window->showText(label);
}


void BScreen::hidePosition() {
    m_pos_window->hide();
}

void BScreen::showGeometry(unsigned int gx, unsigned int gy) {
    if (!doShowWindowPos())
        return;

    char buf[256];
    _FB_USES_NLS;

    snprintf(buf, sizeof(buf),
            _FB_XTEXT(Screen, GeometryFormat,
                    "W: %4d x H: %4d",
                    "Format for width and height window, %4d for width, and %4d for height").c_str(),
            gx, gy);

    FbTk::BiDiString label(buf);
    m_geom_window->showText(label);
}


void BScreen::showTooltip(const FbTk::BiDiString &text) {
    if (*resource.tooltip_delay >= 0)
        m_tooltip_window->showText(text);
}

void BScreen::hideTooltip() {
    if (*resource.tooltip_delay >= 0)
        m_tooltip_window->hide();
}


void BScreen::hideGeometry() {
    m_geom_window->hide();
}

void BScreen::setLayer(FbTk::LayerItem &item, int layernum) {
    m_layermanager.moveToLayer(item, layernum);
}

void BScreen::renderGeomWindow() {

    char buf[256];
    _FB_USES_NLS;

    const std::string msg = _FB_XTEXT(Screen, GeometrySpacing,
            "W: %04d x H: %04d", "Representative maximum sized text for width and height dialog");
    const int n = snprintf(buf, sizeof(buf), msg.c_str(), 0, 0);

    FbTk::BiDiString label(std::string(buf, n));
    m_geom_window->resizeForText(label);
    m_geom_window->reconfigTheme();
}


void BScreen::renderPosWindow() {
    m_pos_window->resizeForText(FbTk::BiDiString("0:00000 x 0:00000"));
    m_pos_window->reconfigTheme();
}

/**
 * Find the winclient to this window's left
 * So, we check the leftgroup hint, and see if we know any windows
 */
WinClient *BScreen::findGroupLeft(WinClient &winclient) {
    Window w = winclient.getGroupLeftWindow();
    if (w == None)
        return 0;

    WinClient *have_client = Fluxbox::instance()->searchWindow(w);

    if (!have_client) {
        // not found, add it to expecting
        m_expecting_groups[w] = &winclient;
    } else if (&have_client->screen() != &winclient.screen())
        // something is not consistent
        return 0;

    return have_client;
}

WinClient *BScreen::findGroupRight(WinClient &winclient) {
    Groupables::iterator it = m_expecting_groups.find(winclient.window());
    if (it == m_expecting_groups.end())
        return 0;

    // yay, this'll do.
    WinClient *other = it->second;
    m_expecting_groups.erase(it); // don't expect it anymore

    // forget about it if it isn't the left-most client in the group
    Window leftwin = other->getGroupLeftWindow();
    if (leftwin != None && leftwin != winclient.window())
        return 0;

    return other;
}
