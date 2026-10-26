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
#include <AutoRemesher/Diagnostics>
#include <AutoRemesher/Predicates>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// K32GetProcessMemoryInfo lives in kernel32, so no psapi import library.
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace AutoRemesher {

namespace {

    void appendJsonString(std::string* out, const std::string& value)
    {
        out->push_back('"');
        for (const char character : value) {
            switch (character) {
            case '"':
                out->append("\\\"");
                break;
            case '\\':
                out->append("\\\\");
                break;
            case '\n':
                out->append("\\n");
                break;
            case '\r':
                out->append("\\r");
                break;
            case '\t':
                out->append("\\t");
                break;
            default:
                if ((unsigned char)character < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)(unsigned char)character);
                    out->append(escaped);
                } else {
                    out->push_back(character);
                }
            }
        }
        out->push_back('"');
    }

    // The shortest of %.15g and %.17g that reads back to the same double, so
    // counts print as integers and measured values lose nothing.
    void appendJsonNumber(std::string* out, double value)
    {
        if (!std::isfinite(value)) {
            out->append("null");
            return;
        }
        char text[32];
        std::snprintf(text, sizeof(text), "%.15g", value);
        if (std::strtod(text, nullptr) != value)
            std::snprintf(text, sizeof(text), "%.17g", value);
        out->append(text);
    }

}

UvOrientation measureUvOrientation(const std::vector<double>& cornerUvs)
{
    UvOrientation orientation;
    const size_t triangleCount = cornerUvs.size() / 6;
    orientation.triangles = triangleCount;
    for (size_t f = 0; f < triangleCount; ++f) {
        const double* uv = &cornerUvs[6 * f];
        const Vector2 a(uv[0], uv[1]), b(uv[2], uv[3]), c(uv[4], uv[5]);
        const double area = std::fabs((b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x()));
        orientation.totalArea += area;
        const int sign = Predicates::orient2d(a, b, c);
        if (sign > 0) {
            ++orientation.positive;
        } else if (sign < 0) {
            ++orientation.negative;
            orientation.negativeArea += area;
        } else {
            ++orientation.degenerate;
        }
    }
    return orientation;
}

void appendUvOrientation(DiagnosticRecord* record, const std::string& prefix,
    const UvOrientation& orientation)
{
    record->set(prefix + "_triangles", (double)orientation.triangles);
    record->set(prefix + "_flipped", (double)orientation.negative);
    record->set(prefix + "_degenerate", (double)orientation.degenerate);
    record->set(prefix + "_flipped_fraction",
        orientation.triangles > 0 ? (double)orientation.negative / orientation.triangles : 0.0);
    record->set(prefix + "_flipped_area_fraction",
        orientation.totalArea > 0.0 ? orientation.negativeArea / orientation.totalArea : 0.0);
}

std::string diagnosticsToJsonLines(const std::vector<DiagnosticRecord>& records,
    const std::vector<std::pair<std::string, std::string>>& context)
{
    std::string out;
    for (const DiagnosticRecord& record : records) {
        out.append("{\"schema\":\"autoremesher.stage-diagnostics\",\"version\":1");
        for (const auto& item : context) {
            out.push_back(',');
            appendJsonString(&out, item.first);
            out.push_back(':');
            appendJsonString(&out, item.second);
        }
        out.append(",\"stage\":");
        appendJsonString(&out, record.stage);
        if (record.island != DiagnosticRecord::noIsland) {
            out.append(",\"island\":");
            appendJsonNumber(&out, (double)record.island);
        }
        for (const auto& item : record.labels) {
            out.push_back(',');
            appendJsonString(&out, item.first);
            out.push_back(':');
            appendJsonString(&out, item.second);
        }
        for (const auto& item : record.values) {
            out.push_back(',');
            appendJsonString(&out, item.first);
            out.push_back(':');
            appendJsonNumber(&out, item.second);
        }
        out.append("}\n");
    }
    return out;
}

