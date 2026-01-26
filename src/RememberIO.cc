// RememberIO.cc for Fluxbox Window Manager
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

#include "Screen.hh"
#include "WinClient.hh"
#include "FbCommands.hh"
#include "fluxbox.hh"
#include "Layer.hh"
#include "Debug.hh"

#include "FbTk/I18n.hh"
#include "FbTk/FbString.hh"
#include "FbTk/StringUtil.hh"
#include "FbTk/FileUtil.hh"
#include "FbTk/App.hh"
#include "FbTk/stringstream.hh"
#include "FbTk/Transparent.hh"
#include "FbTk/AutoReloadHelper.hh"
#include "FbTk/RefCount.hh"
#include "FbTk/Util.hh"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <list>
#include <set>
#include <string>
#include <vector>

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
using FbTk::StringUtil::extractNumber;
using FbTk::StringUtil::expandFilename;

namespace {

inline bool isComment(std::string& line) {
    removeFirstWhitespace(line);
    removeTrailingWhitespace(line);
    if (line.size() == 0 || line[0] == '#')
        return true;
    return false;
}

// offset is the offset in the string that we start looking from
// return true if all ok, false on error
bool handleStartupItem(const string &line, int offset) {

    Fluxbox* fb = Fluxbox::instance();
    unsigned int screen = fb->keyScreen()->screenNumber();
    int next = 0;
    string str;

    // accept some options, for now only "screen=NN"
    // these option are given in parentheses before the command
    next = getStringBetween(str, line.c_str() + offset, '(', ')');
    if (next > 0) {
        // there are some options
        string option;
        int pos = str.find('=');
        bool error = false;
        if (pos > 0) {
            option = str.substr(0, pos);
            if (strcasecmp(option.c_str(), "screen") == 0) {
                error = !extractNumber(str.c_str() + pos + 1, screen);
            } else {
                error = true;
            }
        } else {
            error = true;
        }
        if (error) {
            cerr<<"Error parsing startup options."<<endl;
            return false;
        }
    } else {
        next = 0;
    }

    next = getStringBetween(str, line.c_str() + offset + next, '{', '}');

    if (next <= 0) {
        cerr<<"Error parsing [startup] at column "<<offset<<" - expecting {command}."<<endl;
        return false;
    }

    // don't run command if fluxbox is restarting
    if (fb->findScreen(screen)->isRestart())
        // the line was successfully read; we just didn't use it
        return true;

    FbCommands::ExecuteCmd *tmp_exec_cmd = new FbCommands::ExecuteCmd(str, screen);

    fbdbg<<"Executing startup Command<void> '"<<str<<"' on screen "<<screen<<endl;

    tmp_exec_cmd->execute();
    delete tmp_exec_cmd;
    return true;
}

// returns number of lines read
// optionally can give a line to read before the first (lookahead line)
int parseApp(ifstream &file, Application &app, string *first_line = 0) {
    string line;
    _FB_USES_NLS;
    Focus::Protection protect = Focus::NoProtection;
    bool remember_protect = false;
    int row = 0;
    while (! file.eof()) {
        if (!(first_line || getline(file, line))) {
            continue;
        }

        if (first_line) {
            line = *first_line;
            first_line = 0;
        }

        row++;
        if (isComment(line)) {
            continue;
        }

        string str_key, str_option, str_label;
        int parse_pos = 0;
        int err = getStringBetween(str_key, line.c_str(), '[', ']');
        if (err > 0) {
            int tmp = getStringBetween(str_option, line.c_str() + err, '(', ')');
            if (tmp > 0)
                err += tmp;
        }
        if (err > 0 ) {
            parse_pos += err;
            getStringBetween(str_label, line.c_str() + parse_pos, '{', '}');
        } else
            continue; //read next line

        bool had_error = false;

        if (str_key.empty())
            continue; //read next line

        str_key = toLower(str_key);
        str_label = toLower(str_label);

        if (str_key == "workspace") {
            unsigned int w;
            if (extractNumber(str_label, w))
                app.rememberWorkspace(w);
            else
                had_error = true;
        } else if (str_key == "head") {
            unsigned int h;
            if (extractNumber(str_label, h))
                app.rememberHead(h);
            else
                had_error = true;
        } else if (str_key == "layer") {
            int l = ResourceLayer::getNumFromString(str_label);
            had_error = (l == -1);
            if (!had_error)
                app.rememberLayer(l);
        } else if (str_key == "dimensions") {
            std::vector<string> tokens;
            FbTk::StringUtil::stringtok<std::vector<string> >(tokens, str_label);
            if (tokens.size() == 2) {
                unsigned int h, w;
                bool h_relative, w_relative, ignore;
                w = FbTk::StringUtil::parseSizeToken(tokens[0], w_relative, ignore);
                h = FbTk::StringUtil::parseSizeToken(tokens[1], h_relative, ignore);
                app.rememberDimensions(w, h, w_relative, h_relative);
            } else
                had_error = true;
        } else if (str_key == "ignoresizehints") {
            app.ignoreSizeHints_remember = str_label == "yes";
        } else if (str_key == "position") {
            FluxboxWindow::ReferenceCorner r = FluxboxWindow::LEFTTOP;
            // more info about the parameter
            // in ::rememberPosition

            if (str_option.length())
                r = FluxboxWindow::getCorner(str_option);
            if (!(had_error = (r == FluxboxWindow::ERROR))) {
                std::vector<string> tokens;
                FbTk::StringUtil::stringtok<std::vector<string> >(tokens, str_label);
                if (tokens.size() == 2) {
                    int x, y;
                    bool x_relative, y_relative, ignore;
                    x = FbTk::StringUtil::parseSizeToken(tokens[0], x_relative, ignore);
                    y = FbTk::StringUtil::parseSizeToken(tokens[1], y_relative, ignore);
                    app.rememberPosition(x, y, x_relative, y_relative, r);
                } else
                    had_error = true;
            }
        } else if (str_key == "shaded") {
            app.rememberShadedstate(str_label == "yes");
        } else if (str_key == "tab") {
            app.rememberTabstate(str_label == "yes");
        } else if (str_key == "focushidden") {
            app.rememberFocusHiddenstate(str_label == "yes");
        } else if (str_key == "iconhidden") {
            app.rememberIconHiddenstate(str_label == "yes");
        } else if (str_key == "hidden") {
            app.rememberIconHiddenstate(str_label == "yes");
            app.rememberFocusHiddenstate(str_label == "yes");
        } else if (str_key == "deco") {
            int deco = WindowState::getDecoMaskFromString(str_label);
            if (deco == -1)
                had_error = 1;
            else
                app.rememberDecostate((unsigned int)deco);
        } else if (str_key == "alpha") {
            int focused_a, unfocused_a;
            switch (sscanf(str_label.c_str(), "%i %i", &focused_a, &unfocused_a)) {
            case 1: // 'alpha <focus>'
                unfocused_a = focused_a;
            case 2: // 'alpha <focus> <unfocus>'
                focused_a = FbTk::Util::clamp(focused_a, 0, 255);
                unfocused_a = FbTk::Util::clamp(unfocused_a, 0, 255);
                app.rememberAlpha(focused_a, unfocused_a);
                break;
            default:
                had_error = true;
                break;
            }
        } else if (str_key == "sticky") {
            app.rememberStuckstate(str_label == "yes");
        } else if (str_key == "focusnewwindow") {
            remember_protect = true;
            if (!(protect & (Focus::Gain|Focus::Refuse))) { // cut back on contradiction
                if (str_label == "yes")
                    protect |= Focus::Gain;
                else
                    protect |= Focus::Refuse;
            }
        } else if (str_key == "focusprotection") {
            remember_protect = true;
            std::list<std::string> labels;
            FbTk::StringUtil::stringtok(labels, str_label, ", ");
            std::list<std::string>::iterator it = labels.begin();
            for (; it != labels.end(); ++it) {
                if (*it == "lock")
                    protect = (protect & ~Focus::Deny) | Focus::Lock;
                else if (*it == "deny")
                    protect = (protect & ~Focus::Lock) | Focus::Deny;
                else if (*it == "gain")
                    protect = (protect & ~Focus::Refuse) | Focus::Gain;
                else if (*it == "refuse")
                    protect = (protect & ~Focus::Gain) | Focus::Refuse;
                else if (*it == "none")
                    protect = Focus::NoProtection;
                else
                    had_error = 1;
            }
        } else if (str_key == "minimized") {
            app.rememberMinimizedstate(str_label == "yes");
        } else if (str_key == "maximized") {
            WindowState::MaximizeMode m = WindowState::MAX_NONE;
            if (str_label == "yes")
                m = WindowState::MAX_FULL;
            else if (str_label == "horz")
                m = WindowState::MAX_HORZ;
            else if (str_label == "vert")
                m = WindowState::MAX_VERT;
            app.rememberMaximizedstate(m);
        } else if (str_key == "fullscreen") {
            app.rememberFullscreenstate(str_label == "yes");
        } else if (str_key == "jump") {
            app.rememberJumpworkspace(str_label == "yes");
        } else if (str_key == "close") {
            app.rememberSaveOnClose(str_label == "yes");
        } else if (str_key == "end") {
            break;
        } else {
            cerr << _FB_CONSOLETEXT(Remember, Unknown, "Unknown apps key", "apps entry type not known")<<" = " << str_key << endl;
        }
        if (had_error) {
            cerr<<"Error parsing apps entry: ("<<line<<")"<<endl;
        }
    }
    if (remember_protect)
        app.rememberFocusProtection(protect);
    return row;
}

/*
  This function is used to search for old instances of the same pattern
  (when reloading apps file). More than one pattern might match, but only
  if the application is the same (also note that they'll be adjacent).
  We REMOVE and delete any matching patterns from the old list, as they're
  effectively moved into the new
*/
Application* findMatchingPatterns(ClientPattern *pat, Remember::Patterns *patlist, bool transient, bool is_group, ClientPattern *match_pat = 0) {

    Remember::Patterns::iterator it = patlist->begin();
    Remember::Patterns::iterator it_end = patlist->end();

    for (; it != it_end; ++it) {
        if (*it->first == *pat && is_group == it->second->is_grouped &&
            transient == it->second->is_transient &&
            ((match_pat == 0 && it->second->group_pattern == 0) ||
             (match_pat && *match_pat == *it->second->group_pattern))) {

            Application *ret = it->second;

            if (!is_group) return ret;
            // find the rest of the group and remove it from the list

            // rewind
            Remember::Patterns::iterator tmpit = it;
            while (tmpit != patlist->begin()) {
                --tmpit;
                if (tmpit->second == ret)
                    it = tmpit;
                else
                    break;
            }

            // forward
            for(; it != it_end && it->second == ret; ++it) {
                delete it->first;
            }
            patlist->erase(patlist->begin(), it);

            return ret;
        }
    }

    return 0;
}

} // end anonymous namespace

