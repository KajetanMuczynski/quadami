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
#include "stdincontrol.h"
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace AutoRemesher {
namespace Cli {

    namespace {

        // Reads stdin with the system call, not std::cin: glibc's exit() takes
        // the lock of every stdio stream, and a reader blocked inside
        // std::getline holds stdin's lock, so the process would hang on exit.
        // On Windows a closed pipe ends ReadFile with ERROR_BROKEN_PIPE.
        bool readStdinChunk(std::string& chunk)
        {
            char buffer[256];
#ifdef _WIN32
            const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
            DWORD count = 0;
            if (nullptr == input || INVALID_HANDLE_VALUE == input
                || !ReadFile(input, buffer, sizeof(buffer), &count, nullptr) || 0 == count)
                return false;
#else
            ssize_t count;
            do {
                count = read(STDIN_FILENO, buffer, sizeof(buffer));
            } while (count < 0 && EINTR == errno);
            if (count <= 0)
                return false;
#endif
            chunk.assign(buffer, (size_t)count);
            return true;
        }

    }

    void ContinueGate::release(bool goOn)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (0 != m_state)
                return;
            m_state = goOn ? 1 : 2;
        }
        m_changed.notify_all();
    }

    bool ContinueGate::wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_changed.wait(lock, [this] { return 0 != m_state; });
        return 1 == m_state;
    }

    void startStdinCancelReader(CancelToken* token)
    {
        startStdinControlReader(token, nullptr);
    }

    void startStdinControlReader(CancelToken* token, ContinueGate* gate)
    {
        std::thread([token, gate] {
            std::string pending;
            std::string chunk;
            while (readStdinChunk(chunk)) {
                pending += chunk;
                size_t newline;
                while (std::string::npos != (newline = pending.find('\n'))) {
                    std::string line = pending.substr(0, newline);
                    pending.erase(0, newline + 1);
                    if (!line.empty() && '\r' == line.back())
                        line.pop_back();
                    if ("cancel" == line) {
                        token->requestCancel();
                        if (nullptr != gate)
                            gate->release(false);
                        return;
                    }
                    if ("continue" == line && nullptr != gate)
                        gate->release(true);
                }
            }
            token->requestCancel();
            if (nullptr != gate)
                gate->release(false);
        }).detach();
    }

}
}
