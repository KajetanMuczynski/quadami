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
#ifndef AUTO_REMESHER_CLI_OBJFILE_H
#define AUTO_REMESHER_CLI_OBJFILE_H
#include <AutoRemesher/Remesh>
#include <cstdint>
#include <string>
#include <vector>

// Wavefront OBJ in and out for the command line.  Paths are UTF-8 on every
// platform (the Windows entry point converts its wide arguments), so names
// outside the ANSI code page work.
namespace AutoRemesher {
namespace Cli {

    // Polygons are fan-triangulated as the Qt app does (tinyobjloader);
    // normals, UVs, groups and materials are ignored.  Line elements (`l a b
    // c ...`) are marked edges: each consecutive pair goes into featureEdges
    // (RemeshRequest::featureEdges).  Indices are passed on unchecked:
    // remesh() rejects any that point past the vertices.
    bool readObj(const std::string& path, std::vector<double>* positions,
        std::vector<uint32_t>* triangles, std::vector<uint32_t>* featureEdges, std::string* error);

    // Coordinates with 17 significant digits, so doubles read back exactly.
    bool writeObj(const std::string& path, const RemeshMesh& mesh, std::string* error);

    bool writeTextFile(const std::string& path, const std::string& text, std::string* error);

}
}

#endif
