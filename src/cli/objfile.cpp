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
#include "objfile.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

#define TINYOBJLOADER_USE_DOUBLE
#define TINYOBJLOADER_IMPLEMENTATION
#include "../tiny_obj_loader.h"

namespace AutoRemesher {
namespace Cli {

    namespace {

        // UTF-8 -> the platform's path type (UTF-16 on Windows).  C++17:
        // C++20 deprecates u8path in favour of char8_t strings.
        std::filesystem::path toPath(const std::string& utf8)
        {
            return std::filesystem::u8path(utf8);
        }

    }

    bool readObj(const std::string& path, std::vector<double>* positions,
        std::vector<uint32_t>* triangles, std::vector<uint32_t>* featureEdges, std::string* error)
    {
        std::ifstream stream(toPath(path), std::ios::in | std::ios::binary);
        if (!stream) {
            *error = "cannot open " + path;
            return false;
        }
        tinyobj::attrib_t attributes;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warning;
        std::string loadError;
        tinyobj::MaterialReader* noMaterials = nullptr;
        if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &warning, &loadError, &stream, noMaterials)) {
            *error = "cannot read " + path + (loadError.empty() ? std::string() : ": " + loadError);
            return false;
        }
        positions->assign(attributes.vertices.begin(), attributes.vertices.end());
        triangles->clear();
        featureEdges->clear();
        for (const auto& shape : shapes) {
            for (const auto& corner : shape.mesh.indices)
                triangles->push_back((uint32_t)corner.vertex_index);
            size_t offset = 0;
            for (const int count : shape.lines.num_line_vertices) {
                for (int i = 1; i < count; ++i) {
                    featureEdges->push_back((uint32_t)shape.lines.indices[offset + i - 1].vertex_index);
                    featureEdges->push_back((uint32_t)shape.lines.indices[offset + i].vertex_index);
                }
                offset += (size_t)count;
            }
        }
        return true;
    }

    bool writeObj(const std::string& path, const RemeshMesh& mesh, std::string* error)
    {
        std::ofstream stream(toPath(path), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream) {
            *error = "cannot open " + path;
            return false;
        }
        char line[128];
        for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
            const int length = std::snprintf(line, sizeof(line), "v %.17g %.17g %.17g\n",
                mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
            stream.write(line, length);
        }
        size_t offset = 0;
        for (const uint32_t size : mesh.faceSizes) {
            stream.put('f');
            for (uint32_t corner = 0; corner < size; ++corner) {
                const int length = std::snprintf(line, sizeof(line), " %u", mesh.faceIndices[offset + corner] + 1u);
                stream.write(line, length);
            }
            stream.put('\n');
            offset += size;
        }
        stream.flush();
        if (!stream) {
            *error = "cannot write " + path;
            return false;
        }
        return true;
    }

    bool writeTextFile(const std::string& path, const std::string& text, std::string* error)
    {
        std::ofstream stream(toPath(path), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream) {
            *error = "cannot open " + path;
            return false;
        }
        stream.write(text.data(), (std::streamsize)text.size());
        stream.flush();
        if (!stream) {
            *error = "cannot write " + path;
            return false;
        }
        return true;
    }

}
}
