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
#include <AutoRemesher/InputCache>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

namespace AutoRemesher {

namespace {

    // Bump on any change to the layout below.
    const uint32_t formatVersion = 1;
    const char beginMagic[8] = { 'Q', 'D', 'M', 'I', 'N', 'P', 'U', 'T' };
    const char endMagic[8] = { 'Q', 'D', 'M', 'I', 'N', 'E', 'N', 'D' };
    // Written in host byte order; a file from a host of the other order
    // reads this back swapped and is refused.
    const uint32_t byteOrderMark = 0x01020304u;
    const uint64_t maximumIndex = std::numeric_limits<uint32_t>::max();

    class Writer {
    public:
        explicit Writer(std::ofstream* stream)
            : m_stream(stream)
        {
        }

        void bytes(const void* data, size_t size)
        {
            if (0 != size)
                m_stream->write(static_cast<const char*>(data), (std::streamsize)size);
        }

        void u8(bool value)
        {
            const uint8_t byte = value ? 1 : 0;
            bytes(&byte, 1);
        }

        void u32(uint32_t value)
        {
            bytes(&value, sizeof(value));
        }

        void u64(uint64_t value)
        {
            bytes(&value, sizeof(value));
        }

        void f64(double value)
        {
            bytes(&value, sizeof(value));
        }

        void text(const std::string& value)
        {
            u64(value.size());
            bytes(value.data(), value.size());
        }

        template <typename T>
        void array(const std::vector<T>& values)
        {
            u64(values.size());
            bytes(values.data(), values.size() * sizeof(T));
        }

        // Faces with three corners as uint32 triples; false if one is not.
        bool triangles(const std::vector<std::vector<size_t>>& faces)
        {
            std::vector<uint32_t> flat;
            flat.reserve(faces.size() * 3);
            for (const auto& face : faces) {
                if (3 != face.size())
                    return false;
                for (const size_t v : face) {
                    if (v > maximumIndex)
                        return false;
                    flat.push_back((uint32_t)v);
                }
            }
            array(flat);
            return true;
        }

    private:
        std::ofstream* m_stream;
    };

    class Reader {
    public:
        explicit Reader(std::ifstream* stream)
            : m_stream(stream)
        {
            const auto here = m_stream->tellg();
            m_stream->seekg(0, std::ios::end);
            m_size = (uint64_t)(m_stream->tellg() - here);
            m_stream->seekg(here);
        }

        bool ok() const
        {
            return m_ok;
        }

        void bytes(void* data, size_t size)
        {
            if (!m_ok || 0 == size)
                return;
            if (size > m_size - m_read) {
                m_ok = false;
                return;
            }
            m_stream->read(static_cast<char*>(data), (std::streamsize)size);
            m_read += size;
            if (!*m_stream)
                m_ok = false;
        }

        bool u8()
        {
            uint8_t byte = 0;
            bytes(&byte, 1);
            if (byte > 1)
                m_ok = false;
            return 1 == byte;
        }

        uint32_t u32()
        {
            uint32_t value = 0;
            bytes(&value, sizeof(value));
            return value;
        }

        uint64_t u64()
        {
            uint64_t value = 0;
            bytes(&value, sizeof(value));
            return value;
        }

        double f64()
        {
            double value = 0.0;
            bytes(&value, sizeof(value));
            return value;
        }

        // A count of items of `itemSize` bytes that the rest of the file can
        // hold, so a damaged count cannot ask for a huge allocation.
        uint64_t count(size_t itemSize)
        {
            const uint64_t n = u64();
            if (m_ok && 0 != itemSize && n > (m_size - m_read) / itemSize)
                m_ok = false;
            return m_ok ? n : 0;
        }

        std::string text()
        {
            std::string value(count(1), '\0');
            bytes(&value[0], value.size());
            return value;
        }

        template <typename T>
        void array(std::vector<T>* values)
        {
            values->resize(count(sizeof(T)));
            bytes(values->data(), values->size() * sizeof(T));
        }

