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
// autoremesher-cli: the remeshing core behind a command line, without Qt.
// The Blender add-on runs it as its worker process (addon/README.md).
#include "build_id.h"
#include "objfile.h"
#include "stdincontrol.h"
#include <AutoRemesher/Remesh>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#ifndef AUTOREMESHER_VERSION
#define AUTOREMESHER_VERSION "unknown"
#endif

namespace {

using namespace AutoRemesher;

const char* const usage = "Usage: autoremesher-cli -i input.obj -o output.obj [options]\n"
                          "\n"
                          "Quad-remeshes a triangle or polygon mesh (Wavefront OBJ).\n"
                          "\n"
                          "Options:\n"
                          "  -i, --input <file.obj>      mesh to remesh (required); its line elements\n"
                          "                              (l a b ...) are marked edges, kept as creases\n"
                          "  -o, --output <file.obj>     where to write the quad mesh (required)\n"
                          "  --report-json <file.json>   status and quality report, also written when\n"
                          "                              the run fails (schema autoremesher.remesh-result)\n"
                          "  --stage-diagnostics <file>  per-stage solver, rounding and timing records as\n"
                          "                              JSON lines (schema autoremesher.stage-diagnostics)\n"
                          "  --target-quads <count>      target quad count (default 50000, at most\n"
                          "                              2000000; about 22 KB of memory per quad)\n"
                          "  --adaptivity <0..1>         curvature-adaptive density (default 0.5)\n"
                          "  --anisotropy <0..1>         curvature-adaptive elongation (default 1)\n"
                          "  --edge-scaling <factor>     quad edge length factor (default 1)\n"
                          "  --sharp-edge <degrees>      dihedral angle of sharp edges (default 90)\n"
                          "  --smooth-normal <degrees>   > 0 projects onto a smoothed input (default 0)\n"
                          "  --resolution fast|quality   triangle mesh the quads are solved on: quality\n"
                          "                              gives cleaner quads, fast takes about 2/3 of\n"
                          "                              the time; same quad count (default quality)\n"
                          "  --extractor qex|trace       qex: libQEx, falling back to our own tracing per\n"
                          "                              island; trace: only our own (default qex)\n"
                          "  --fold-repair on|off        untangle folded-over input triangles (brush\n"
                          "                              leftovers) before remeshing (default off)\n"
                          "  --flatten-spikes on|off     with --fold-repair on: where a fold cannot be\n"
                          "                              untangled, also lower needle spikes near it\n"
                          "                              (changes the shape; default off)\n"
                          "  --quad-smoothing laplacian|tangential\n"
                          "                              how the extracted quads are smoothed: tangential\n"
                          "                              slides them along the surface and keeps ridges\n"
                          "                              and creases (default laplacian)\n"
                          "  --stdin-control             cancel when stdin receives the line \"cancel\"\n"
                          "                              or is closed; the report then says \"cancelled\"\n"
                          "  -h, --help                  show this text\n"
                          "  -v, --version               show the version\n"
                          "\n"
                          "Progress goes to stdout as \"<percent>% done. <step>\" lines.\n"
                          "Exit status: 0 remeshed, 1 bad arguments or files, 2 the remesh did not\n"
                          "succeed (rejected input, cancelled or no faces; see the report).\n";

struct Options {
    std::string inputPath;
    std::string outputPath;
    std::string reportJsonPath;
    std::string stageDiagnosticsPath;
    RemeshParameters parameters;
    bool stdinControl = false;
};

// Above this many quads the run needs gigabytes, so the command line says
// how many (the Blender add-on asks first at the same count).
const uint32_t largeTargetQuadCount = 200000;

// Lives until exit, so the detached stdin reader never outlives it.
CancelToken g_stdinCancelToken;

bool parseDouble(const std::string& text, double* value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    *value = std::strtod(text.c_str(), &end);
    return '\0' == *end;
}

bool parseCount(const std::string& text, uint32_t* value)
{
    if (text.empty() || '-' == text[0])
        return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if ('\0' != *end || parsed > 0xffffffffull)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

// Returns 0 to run, otherwise the exit status (after --help or an error).
int parseArguments(const std::vector<std::string>& arguments, Options* options)
{
    for (size_t i = 1; i < arguments.size(); ++i) {
        std::string name = arguments[i];
        std::string value;
        bool hasValue = false;
        const size_t equals = name.find('=');
        if (0 == name.compare(0, 2, "--") && std::string::npos != equals) {
            value = name.substr(equals + 1);
            name.erase(equals);
            hasValue = true;
        }
        if ("-h" == name || "--help" == name) {
            std::fputs(usage, stdout);
            return -1;
        }
        if ("-v" == name || "--version" == name) {
            std::printf("autoremesher-cli %s (build %s, remesh interface %d.%d)\n", AUTOREMESHER_VERSION,
                AUTOREMESHER_BUILD_ID, InterfaceVersionMajor, InterfaceVersionMinor);
            return -1;
        }
        if ("--stdin-control" == name) {
            options->stdinControl = true;
            continue;
        }
        if (!hasValue) {
            if (i + 1 >= arguments.size()) {
                std::fprintf(stderr, "Error: %s needs a value\n", name.c_str());
                return 1;
            }
            value = arguments[++i];
        }
        bool valid = true;
        RemeshParameters& parameters = options->parameters;
        if ("-i" == name || "--input" == name)
            options->inputPath = value;
        else if ("-o" == name || "--output" == name)
            options->outputPath = value;
        else if ("--report-json" == name)
            options->reportJsonPath = value;
        else if ("--stage-diagnostics" == name)
            options->stageDiagnosticsPath = value;
        else if ("--target-quads" == name)
            valid = parseCount(value, &parameters.targetQuadCount);
        else if ("--adaptivity" == name)
            valid = parseDouble(value, &parameters.adaptivity);
        else if ("--anisotropy" == name)
            valid = parseDouble(value, &parameters.anisotropy);
        else if ("--edge-scaling" == name)
            valid = parseDouble(value, &parameters.edgeScaling);
        else if ("--sharp-edge" == name)
            valid = parseDouble(value, &parameters.sharpEdgeDegrees);
        else if ("--smooth-normal" == name)
            valid = parseDouble(value, &parameters.smoothNormalDegrees);
        else if ("--extractor" == name) {
            if (!parseExtractor(value, &parameters.extractor)) {
                std::fprintf(stderr, "Error: --extractor must be qex or trace, not %s\n", value.c_str());
                return 1;
            }
        } else if ("--quad-smoothing" == name) {
            if (!parseQuadSmoothing(value, &parameters.quadSmoothing)) {
                std::fprintf(stderr, "Error: --quad-smoothing must be laplacian or tangential, not %s\n", value.c_str());
                return 1;
            }
        } else if ("--resolution" == name) {
            if (!parseMeshResolution(value, &parameters.resolution)) {
                std::fprintf(stderr, "Error: --resolution must be fast or quality, not %s\n", value.c_str());
                return 1;
            }
        } else if ("--fold-repair" == name) {
            if ("on" != value && "off" != value) {
                std::fprintf(stderr, "Error: --fold-repair must be on or off, not %s\n", value.c_str());
                return 1;
            }
            parameters.repairFolds = "on" == value;
        } else if ("--flatten-spikes" == name) {
            if ("on" != value && "off" != value) {
                std::fprintf(stderr, "Error: --flatten-spikes must be on or off, not %s\n", value.c_str());
                return 1;
            }
            parameters.flattenSpikes = "on" == value;
        } else {
            std::fprintf(stderr, "Error: unknown option %s (see --help)\n", name.c_str());
            return 1;
        }
        if (!valid) {
            std::fprintf(stderr, "Error: %s: not a valid number: %s\n", name.c_str(), value.c_str());
            return 1;
        }
    }
    if (options->inputPath.empty() || options->outputPath.empty()) {
        std::fprintf(stderr, "Error: --input and --output are required (see --help)\n");
        return 1;
    }
    if (options->parameters.flattenSpikes && !options->parameters.repairFolds) {
        std::fprintf(stderr, "Error: --flatten-spikes on needs --fold-repair on\n");
        return 1;
    }
    // Above the core's maximum the remesh itself refuses with a message.
    const uint32_t targetQuads = options->parameters.targetQuadCount;
    if (targetQuads > largeTargetQuadCount && targetQuads <= MaximumTargetQuadCount) {
        std::fprintf(stderr, "Warning: --target-quads %u needs about %.1f GB of memory\n",
            targetQuads, targetQuads * ApproximatePeakBytesPerQuad / 1e9);
    }
    return 0;
}

// Several steps are shorter than one percent of the run, so a new step is
// printed even when the percentage has not changed.
void printProgress(float fraction, const char* step)
{
    static int lastPercent = -1;
    static std::string lastStep;
    const int percent = (int)(fraction * 100);
    if (percent == lastPercent && lastStep == step)
        return;
    lastPercent = percent;
    lastStep = step;
    if ('\0' == step[0])
        std::printf("%d%% done.\n", percent);
    else
        std::printf("%d%% done. %s\n", percent, step);
    std::fflush(stdout);
}

int run(const std::vector<std::string>& arguments)
{
    Options options;
    const int parsed = parseArguments(arguments, &options);
    if (0 != parsed)
        return parsed < 0 ? 0 : parsed;

    if (options.stdinControl)
        Cli::startStdinCancelReader(&g_stdinCancelToken);

    RemeshResult result;
    RemeshRequest request;
    request.parameters = options.parameters;
    std::string error;
    if (Cli::readObj(options.inputPath, &request.positions, &request.triangles, &request.featureEdges, &error)) {
        RemeshCallbacks callbacks;
        callbacks.progress = printProgress;
        if (options.stdinControl)
            callbacks.cancel = &g_stdinCancelToken;
        result = remesh(request, callbacks);
    } else {
        result.status = RemeshStatus::InvalidInput;
        result.message = error;
        result.parameters = options.parameters;
    }

    if (RemeshStatus::Ok == result.status && !Cli::writeObj(options.outputPath, result.mesh, &error)) {
        result.status = RemeshStatus::Failed;
        result.message = error;
    }
    bool reportWritten = true;
    if (!options.reportJsonPath.empty())
        reportWritten = Cli::writeTextFile(options.reportJsonPath, toJson(result), &error);
    if (reportWritten && !options.stageDiagnosticsPath.empty()) {
        const std::vector<std::pair<std::string, std::string>> context = {
            { "input", options.inputPath },
            { "target_quads", std::to_string(options.parameters.targetQuadCount) },
        };
        reportWritten = Cli::writeTextFile(options.stageDiagnosticsPath,
            diagnosticsToJsonLines(result.stageDiagnostics, context), &error);
    }

    if (RemeshStatus::Ok == result.status) {
        const QualityReport& report = result.report;
        std::printf("Remeshed: %zu quads, %zu other faces, %zu vertices in %.2f s\n",
            report.quadCount, report.nonQuadCount, report.vertexCount, report.seconds);
    } else {
        std::fprintf(stderr, "Error: %s: %s\n", toString(result.status), result.message.c_str());
    }
    if (!reportWritten) {
        std::fprintf(stderr, "Error: %s\n", error.c_str());
        return 1;
    }
    return RemeshStatus::Ok == result.status ? 0 : 2;
}

}

#ifdef _WIN32
// The wide entry point gets the arguments in UTF-16, so paths with characters
// outside the ANSI code page (a user name like "Łucja") survive.
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string argument(size > 0 ? (size_t)size : 1, '\0');
        if (size > 0)
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &argument[0], size, nullptr, nullptr);
        argument.resize(argument.size() - 1);
        arguments.push_back(argument);
    }
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
