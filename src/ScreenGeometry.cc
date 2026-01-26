// ScreenGeometry.cc for Fluxbox Window Manager
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

#include "Workspace.hh"
#include "Window.hh"
#include "Strut.hh"
#include "HeadArea.hh"
#include "RectangleUtil.hh"
#include "Debug.hh"

#include "FbTk/App.hh"
#include "FbTk/FbWindow.hh"
#include "FbTk/StringUtil.hh"
#include "FbTk/Util.hh"

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

#include <X11/Xlib.h>

#ifdef XINERAMA
extern  "C" {
#include <X11/extensions/Xinerama.h>
}
#endif // XINERAMA

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <list>
#include <string>
#include <utility>
#include <vector>

using std::make_pair;
using std::pair;
using std::string;

namespace {

int calcSquareDistance(int x1, int y1, int x2, int y2) {
    return (x2-x1)*(x2-x1) + (y2-y1)*(y2-y1);
}

static void parseStruts(const std::string &s, int &l, int &r, int &t, int &b) {
    std::list<std::string> v;
    FbTk::StringUtil::stringtok(v, s, " ,");
    std::list<std::string>::iterator it = v.begin();
    if (it != v.end())   l = std::max(0, atoi(it->c_str()));
    if (++it != v.end()) r = std::max(0, atoi(it->c_str()));
    if (++it != v.end()) t = std::max(0, atoi(it->c_str()));
    if (++it != v.end()) b = std::max(0, atoi(it->c_str()));
}

} // end anonymous namespace

const Strut* BScreen::availableWorkspaceArea(int head) const {
    if (head > numHeads()) {
        /* May this ever happen? */
        static Strut whole(-1 /* should never be used */, 0, width(), 0, height());
        return &whole;
    }
    return m_head_areas[head ? head-1 : 0]->availableWorkspaceArea();
}

unsigned int BScreen::maxLeft(int head) const {

    // we ignore strut if we're doing full maximization
    if (hasXinerama())
        return doFullMax() ? getHeadX(head) :
            getHeadX(head) + availableWorkspaceArea(head)->left();
    else
        return doFullMax() ? 0 : availableWorkspaceArea(head)->left();
}

unsigned int BScreen::maxRight(int head) const {
    // we ignore strut if we're doing full maximization
    if (hasXinerama())
        return doFullMax() ? getHeadX(head) + getHeadWidth(head) :
            getHeadX(head) + getHeadWidth(head) - availableWorkspaceArea(head)->right();
    else
        return doFullMax() ? width() : width() - availableWorkspaceArea(head)->right();
}

unsigned int BScreen::maxTop(int head) const {
    // we ignore strut if we're doing full maximization

    if (hasXinerama())
        return doFullMax() ? getHeadY(head) : getHeadY(head) + availableWorkspaceArea(head)->top();
    else
        return doFullMax() ? 0 : availableWorkspaceArea(head)->top();
}

unsigned int BScreen::maxBottom(int head) const {
    // we ignore strut if we're doing full maximization

    if (hasXinerama())
        return doFullMax() ? getHeadY(head) + getHeadHeight(head) :
            getHeadY(head) + getHeadHeight(head) - availableWorkspaceArea(head)->bottom();
    else
        return doFullMax() ? height() : height() - availableWorkspaceArea(head)->bottom();
}

void BScreen::reconfigureStruts() {
    for (std::vector<Strut*>::iterator it = m_head_struts.begin(),
                                      end = m_head_struts.end(); it != end; ++it) {
        clearStrut(*it);
    }

    m_head_struts.clear();

    int gl = 0, gr = 0, gt = 0, gb = 0;
    parseStruts(FbTk::Resource<std::string>(resourceManager(), "",
                                            name() + ".struts",
                                         altName() + ".Struts"), gl, gr, gt, gb);
    const int nh = std::max(1, numHeads());
    for (int i = 1; i <= nh; ++i) {
        int l = gl, r = gr, t = gt, b = gb;
        char ai[16];
        sprintf(ai, "%d", i);
        parseStruts(FbTk::Resource<std::string>(resourceManager(), "",
                                            name() + ".struts." + ai,
                                         altName() + ".Struts." + ai), l, r, t, b);
        if (l+t+r+b)
            m_head_struts.push_back(requestStrut(i, l, r, t, b));
    }
    updateAvailableWorkspaceArea();
}

Strut *BScreen::requestStrut(int head, int left, int right, int top, int bottom) {
    if (head > numHeads() && head != 1) {
        // head does not exist (if head == 1, then numHeads() == 0,
        // which means no xinerama, but there's a head after all
        head = numHeads();
    }

    int begin = head-1;
    int end   = head;

    if (head == 0) { // all heads (or no xinerama)
        begin = 0;
        end = (numHeads() ? numHeads() : 1);
    }

    Strut* next = 0;
    for (int i = begin; i != end; i++) {
        next = m_head_areas[i]->requestStrut(i+1, left, right, top, bottom, next);
    }

    return next;
}

