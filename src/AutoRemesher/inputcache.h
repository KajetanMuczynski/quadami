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
#ifndef AUTO_REMESHER_INPUT_CACHE_H
#define AUTO_REMESHER_INPUT_CACHE_H

#include <AutoRemesher/AutoRemesher>
#include <AutoRemesher/FoldRepair>
#include <AutoRemesher/Vector3>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace AutoRemesher {

// The input of a run after the steps that depend on nothing but the input
// and the fold settings (cleanup, orientation, fold survey or repair, island
// split; docs/PREVIEW.md §4.2), so that a re-run or a plan of the same mesh
// can skip them.
struct PreparedInput {
    // The settings the preparation depends on.
    bool repairFolds = false;
    bool flattenSpikes = false;
    // The engine's state where the target first matters (its voxel size).
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    std::vector<std::pair<size_t, size_t>> markedEdges;
    size_t givenVertexCount = 0;
    size_t markedEdgesGiven = 0;
    size_t ignoredMarkedEdgeCount = 0;
    AutoRemesher::InputCleanupStats cleanup;
    // A plan skips the fold survey (it has no report to put it in); a run
    // that reads such a file surveys and writes the file again.
    bool foldsSurveyed = false;
    FoldRepair::Stats folds;
    // MeshSeparator::splitToIslands of `triangles`.
    std::vector<std::vector<std::vector<size_t>>> islands;
};

// The request's own arrays (RemeshRequest::positions, triangles and
// featureEdges), which the report compares the output with.  The file holds
// them too, so a host can leave them out on a re-run.
struct RawInput {
    std::vector<double> positions;
    std::vector<uint32_t> triangles;
    std::vector<uint32_t> featureEdges;
};

// One file holds `tag` (the host's text, the worker's build id: another
// text makes the file unusable, so a changed core never reads what another
// core prepared), the raw input and the prepared input.  Written to a
// temporary file next to `path` and renamed, so a reader sees the old file
// or the new one.  False with `error` set on failure.
bool writeInputCache(const std::string& path, const std::string& tag,
    const std::vector<double>& rawPositions, const std::vector<uint32_t>& rawTriangles,
    const std::vector<uint32_t>& rawFeatureEdges, const PreparedInput& input, std::string* error);

// False with `error` set when the file is missing, from another format or
// tag, or damaged; the outputs are then unspecified.
bool readInputCache(const std::string& path, const std::string& tag, RawInput* raw, PreparedInput* input,
    std::string* error);

}

#endif