        void triangles(std::vector<std::vector<size_t>>* faces)
        {
            std::vector<uint32_t> flat;
            array(&flat);
            if (0 != flat.size() % 3) {
                m_ok = false;
                return;
            }
            faces->resize(flat.size() / 3);
            for (size_t i = 0; i < faces->size(); ++i)
                (*faces)[i] = { flat[3 * i], flat[3 * i + 1], flat[3 * i + 2] };
        }

    private:
        std::ifstream* m_stream;
        uint64_t m_size = 0;
        uint64_t m_read = 0;
        bool m_ok = true;
    };

    // The fields of the two stats structs, in file order.
    template <typename Visit>
    void visitCleanup(AutoRemesher::InputCleanupStats& s, Visit visit)
    {
        for (size_t* field : { &s.mergedVertices, &s.droppedTriangles, &s.keptParts, &s.zeroAreaTriangles,
                 &s.flippedTriangles, &s.nonOrientablePieces, &s.weldedEdges, &s.weldedVertices,
                 &s.weldKeptParts, &s.repeatedCornerTriangles, &s.duplicateTriangles, &s.splitVertices })
            visit(field);
    }

    template <typename VisitCount, typename VisitValue>
    void visitFolds(FoldRepair::Stats& s, VisitCount visitCount, VisitValue visitValue)
    {
        for (size_t* field : { &s.foldedTriangles, &s.clusters, &s.repairedPatches, &s.foldedTrianglesAfter,
                 &s.clustersAfter, &s.movedVertices, &s.flattenedSpikes })
            visitCount(field);
        for (double* field : { &s.radius, &s.maximumOffset, &s.maximumMove, &s.maximumSpikeDrop, &s.seconds })
            visitValue(field);
    }

    std::string temporaryPath(const std::string& path)
    {
        std::random_device device;
        std::ostringstream name;
        name << path << ".tmp-" << std::hex << device() << device();
        return name.str();
    }

}

bool writeInputCache(const std::string& path, const std::string& tag,
    const std::vector<double>& rawPositions, const std::vector<uint32_t>& rawTriangles,
    const std::vector<uint32_t>& rawFeatureEdges, const PreparedInput& input, std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (nullptr != error)
            *error = message;
        return false;
    };
    if (input.vertices.size() > maximumIndex)
        return fail("too many vertices for the input cache");

    const std::string temporary = temporaryPath(path);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return fail("cannot write " + temporary);
        Writer out(&stream);
        out.bytes(beginMagic, sizeof(beginMagic));
        out.u32(formatVersion);
        out.u32(byteOrderMark);
        out.text(tag);
        out.u8(input.repairFolds);
        out.u8(input.flattenSpikes);
        out.array(rawPositions);
        out.array(rawTriangles);
        out.array(rawFeatureEdges);
        out.u64(input.vertices.size());
        for (const Vector3& v : input.vertices) {
            out.f64(v.x());
            out.f64(v.y());
            out.f64(v.z());
        }
        bool triangles = out.triangles(input.triangles);
        std::vector<uint32_t> marked;
        marked.reserve(input.markedEdges.size() * 2);
        for (const auto& edge : input.markedEdges) {
            if (edge.first > maximumIndex || edge.second > maximumIndex)
                triangles = false;
            marked.push_back((uint32_t)edge.first);
            marked.push_back((uint32_t)edge.second);
        }
        out.array(marked);
        out.u64(input.givenVertexCount);
        out.u64(input.markedEdgesGiven);
        out.u64(input.ignoredMarkedEdgeCount);
        AutoRemesher::InputCleanupStats cleanup = input.cleanup;
        visitCleanup(cleanup, [&](size_t* field) { out.u64(*field); });
        out.u8(input.foldsSurveyed);
        FoldRepair::Stats folds = input.folds;
        visitFolds(folds, [&](size_t* field) { out.u64(*field); }, [&](double* field) { out.f64(*field); });
        out.u64(input.islands.size());
        for (const auto& island : input.islands)
            triangles = triangles && out.triangles(island);
        out.bytes(endMagic, sizeof(endMagic));
        stream.flush();
        if (!triangles || !stream) {
            stream.close();
            std::remove(temporary.c_str());
            return fail(triangles ? "cannot write " + temporary : "the prepared input has a face that is not a triangle");
        }
    }
    // std::rename does not replace an existing file on Windows.
    if (0 != std::rename(temporary.c_str(), path.c_str())) {
        std::remove(path.c_str());
        if (0 != std::rename(temporary.c_str(), path.c_str())) {
            std::remove(temporary.c_str());
            return fail("cannot replace " + path);
        }
    }
    return true;
}