std::vector<std::pair<std::string, double>> summarizeDiagnostics(
    const std::vector<DiagnosticRecord>& records)
{
    const auto valueOf = [](const DiagnosticRecord& record, const char* key, double fallback) {
        for (const auto& item : record.values)
            if (item.first == key)
                return item.second;
        return fallback;
    };
    double flipped = 0, degenerate = 0, flippedContinuous = 0, flippedRounded = 0, flippedUntangled = 0, mapped = 0;
    double pinned = 0, untangleSeconds = 0;
    double integralityResidual = 0, constraintResidual = 0, droppedConstraints = 0;
    double roundingShift = 0, roundingPasses = 0;
    double solves = 0, factorizations = 0, fallbacks = 0, nonFinite = 0;
    double peakRss = 0, cpuSeconds = 0;
    double mergedVertices = 0, droppedTriangles = 0, cleanupKeptParts = 0, zeroAreaTriangles = 0;
    double flippedTriangles = 0, nonOrientablePieces = 0;
    double foldRepair = 0, foldedTriangles = 0, foldClusters = 0, foldPatches = 0, foldedAfter = 0, foldClustersAfter = 0;
    double foldMovedVertices = 0, foldMaxOffset = 0, foldMaxMove = 0, foldSpikes = 0, foldSpikeDrop = 0, foldMs = 0;
    double thinShare = 0, thinSpots = 0, thinGrowth = 0;
    double projected = 0, projectionKeptFolding = 0, projectionKeptOther = 0;
    double creaseSnapped = 0, relaxMoves = 0;
    // Area-weighted over islands; the rescale is the island's farthest from 1.
    double sizeArea = 0, sizeSlope = 0, sizeErrorSquared = 0, rescale = 0;
    // A solver object reports running totals, so only its last record counts
    // towards solves and factorisations: per island the frame field, the curl
    // correction and the last rounding pass.
    std::vector<const DiagnosticRecord*> lastRounding;
    for (const DiagnosticRecord& record : records) {
        if (record.stage == "run") {
            peakRss = valueOf(record, "peak_rss_bytes", 0);
            cpuSeconds = valueOf(record, "cpu_seconds", 0);
            mergedVertices = valueOf(record, "input_merged_vertices", 0);
            droppedTriangles = valueOf(record, "input_dropped_triangles", 0);
            cleanupKeptParts = valueOf(record, "input_cleanup_kept_parts", 0);
            zeroAreaTriangles = valueOf(record, "input_zero_area_triangles", 0);
            flippedTriangles = valueOf(record, "input_flipped_triangles", 0);
            nonOrientablePieces = valueOf(record, "input_non_orientable_pieces", 0);
            foldRepair = valueOf(record, "input_fold_repair", 0);
            foldedTriangles = valueOf(record, "input_folded_triangles", 0);
            foldClusters = valueOf(record, "input_fold_clusters", 0);
            foldPatches = valueOf(record, "input_fold_patches_repaired", 0);
            foldedAfter = valueOf(record, "input_folded_triangles_after", 0);
            foldClustersAfter = valueOf(record, "input_fold_clusters_after", 0);
            foldMovedVertices = valueOf(record, "input_fold_moved_vertices", 0);
            foldMaxOffset = valueOf(record, "input_fold_max_offset", 0);
            foldMaxMove = valueOf(record, "input_fold_max_move", 0);
            foldSpikes = valueOf(record, "input_fold_spikes", 0);
            foldSpikeDrop = valueOf(record, "input_fold_spike_drop_quads", 0);
            foldMs = valueOf(record, "fold_repair_ms", 0);
            thinShare = valueOf(record, "thin_area_share", 0);
            thinSpots = valueOf(record, "thin_spots", 0);
            thinGrowth = valueOf(record, "thin_growth_p50", 0);
        } else if (record.stage == "cover_result") {
            flipped += valueOf(record, "snapped_flipped", 0);
            degenerate += valueOf(record, "snapped_degenerate", 0);
            flippedContinuous += valueOf(record, "continuous_flipped", 0);
            flippedRounded += valueOf(record, "rounded_flipped", 0);
            flippedUntangled += valueOf(record, "untangled_flipped", 0);
            pinned += valueOf(record, "untangle_pinned_faces", 0);
            untangleSeconds += valueOf(record, "untangle_seconds", 0);
            mapped += valueOf(record, "snapped_triangles", 0);
            integralityResidual = std::max(integralityResidual, valueOf(record, "integrality_residual", 0));
            constraintResidual = std::max(constraintResidual, valueOf(record, "constraint_residual", 0));
            roundingPasses = std::max(roundingPasses, valueOf(record, "iterations", 0));
            const double area = valueOf(record, "size_measured_area", 0);
            if (area > 0) {
                sizeArea += area;
                sizeSlope += area * valueOf(record, "continuous_size_slope", 0);
                const double error = valueOf(record, "continuous_size_error_rms", 0);
                sizeErrorSquared += area * error * error;
            }
            const double islandRescale = valueOf(record, "continuous_rescale", 0);
            if (islandRescale > 0 && (!(rescale > 0) || std::fabs(std::log(islandRescale)) > std::fabs(std::log(rescale))))
                rescale = islandRescale;
        } else if (record.stage == "surface_projection") {
            projected += valueOf(record, "projected", 0);
            projectionKeptFolding += valueOf(record, "kept_folding", 0);
            projectionKeptOther += valueOf(record, "kept_unmatched", 0) + valueOf(record, "kept_far", 0);
        } else if (record.stage == "surface_relax") {
            creaseSnapped += valueOf(record, "snapped", 0);
            relaxMoves += valueOf(record, "moves", 0);
        } else if (record.stage == "cover_system") {
            droppedConstraints += valueOf(record, "dropped_constraints", 0);
        } else if (record.stage == "rounding_iteration") {
            roundingShift = std::max(roundingShift, valueOf(record, "max_rounding_shift", 0));
            if (!lastRounding.empty() && lastRounding.back()->island == record.island)
                lastRounding.back() = &record;
            else
                lastRounding.push_back(&record);
        }
        const bool hasSolve = record.stage == "frame_field" || record.stage == "curl_correction"
            || record.stage == "rounding_iteration";
        if (hasSolve) {
            fallbacks += valueOf(record, "solve_kkt_fallback", 0);
            nonFinite += valueOf(record, "solve_non_finite", 0);
            if (record.stage != "rounding_iteration") {
                solves += valueOf(record, "solve_solves", 0);
                factorizations += valueOf(record, "solve_factorizations", 0);
            }
        }
    }
    for (const DiagnosticRecord* record : lastRounding) {
        solves += valueOf(*record, "solve_solves", 0);
        factorizations += valueOf(*record, "solve_factorizations", 0);
    }
    return {
        { "uv_flipped_triangles", flipped },
        { "uv_degenerate_triangles", degenerate },
        { "uv_flipped_triangles_continuous", flippedContinuous },
        { "uv_flipped_triangles_rounded", flippedRounded },
        { "uv_flipped_triangles_untangled", flippedUntangled },
        { "uv_pinned_triangles", pinned },
        { "untangle_seconds", untangleSeconds },
        { "uv_mapped_triangles", mapped },
        { "integrality_residual", integralityResidual },
        { "constraint_residual", constraintResidual },
        { "dropped_constraints", droppedConstraints },
        { "max_rounding_shift", roundingShift },
        { "continuous_rescale", rescale },
        { "continuous_size_slope", sizeArea > 0 ? sizeSlope / sizeArea : 0 },
        { "continuous_size_error_rms", sizeArea > 0 ? std::sqrt(sizeErrorSquared / sizeArea) : 0 },
        { "rounding_passes", roundingPasses },
        { "solves", solves },
        { "factorizations", factorizations },
        { "kkt_fallbacks", fallbacks },
        { "non_finite_values", nonFinite },
        { "projected_vertices", projected },
        { "projection_kept_folding", projectionKeptFolding },
        { "projection_kept_unmatched", projectionKeptOther },
        { "crease_snapped_vertices", creaseSnapped },
        { "relax_moves", relaxMoves },
        { "peak_rss_bytes", peakRss },
        { "cpu_seconds", cpuSeconds },
        { "input_merged_vertices", mergedVertices },
        { "input_dropped_triangles", droppedTriangles },
        { "input_cleanup_kept_parts", cleanupKeptParts },
        { "input_zero_area_triangles", zeroAreaTriangles },
        { "input_flipped_triangles", flippedTriangles },
        { "input_non_orientable_pieces", nonOrientablePieces },
        { "input_fold_repair", foldRepair },
        { "input_folded_triangles", foldedTriangles },
        { "input_fold_clusters", foldClusters },
        { "input_fold_patches_repaired", foldPatches },
        { "input_folded_triangles_after", foldedAfter },
        { "input_fold_clusters_after", foldClustersAfter },
        { "input_fold_moved_vertices", foldMovedVertices },
        { "input_fold_max_offset", foldMaxOffset },
        { "input_fold_max_move", foldMaxMove },
        { "input_fold_spikes", foldSpikes },
        { "input_fold_spike_drop_quads", foldSpikeDrop },
        { "fold_repair_ms", foldMs },
        { "thin_area_share", thinShare },
        { "thin_spots", thinSpots },
        { "thin_growth_p50", thinGrowth },
    };
}

size_t peakResidentSetBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return (size_t)counters.PeakWorkingSetSize;
    return 0;
#else
    struct rusage usage;
    if (0 != getrusage(RUSAGE_SELF, &usage))
        return 0;
#if defined(__APPLE__)
    return (size_t)usage.ru_maxrss;
#else
    return (size_t)usage.ru_maxrss * 1024;
#endif
#endif
}

double processCpuSeconds()
{
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0.0;
    const auto seconds = [](const FILETIME& time) {
        return (double)(((unsigned long long)time.dwHighDateTime << 32) | time.dwLowDateTime) * 1e-7;
    };
    return seconds(kernel) + seconds(user);
#else
    struct rusage usage;
    if (0 != getrusage(RUSAGE_SELF, &usage))
        return 0.0;
    return (double)usage.ru_utime.tv_sec + 1e-6 * usage.ru_utime.tv_usec
        + (double)usage.ru_stime.tv_sec + 1e-6 * usage.ru_stime.tv_usec;
#endif
}

}
