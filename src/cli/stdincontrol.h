/*
 *  Copyright (C) 2026 Quadami contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef AUTO_REMESHER_CLI_STDINCONTROL_H
#define AUTO_REMESHER_CLI_STDINCONTROL_H
#include <AutoRemesher/Remesh>
#include <condition_variable>
#include <mutex>

namespace AutoRemesher {
namespace Cli {

    // Lets the process that started this one (the Blender add-on) cancel the
    // run: a detached thread requests the cancel when stdin receives the line
    // "cancel" or is closed, e.g. because the host died.  The token must live
    // until the process exits.
    void startStdinCancelReader(CancelToken* token);

    // Where a run that has made its plan waits for the host
    // (--wait-after-plan): the line "continue" lets it go on; "cancel" or a
    // closed stdin stops it.
    class ContinueGate {
    public:
        void release(bool goOn);
        // Blocks until the host decided; true: continue.
        bool wait();

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        int m_state = 0; // 0 waiting, 1 continue, 2 stop
    };

    // As startStdinCancelReader(), and the line "continue" releases `gate`
    // (a cancel or a closed stdin releases it with false).  Both must live
    // until the process exits.
    void startStdinControlReader(CancelToken* token, ContinueGate* gate);

}
}

#endif