void BScreen::clearStrut(Strut *str) {
    if (str->next())
        clearStrut(str->next());
    int head = str->head() ? str->head() - 1 : 0;
    /* The number of heads may have changed, be careful. */
    if (head < (numHeads() ? numHeads() : 1))
        m_head_areas[head]->clearStrut(str);
    // str is invalid now
}

void BScreen::updateAvailableWorkspaceArea() {
    size_t n = (numHeads() ? numHeads() : 1);
    bool updated = false;

    for (size_t i = 0; i < n; i++) {
        updated = m_head_areas[i]->updateAvailableWorkspaceArea() || updated;
    }

    if (updated)
        m_workspace_area_sig.emit(*this);
}

int BScreen::getGap(int head, const char type) {
    return type == 'w' ? getXGap(head) : getYGap(head);
}

int BScreen::calRelativeSize(int head, int i, char type) {
    // return floor(i * getGap(head, type) / 100 + 0.5);
    return FbTk::RelCalcHelper::calPercentageValueOf(i, getGap(head, type));
}
int BScreen::calRelativeWidth(int head, int i) {
    return calRelativeSize(head, i, 'w');
}
int BScreen::calRelativeHeight(int head, int i) {
    return calRelativeSize(head, i, 'h');
}

int BScreen::calRelativePosition(int head, int i, char type) {
    int max = type == 'w' ? maxLeft(head) : maxTop(head);
    // return floor((i - min) / getGap(head, type)  * 100 + 0.5);
    return FbTk::RelCalcHelper::calPercentageOf((i - max), getGap(head, type));
}
// returns a pixel, which is relative to the width of the screen
// screen starts from 0, 1000 px width, if i is 10 then it should return 100
int BScreen::calRelativePositionWidth(int head, int i) {
    return calRelativePosition(head, i, 'w');
}
// returns a pixel, which is relative to the height of th escreen
// screen starts from 0, 1000 px height, if i is 10 then it should return 100
int BScreen::calRelativePositionHeight(int head, int i) {
    return calRelativePosition(head, i, 'h');
}

int BScreen::calRelativeDimension(int head, int i, char type) {
    // return floor(i / getGap(head, type) * 100 + 0.5);
    return FbTk::RelCalcHelper::calPercentageOf(i, getGap(head, type));
  }
int BScreen::calRelativeDimensionWidth(int head, int i) {
    return calRelativeDimension(head, i, 'w');
}
int BScreen::calRelativeDimensionHeight(int head, int i) {
    return calRelativeDimension(head, i, 'h');
}

float BScreen::getXGap(int head) {
    return maxRight(head) - maxLeft(head);
}
float BScreen::getYGap(int head) {
    return maxBottom(head) - maxTop(head);
}

void BScreen::updateSize() {
    // update xinerama layout
    initXinerama();

    // check if window geometry has changed
    if (rootWindow().updateGeometry()) {
        // reset background
        m_root_theme->reset();

        // send resize notify
        m_resize_sig.emit(*this);
        m_workspace_area_sig.emit(*this);

        // move windows out of inactive heads
        clearHeads();
    }
}

void BScreen::clearXinerama() {
    fbdbg<<"BScreen::initXinerama(): dont have Xinerama"<<endl;

    m_xinerama.avail = false;
    m_xinerama.heads.clear();
}

void BScreen::initXinerama() {
#ifdef XINERAMA
    Display* display = FbTk::App::instance()->display();
    int number = 0;
    XineramaScreenInfo *si = XineramaQueryScreens(display, &number);

    if (!si && number == 0) {
        clearXinerama();
        return;
    }

    m_xinerama.avail = true;

    fbdbg<<"BScreen::initXinerama(): have Xinerama"<<endl;

    /* The call may have actually failed. If this is the first time we init
     * Xinerama, fall back to turning it off. If not, pretend nothing
     * happened -- another event will tell us and it will work then. */
    if (!si) {
        if (m_xinerama.heads.empty())
            clearXinerama();
        return;
    }

    m_xinerama.heads.resize(number);
    for (int i = 0; i < number; i++) {
        m_xinerama.heads[i]._x = si[i].x_org;
        m_xinerama.heads[i]._y = si[i].y_org;
        m_xinerama.heads[i]._width = si[i].width;
        m_xinerama.heads[i]._height = si[i].height;
    }
    XFree(si);

    fbdbg<<"BScreen::initXinerama(): number of heads ="<<number<<endl;

    /* Reallocate to the new number of heads. */
    int ha_num = numHeads() ? numHeads() : 1;
    int ha_oldnum = m_head_areas.size();
    if (ha_num > ha_oldnum) {
        m_head_areas.resize(ha_num);
        for (int i = ha_oldnum; i < ha_num; i++)
            m_head_areas[i] = new HeadArea();
    } else if (ha_num < ha_oldnum) {
        for (int i = ha_num; i < ha_oldnum; i++)
            delete m_head_areas[i];
        m_head_areas.resize(ha_num);
    }

#else // XINERAMA
    m_xinerama.avail = false;
#endif // XINERAMA

    reconfigureStruts();
}