void Remember::reload() {

    Fluxbox& fb = *Fluxbox::instance();
    string apps_string = expandFilename(fb.getAppsFilename());
    bool ok = true;

    fbdbg<<"("<<__FUNCTION__<<"): Loading apps file ["<<apps_string<<"]"<<endl;

    ifstream apps_file(apps_string.c_str());

    // we merge the old patterns with new ones
    Patterns *old_pats = m_pats.release();
    set<Application *> reused_apps;
    m_pats.reset(new Patterns());
    m_startups.clear();

    if (apps_file.fail()) {
        ok = false;
        cerr << "failed to open apps file " << apps_string << endl;
    }

    if (ok && apps_file.eof()) {
        ok = false;
        fbdbg<<"("<<__FUNCTION__<< ") Empty apps file" << endl;
    }

    if (ok) {
        string line;
        int row = 0;
        bool in_group = false;
        ClientPattern *pat = 0;
        list<ClientPattern *> grouped_pats;
        while (getline(apps_file, line) && ! apps_file.eof()) {
            row++;

            if (isComment(line)) {
                continue;
            }

            string key;
            int err=0;
            int pos = getStringBetween(key, line.c_str(), '[', ']');
            string lc_key = toLower(key);

            if (pos > 0 && (lc_key == "app" || lc_key == "transient")) {
                ClientPattern *pat = new ClientPattern(line.c_str() + pos);
                if (!in_group) {
                    if ((err = pat->error()) == 0) {
                        bool transient = (lc_key == "transient");
                        Application *app = findMatchingPatterns(pat,
                                               old_pats, transient, false);
                        if (app) {
                            app->reset();
                            reused_apps.insert(app);
                        } else {
                            app = new Application(transient, false);
                        }

                        m_pats->push_back(make_pair(pat, app));
                        row += parseApp(apps_file, *app);
                    } else {
                        cerr<<"Error reading apps file at line "<<row<<", column "<<(err+pos)<<"."<<endl;
                        delete pat; // since it didn't work
                    }
                } else {
                    grouped_pats.push_back(pat);
                }
            } else if (pos > 0 && lc_key == "startup" && fb.isStartup()) {
                if (!handleStartupItem(line, pos)) {
                    cerr<<"Error reading apps file at line "<<row<<"."<<endl;
                }
                // save the item even if it was bad (aren't we nice)
                m_startups.push_back(line.substr(pos));
            } else if (pos > 0 && lc_key == "group") {
                in_group = true;
                if (line.find('(') != string::npos)
                    pat = new ClientPattern(line.c_str() + pos);
            } else if (in_group) {
                // otherwise assume that it is the start of the attributes
                Application *app = 0;
                // search for a matching app
                list<ClientPattern *>::iterator it = grouped_pats.begin();
                list<ClientPattern *>::iterator it_end = grouped_pats.end();
                for (; !app && it != it_end; ++it) {
                    app = findMatchingPatterns(*it, old_pats, false, in_group, pat);
                }

                if (!app)
                    app = new Application(false, in_group, pat);
                else
                    reused_apps.insert(app);

                while (!grouped_pats.empty()) {
                    // associate all the patterns with this app
                    m_pats->push_back(make_pair(grouped_pats.front(), app));
                    grouped_pats.pop_front();
                }

                // we hit end... probably don't have attribs for the group
                // so finish it off with an empty application
                // otherwise parse the app
                if (!(pos>0 && lc_key == "end")) {
                    row += parseApp(apps_file, *app, &line);
                }
                in_group = false;
            } else
                cerr<<"Error in apps file on line "<<row<<"."<<endl;
        }
    }

    // Clean up old state
    // can't just delete old patterns list. Need to delete the
    // patterns themselves, plus the applications!

    Patterns::iterator it;
    set<Application *> old_apps; // no duplicates
    while (!old_pats->empty()) {
        it = old_pats->begin();
        delete it->first; // ClientPattern
        if (reused_apps.find(it->second) == reused_apps.end())
            old_apps.insert(it->second); // Application, not necessarily unique
        old_pats->erase(it);
    }

    // now remove any client entries for the old apps
    Clients::iterator cit = m_clients.begin();
    Clients::iterator cit_end = m_clients.end();
    while (cit != cit_end) {
        if (old_apps.find(cit->second) != old_apps.end()) {
            Clients::iterator tmpit = cit;
            ++cit;
            m_clients.erase(tmpit);
        } else {
            ++cit;
        }
    }

    set<Application *>::iterator ait = old_apps.begin(); // no duplicates
    for (; ait != old_apps.end(); ++ait) {
        delete (*ait);
    }

    delete old_pats;
}

