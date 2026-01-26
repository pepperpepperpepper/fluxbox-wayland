// RememberApp.hh for Fluxbox Window Manager
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

#ifndef REMEMBER_APP_HH
#define REMEMBER_APP_HH

#include "ClientPattern.hh"
#include "Window.hh"

#include "FbTk/RefCount.hh"

class Application {
public:
    Application(bool transient, bool grouped, ClientPattern *pat = 0);
    void reset();
    void forgetWorkspace() { workspace_remember = false; }
    void forgetHead() { head_remember = false; }
    void forgetDimensions() { dimensions_remember = false; }
    void forgetPosition() { position_remember = false; }
    void forgetShadedstate() { shadedstate_remember = false; }
    void forgetTabstate() { tabstate_remember = false; }
    void forgetDecostate() { decostate_remember = false; }
    void forgetFocusHiddenstate() { focushiddenstate_remember= false; }
    void forgetIconHiddenstate() { iconhiddenstate_remember= false; }
    void forgetStuckstate() { stuckstate_remember = false; }
    void forgetFocusProtection() { focusprotection_remember = false; }
    void forgetJumpworkspace() { jumpworkspace_remember = false; }
    void forgetLayer() { layer_remember = false; }
    void forgetSaveOnClose() { save_on_close_remember = false; }
    void forgetAlpha() { alpha_remember = false; }
    void forgetMinimizedstate() { minimizedstate_remember = false; }
    void forgetMaximizedstate() { maximizedstate_remember = false; }
    void forgetFullscreenstate() { fullscreenstate_remember = false; }

    void rememberWorkspace(int ws)
        { workspace = ws; workspace_remember = true; }
    void rememberHead(int h)
        { head = h; head_remember = true; }
    void rememberDimensions(int width, int height, bool is_w_relative, bool is_h_relative)
        {
          dimension_is_w_relative = is_w_relative;
          dimension_is_h_relative = is_h_relative;
          w = width; h = height;
          dimensions_remember = true;
        }
    void rememberFocusHiddenstate(bool state)
        { focushiddenstate= state; focushiddenstate_remember= true; }
    void rememberIconHiddenstate(bool state)
        { iconhiddenstate= state; iconhiddenstate_remember= true; }
    void rememberPosition(int posx, int posy, bool is_x_relative, bool is_y_relative,
                 FluxboxWindow::ReferenceCorner rfc = FluxboxWindow::LEFTTOP)
        {
          position_is_x_relative = is_x_relative;
          position_is_y_relative = is_y_relative;
          x = posx; y = posy;
          refc = rfc;
          position_remember = true;
        }
    void rememberShadedstate(bool state)
        { shadedstate = state; shadedstate_remember = true; }
    void rememberTabstate(bool state)
        { tabstate = state; tabstate_remember = true; }
    void rememberDecostate(unsigned int state)
        { decostate = state; decostate_remember = true; }
    void rememberStuckstate(bool state)
        { stuckstate = state; stuckstate_remember = true; }
    void rememberFocusProtection(unsigned int protect)
        { focusprotection = protect; focusprotection_remember = true; }
    void rememberJumpworkspace(bool state)
        { jumpworkspace = state; jumpworkspace_remember = true; }
    void rememberLayer(int layernum)
        { layer = layernum; layer_remember = true; }
    void rememberSaveOnClose(bool state)
        { save_on_close = state; save_on_close_remember = true; }
    void rememberAlpha(int focused_a, int unfocused_a)
        { focused_alpha = focused_a; unfocused_alpha = unfocused_a; alpha_remember = true; }
    void rememberMinimizedstate(bool state)
        { minimizedstate = state; minimizedstate_remember = true; }
    void rememberMaximizedstate(int state)
        { maximizedstate = state; maximizedstate_remember = true; }
    void rememberFullscreenstate(bool state)
        { fullscreenstate = state; fullscreenstate_remember = true; }

    bool workspace_remember;
    unsigned int workspace;

    bool head_remember;
    int head;

    bool dimensions_remember;
    int w,h; // width, height
    bool dimension_is_w_relative;
    bool dimension_is_h_relative;

    bool ignoreSizeHints_remember;

    bool position_remember;
    int x,y;
    bool position_is_x_relative;
    bool position_is_y_relative;
    FluxboxWindow::ReferenceCorner refc;

    bool alpha_remember;
    int focused_alpha;
    int unfocused_alpha;

    bool shadedstate_remember;
    bool shadedstate;

    bool tabstate_remember;
    bool tabstate;

    bool decostate_remember;
    unsigned int decostate;

    bool stuckstate_remember;
    bool stuckstate;

    bool focusprotection_remember;
    unsigned int focusprotection;

    bool focushiddenstate_remember;
    bool focushiddenstate;

    bool iconhiddenstate_remember;
    bool iconhiddenstate;

    bool jumpworkspace_remember;
    bool jumpworkspace;

    bool layer_remember;
    int layer;

    bool save_on_close_remember;
    bool save_on_close;

    bool minimizedstate_remember;
    bool minimizedstate;

    bool maximizedstate_remember;
    int maximizedstate;

    bool fullscreenstate_remember;
    bool fullscreenstate;

    bool is_transient, is_grouped;
    FbTk::RefCount<ClientPattern> group_pattern;
};

#endif // REMEMBER_APP_HH