/* Move windows out of inactive heads */
void BScreen::clearHeads() {
    if (!hasXinerama()) return;

    for (Workspaces::iterator i = m_workspaces_list.begin();
            i != m_workspaces_list.end(); ++i) {
        for (Workspace::Windows::iterator win = (*i)->windowList().begin();
                win != (*i)->windowList().end(); ++win) {

            FluxboxWindow& w = *(*win);

            // check if the window is invisible
            bool invisible = true;
            int j;
            for (j = 0; j < numHeads(); ++j) {
                XineramaHeadInfo& hi = m_xinerama.heads[j];
                if (RectangleUtil::overlapRectangles(hi, w)) {
                    invisible = false;
                    break;
                }
            }

            if (invisible) { // get closest head and replace the (now invisible) cwindow
                int closest_head = getHead(w.fbWindow());
                if (closest_head == 0) {
                    closest_head = 1; // first head is a safe bet here
                }
                w.placeWindow(closest_head);
            }
        }
    }
}

int BScreen::getHead(int x, int y) const {
#ifdef XINERAMA
    if (hasXinerama()) {
        for (int i = 0; i < numHeads(); i++) {
            if (RectangleUtil::insideBorder(m_xinerama.heads[i], x, y, 0)) {
                return i+1;
            }
        }
    }
#endif // XINERAMA
    return 0;
}


int BScreen::getHead(const FbTk::FbWindow &win) const {

    int head = 0; // whole screen

#ifdef XINERAMA
    if (hasXinerama()) {

        // cast needed to prevent win.x() become "unsigned int" which is bad
        // since it might be negative
        int cx = win.x() + static_cast<int>(win.width() / 2);
        int cy = win.y() + static_cast<int>(win.height() / 2);

        head = getHead(cx, cy);
        if ( head == 0 ) {
            // if the center of the window is not on any head then select
            // the head which center is nearest to the window center
            long dist = -1;
            int i;
            for (i = 0; i < numHeads(); ++i) {
                const XineramaHeadInfo& hi = m_xinerama.heads[i];
                long d = calcSquareDistance(cx, cy,
                    hi.x() + (hi.width() / 2), hi.y() + (hi.height() / 2));
                if (dist == -1 || d < dist) { // found a closer head
                    head = i + 1;
                    dist = d;
                }
            }
        }
    }
#endif

    return head;
}


int BScreen::getCurrHead() const {
    if (!hasXinerama()) return 0;
    int root_x = 0, root_y = 0;
#ifdef XINERAMA
    union { int i; unsigned int ui; Window w; } ignore;

    XQueryPointer(FbTk::App::instance()->display(),
                  rootWindow().window(), &ignore.w,
                  &ignore.w, &root_x, &root_y,
                  &ignore.i, &ignore.i, &ignore.ui);
#endif // XINERAMA
    return getHead(root_x, root_y);
}

int BScreen::getHeadX(int head) const {
#ifdef XINERAMA
    if (head == 0 || head > numHeads()) return 0;
    return m_xinerama.heads[head-1].x();
#else
    return 0;
#endif // XINERAMA
}

int BScreen::getHeadY(int head) const {
#ifdef XINERAMA
    if (head == 0 || head > numHeads()) return 0;
    return m_xinerama.heads[head-1].y();
#else
    return 0;
#endif // XINERAMA
}

int BScreen::getHeadWidth(int head) const {
#ifdef XINERAMA
    if (head == 0 || head > numHeads()) return width();
    return m_xinerama.heads[head-1].width();
#else
    return width();
#endif // XINERAMA
}

int BScreen::getHeadHeight(int head) const {
#ifdef XINERAMA
    if (head == 0 || head > numHeads()) return height();
    return m_xinerama.heads[head-1].height();
#else
    return height();
#endif // XINERAMA
}

pair<int,int> BScreen::clampToHead(int head, int x, int y, int w, int h) const {

    // if there are multiple heads, head=0 is not valid
    // a better way would be to search the closest head
    if (head == 0 && numHeads() != 0)
        head = 1;

    int hx = getHeadX(head);
    int hy = getHeadY(head);
    int hw = getHeadWidth(head);
    int hh = getHeadHeight(head);

    x = FbTk::Util::clamp(x, hx, hx + hw - w);
    y = FbTk::Util::clamp(y, hy, hy + hh - h);

    return make_pair(x,y);
}

