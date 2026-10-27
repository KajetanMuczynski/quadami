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
// Termination and memory guards added to libQEx by the Quadami fork
// (TODO 6.1b; thirdparty/libQEx/PATCHES.md). On a folded map libQEx can
// build halfedge connectivity on which OpenMesh's circulators never end,
// trace for minutes, or grow without bound. Every guard throws QEx::Abort,
// and the caller falls back to its own extractor.
#ifndef QEX_GUARDS_HH
#define QEX_GUARDS_HH

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// libQEx's diagnostics go nowhere in the fork: on folded maps they flooded
// stderr with millions of lines (minutes of I/O, logs of gigabytes). The
// arguments are not evaluated.
#define QEX_LOG while (false) std::cerr

namespace QEx {

struct Abort : std::runtime_error {
    explicit Abort(const std::string& what) : std::runtime_error("QEx: " + what) {}
};

// Deterministic limits, set by the caller; 0 means no limit.
struct Limits {
    size_t max_steps = 0;          // tracing steps (find_path, connection repair)
    size_t max_grid_vertices = 0;  // grid vertices, which bound QEx's memory
};

// Throws unless the loop h -> next(h) of face f returns to its start and every
// halfedge on it belongs to f. A loop that never closed made the face
// circulators of the post-processing grow a vector until memory ran out:
// OpenMesh's delete_face relinks the neighbours of a deleted boundary edge,
// which at a pinched vertex can cut into another face's loop. O(face size)
// when the loop is sound, O(#halfedges) at most.
template <class MeshT>
void checkFaceLoopCloses(const MeshT& m, typename MeshT::FaceHandle f, const char* where)
{
    const auto h0 = m.halfedge_handle(f);
    auto h = h0;
    for (size_t n = 0; n <= m.n_halfedges(); ++n) {
        if (!h.is_valid() || m.face_handle(h) != f)
            break;
        h = m.next_halfedge_handle(h);
        if (h == h0)
            return;
    }
    throw Abort(std::string("face loop does not close ") + where);
}

// OpenMesh's PolyConnectivity::adjust_outgoing_halfedge(v), bounded: makes the
// first boundary halfedge of v's rotation h -> next(opposite(h)) its outgoing
// one, visiting the rotation in the same order as OpenMesh's circulator, so
// the result is the same wherever that terminates. QEx's add_face calls it
// after every face; on a folded map a rotation can run into a cycle that
// misses its start, and the circulator then never ended (ext_mosquito at
// 2000 quads), before checkCirculatorsTerminate below could see it.
template <class MeshT>
void adjustOutgoingHalfedgeBounded(MeshT& m, typename MeshT::VertexHandle v, const char* where)
{
    const auto h0 = m.halfedge_handle(v);
    auto h = h0;
    for (size_t n = 0; n <= m.n_halfedges(); ++n) {
        if (!h.is_valid())
            return;
        if (m.is_boundary(h)) {
            m.set_halfedge_handle(v, h);
            return;
        }
        h = m.next_halfedge_handle(m.opposite_halfedge_handle(h));
        if (h == h0)
            return;
    }
    throw Abort(std::string("vertex rotation does not close ") + where);
}

// The outgoing halfedges of v in the order of OpenMesh's vertex circulator
// (h -> next(opposite(h)) from v's halfedge). Throws when the rotation does not
// return to its start; the circulator then never ended (ext_watch at 1462
// quads, in create_faces after OpenMesh's add_face).
template <class MeshT>
std::vector<typename MeshT::HalfedgeHandle> outgoingHalfedgesBounded(const MeshT& m,
    typename MeshT::VertexHandle v, const char* where)
{
    std::vector<typename MeshT::HalfedgeHandle> outgoing;
    const auto h0 = m.halfedge_handle(v);
    auto h = h0;
    while (h.is_valid()) {
        if (outgoing.size() > m.n_halfedges())
            throw Abort(std::string("vertex rotation does not close ") + where);
        outgoing.push_back(h);
        h = m.next_halfedge_handle(m.opposite_halfedge_handle(h));
        if (h == h0)
            break;
    }
    return outgoing;
}

// Throws unless every handle a vertex or halfedge keeps (outgoing halfedge,
// next halfedge, to-vertex, face) is invalid or inside the mesh's arrays.
// garbage_collection() indexes its maps with them: a vertex whose outgoing
// halfedge lay past the halfedges after the n-gon merge made it read out of
// bounds (ASan, ext_watch at 2000 quads; a segfault in some runs, garbage
// connectivity in the others). O(#vertices + #halfedges).
template <class MeshT>
void checkHandlesInRange(const MeshT& m, const char* where)
{
    const auto bad = [&](int idx, size_t size) { return idx < -1 || (idx >= 0 && (size_t)idx >= size); };
    for (size_t v = 0; v < m.n_vertices(); ++v)
        if (bad(m.halfedge_handle(typename MeshT::VertexHandle((int)v)).idx(), m.n_halfedges()))
            throw Abort(std::string("vertex halfedge out of range ") + where);
    for (size_t h = 0; h < m.n_halfedges(); ++h) {
        const typename MeshT::HalfedgeHandle hh((int)h);
        if (bad(m.next_halfedge_handle(hh).idx(), m.n_halfedges())
                || bad(m.to_vertex_handle(hh).idx(), m.n_vertices())
                || bad(m.face_handle(hh).idx(), m.n_faces()))
            throw Abort(std::string("halfedge handle out of range ") + where);
    }
}

// Throws unless every vertex rotation h -> next(opposite(h)) and every face
// loop h -> next(h) of the polygon mesh returns to its start, which OpenMesh's
// circulators assume. O(#halfedges).
template <class MeshT>
void checkCirculatorsTerminate(const MeshT& m, const char* where)
{
    std::vector<char> seen(m.n_halfedges(), 0);
    for (auto v = m.vertices_begin(); v != m.vertices_end(); ++v) {
        if (m.has_vertex_status() && m.status(*v).deleted())
            continue;
        const auto h0 = m.halfedge_handle(*v);
        if (!h0.is_valid())
            continue;
        if (m.from_vertex_handle(h0) != *v)
            throw Abort(std::string("outgoing halfedge of another vertex ") + where);
        auto h = h0;
        do {
            if (seen[h.idx()])
                throw Abort(std::string("vertex rotation does not close ") + where);
            seen[h.idx()] = 1;
            h = m.next_halfedge_handle(m.opposite_halfedge_handle(h));
            if (!h.is_valid())
                throw Abort(std::string("vertex rotation is open ") + where);
        } while (h != h0);
    }
    for (auto f = m.faces_begin(); f != m.faces_end(); ++f) {
        if (!(m.has_face_status() && m.status(*f).deleted()))
            checkFaceLoopCloses(m, *f, where);
    }
}

}

#endif