bool readInputCache(const std::string& path, const std::string& tag, RawInput* raw, PreparedInput* input,
    std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (nullptr != error)
            *error = message;
        return false;
    };
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return fail("no input cache at " + path);
    Reader in(&stream);
    char magic[8] = {};
    in.bytes(magic, sizeof(magic));
    if (!in.ok() || 0 != std::memcmp(magic, beginMagic, sizeof(magic)))
        return fail("not an input cache file");
    if (formatVersion != in.u32() || byteOrderMark != in.u32())
        return fail("input cache from another format or platform");
    if (in.text() != tag || !in.ok())
        return fail("input cache written by another build");
    input->repairFolds = in.u8();
    input->flattenSpikes = in.u8();
    in.array(&raw->positions);
    in.array(&raw->triangles);
    in.array(&raw->featureEdges);
    input->vertices.resize(in.count(3 * sizeof(double)));
    for (Vector3& v : input->vertices) {
        const double x = in.f64();
        const double y = in.f64();
        const double z = in.f64();
        v = Vector3(x, y, z);
    }
    in.triangles(&input->triangles);
    std::vector<uint32_t> marked;
    in.array(&marked);
    if (0 != marked.size() % 2)
        return fail("damaged input cache");
    input->markedEdges.clear();
    for (size_t i = 0; i + 1 < marked.size(); i += 2)
        input->markedEdges.emplace_back(marked[i], marked[i + 1]);
    input->givenVertexCount = (size_t)in.u64();
    input->markedEdgesGiven = (size_t)in.u64();
    input->ignoredMarkedEdgeCount = (size_t)in.u64();
    visitCleanup(input->cleanup, [&](size_t* field) { *field = (size_t)in.u64(); });
    input->foldsSurveyed = in.u8();
    visitFolds(input->folds, [&](size_t* field) { *field = (size_t)in.u64(); }, [&](double* field) { *field = in.f64(); });
    input->islands.resize(in.count(sizeof(uint64_t)));
    for (auto& island : input->islands)
        in.triangles(&island);
    in.bytes(magic, sizeof(magic));
    if (!in.ok() || 0 != std::memcmp(magic, endMagic, sizeof(magic)))
        return fail("damaged input cache");
    // Indices must stay inside their arrays: a reader of a damaged file must
    // not index out of bounds later.
    const size_t rawVertexCount = raw->positions.size() / 3;
    bool valid = 0 == raw->positions.size() % 3 && 0 == raw->triangles.size() % 3
        && 0 == raw->featureEdges.size() % 2 && input->givenVertexCount <= input->vertices.size();
    for (const uint32_t v : raw->triangles)
        valid = valid && v < rawVertexCount;
    for (const uint32_t v : raw->featureEdges)
        valid = valid && v < rawVertexCount;
    const auto inside = [&](const std::vector<std::vector<size_t>>& faces) {
        for (const auto& face : faces)
            for (const size_t v : face)
                if (v >= input->vertices.size())
                    return false;
        return true;
    };
    valid = valid && inside(input->triangles);
    for (const auto& island : input->islands)
        valid = valid && inside(island);
    for (const auto& edge : input->markedEdges)
        valid = valid && edge.first < input->vertices.size() && edge.second < input->vertices.size();
    if (!valid)
        return fail("damaged input cache");
    return true;
}

}