void Remember::save() {

    string apps_string = FbTk::StringUtil::expandFilename(Fluxbox::instance()->getAppsFilename());

    fbdbg<<"("<<__FUNCTION__<<"): Saving apps file ["<<apps_string<<"]"<<endl;

    ofstream apps_file(apps_string.c_str());

    // first of all we output all the startup commands
    Startups::iterator sit = m_startups.begin();
    Startups::iterator sit_end = m_startups.end();
    for (; sit != sit_end; ++sit) {
        apps_file<<"[startup] "<<(*sit)<<endl;
    }

    Patterns::iterator it = m_pats->begin();
    Patterns::iterator it_end = m_pats->end();

    set<Application *> grouped_apps; // no duplicates

    for (; it != it_end; ++it) {
        Application &a = *it->second;
        if (a.is_grouped) {
            // if already processed
            if (grouped_apps.find(&a) != grouped_apps.end())
                continue;
            grouped_apps.insert(&a);
            // otherwise output this whole group
            apps_file << "[group]";
            if (a.group_pattern)
                apps_file << " " << a.group_pattern->toString();
            apps_file << endl;

            Patterns::iterator git = m_pats->begin();
            Patterns::iterator git_end = m_pats->end();
            for (; git != git_end; ++git) {
                if (git->second == &a) {
                    apps_file << (a.is_transient ? " [transient]" : " [app]") <<
                                 git->first->toString()<<endl;
                }
            }
        } else {
            apps_file << (a.is_transient ? "[transient]" : "[app]") <<
                         it->first->toString()<<endl;
        }
        if (a.workspace_remember) {
            apps_file << "  [Workspace]\t{" << a.workspace << "}" << endl;
        }
        if (a.head_remember) {
            apps_file << "  [Head]\t{" << a.head << "}" << endl;
        }
        if (a.dimensions_remember) {
            apps_file << "  [Dimensions]\t{" <<
                a.w << (a.dimension_is_w_relative ? "% " : " ") <<
                a.h << (a.dimension_is_h_relative ? "%}" : "}") << endl;
        }
        if (a.position_remember) {
            apps_file << "  [Position]\t(";
            switch(a.refc) {
            case FluxboxWindow::CENTER:
                apps_file << "CENTER";
                break;
            case FluxboxWindow::LEFTBOTTOM:
                apps_file << "LOWERLEFT";
                break;
            case FluxboxWindow::RIGHTBOTTOM:
                apps_file << "LOWERRIGHT";
                break;
            case FluxboxWindow::RIGHTTOP:
                apps_file << "UPPERRIGHT";
                break;
            case FluxboxWindow::LEFT:
                apps_file << "LEFT";
                break;
            case FluxboxWindow::RIGHT:
                apps_file << "RIGHT";
                break;
            case FluxboxWindow::TOP:
                apps_file << "TOP";
                break;
            case FluxboxWindow::BOTTOM:
                apps_file << "BOTTOM";
                break;
            default:
                apps_file << "UPPERLEFT";
            }
            apps_file << ")\t{" <<
                a.x << (a.position_is_x_relative ? "% " : " ") <<
                a.y << (a.position_is_y_relative ? "%}" : "}") << endl;
        }
        if (a.shadedstate_remember) {
            apps_file << "  [Shaded]\t{" << ((a.shadedstate)?"yes":"no") << "}" << endl;
        }
        if (a.tabstate_remember) {
            apps_file << "  [Tab]\t\t{" << ((a.tabstate)?"yes":"no") << "}" << endl;
        }
        if (a.decostate_remember) {
            switch (a.decostate) {
            case (0) :
                apps_file << "  [Deco]\t{NONE}" << endl;
                break;
            case (0xffffffff):
            case (WindowState::DECOR_NORMAL):
                apps_file << "  [Deco]\t{NORMAL}" << endl;
                break;
            case (WindowState::DECOR_TOOL):
                apps_file << "  [Deco]\t{TOOL}" << endl;
                break;
            case (WindowState::DECOR_TINY):
                apps_file << "  [Deco]\t{TINY}" << endl;
                break;
            case (WindowState::DECOR_BORDER):
                apps_file << "  [Deco]\t{BORDER}" << endl;
                break;
            case (WindowState::DECOR_TAB):
                apps_file << "  [Deco]\t{TAB}" << endl;
                break;
            default:
                apps_file << "  [Deco]\t{0x"<<hex<<a.decostate<<dec<<"}"<<endl;
                break;
            }
        }

        if (a.focushiddenstate_remember || a.iconhiddenstate_remember) {
            if (a.focushiddenstate_remember && a.iconhiddenstate_remember &&
                a.focushiddenstate == a.iconhiddenstate)
                apps_file << "  [Hidden]\t{" << ((a.focushiddenstate)?"yes":"no") << "}" << endl;
            else if (a.focushiddenstate_remember) {
                apps_file << "  [FocusHidden]\t{" << ((a.focushiddenstate)?"yes":"no") << "}" << endl;
            } else if (a.iconhiddenstate_remember) {
                apps_file << "  [IconHidden]\t{" << ((a.iconhiddenstate)?"yes":"no") << "}" << endl;
            }
        }
        if (a.stuckstate_remember) {
            apps_file << "  [Sticky]\t{" << ((a.stuckstate)?"yes":"no") << "}" << endl;
        }
        if (a.focusprotection_remember) {
            apps_file << "  [FocusProtection]\t{";
            if (a.focusprotection == Focus::NoProtection) {
                apps_file << "none";
            } else {
                bool b = false;
                if (a.focusprotection & Focus::Gain) {
                    apps_file << (b?",":"") << "gain";
                    b = true;
                }
                if (a.focusprotection & Focus::Refuse) {
                    apps_file << (b?",":"") << "refuse";
                    b = true;
                }
                if (a.focusprotection & Focus::Lock) {
                    apps_file << (b?",":"") << "lock";
                    b = true;
                }
                if (a.focusprotection & Focus::Deny) {
                    apps_file << (b?",":"") << "deny";
                    b = true;
                }
            }
            apps_file << "}" << endl;
        }
        if (a.minimizedstate_remember) {
            apps_file << "  [Minimized]\t{" << ((a.minimizedstate)?"yes":"no") << "}" << endl;
        }
        if (a.maximizedstate_remember) {
            apps_file << "  [Maximized]\t{";
            switch (a.maximizedstate) {
            case WindowState::MAX_FULL:
                apps_file << "yes" << "}" << endl;
                break;
            case WindowState::MAX_HORZ:
                apps_file << "horz" << "}" << endl;
                break;
            case WindowState::MAX_VERT:
                apps_file << "vert" << "}" << endl;
                break;
            case WindowState::MAX_NONE:
            default:
                apps_file << "no" << "}" << endl;
                break;
            }
        }
        if (a.fullscreenstate_remember) {
            apps_file << "  [Fullscreen]\t{" << ((a.fullscreenstate)?"yes":"no") << "}" << endl;
        }
        if (a.jumpworkspace_remember) {
            apps_file << "  [Jump]\t{" << ((a.jumpworkspace)?"yes":"no") << "}" << endl;
        }
        if (a.layer_remember) {
            apps_file << "  [Layer]\t{" << a.layer << "}" << endl;
        }
        if (a.save_on_close_remember) {
            apps_file << "  [Close]\t{" << ((a.save_on_close)?"yes":"no") << "}" << endl;
        }
        if (a.alpha_remember) {
            if (a.focused_alpha == a.unfocused_alpha)
                apps_file << "  [Alpha]\t{" << a.focused_alpha << "}" << endl;
            else
                apps_file << "  [Alpha]\t{" << a.focused_alpha << " " << a.unfocused_alpha << "}" << endl;
        }
        apps_file << "[end]" << endl;
    }
    apps_file.close();
    // update timestamp to avoid unnecessary reload
    m_reloader->addFile(Fluxbox::instance()->getAppsFilename());
}

