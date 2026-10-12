//
//  VoxelBody.cpp
//  libraries/auto-rigging/src
//
//  Created on 2026-10-12 for Darkride's avatar rigger (AUTO-RIGGING.md in the project root).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "VoxelBody.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

using namespace autorig;

static const int MARGIN = 3;  // empty voxels around the body, so the outside wraps it
static const double FAR_AWAY = 1.0e20;

// One pass of the exact Euclidean distance transform (Felzenszwalb and Huttenlocher, 2012): squared distances along a
// row, given squared distances so far in f.
static void distanceTransform1D(const std::vector<double>& f, std::vector<double>& d, std::vector<int>& v,
                                std::vector<double>& z, int n) {
    int k = 0;
    v[0] = 0;
    z[0] = -FAR_AWAY;
    z[1] = FAR_AWAY;
    for (int q = 1; q < n; q++) {
        double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * q - 2.0 * v[k]);
        while (s <= z[k]) {
            k--;
            s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * q - 2.0 * v[k]);
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = FAR_AWAY;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) {
            k++;
        }
        double dq = (double)q - v[k];
        d[q] = dq * dq + f[v[k]];
    }
}

VoxelBody::VoxelBody(const Body& body, float voxelSize) : _size(voxelSize) {
    _origin = body.minimum - glm::vec3((float)MARGIN * _size);
    glm::vec3 span = (body.maximum - body.minimum) / _size;
    _dimensions = glm::ivec3((int)std::ceil(span.x), (int)std::ceil(span.y), (int)std::ceil(span.z)) +
                  glm::ivec3(2 * MARGIN + 1);
    const size_t total = (size_t)_dimensions.x * _dimensions.y * _dimensions.z;

    // the surface: points sampled over every triangle half a voxel apart
    std::vector<uint8_t> surface(total, 0);
    for (const glm::uvec3& t : body.triangles) {
        const glm::vec3& a = body.vertices[t.x];
        const glm::vec3& b = body.vertices[t.y];
        const glm::vec3& c = body.vertices[t.z];
        float longest = std::max(glm::length(b - a), std::max(glm::length(c - a), glm::length(c - b)));
        int n = std::max(1, (int)std::ceil(longest / (0.5f * _size)));
        for (int i = 0; i <= n; i++) {
            for (int j = 0; j <= n - i; j++) {
                long k = find(a + (b - a) * ((float)i / n) + (c - a) * ((float)j / n));
                if (k >= 0) {
                    surface[k] = 1;
                }
            }
        }
    }

    // a wall: the surface grown by one voxel, which closes seams and small holes between the model's pieces
    std::vector<uint8_t> wall(surface);
    const size_t row = (size_t)_dimensions.x;
    const size_t slice = (size_t)_dimensions.x * _dimensions.y;
    for (int z = 1; z < _dimensions.z - 1; z++) {
        for (int y = 1; y < _dimensions.y - 1; y++) {
            for (int x = 1; x < _dimensions.x - 1; x++) {
                size_t i = index(x, y, z);
                if (surface[i]) {
                    wall[i - 1] = wall[i + 1] = 1;
                    wall[i - row] = wall[i + row] = 1;
                    wall[i - slice] = wall[i + slice] = 1;
                }
            }
        }
    }

    // everything reachable from the edge of the grid without crossing the wall is outside; the rest is the body
    std::vector<uint8_t> outside(total, 0);
    std::vector<size_t> open;
    auto reach = [&](int x, int y, int z) {
        size_t i = index(x, y, z);
        if (!wall[i] && !outside[i]) {
            outside[i] = 1;
            open.push_back(i);
        }
    };
    for (int z = 0; z < _dimensions.z; z++) {
        for (int y = 0; y < _dimensions.y; y++) {
            for (int x = 0; x < _dimensions.x; x++) {
                if (x == 0 || y == 0 || z == 0 || x == _dimensions.x - 1 || y == _dimensions.y - 1 ||
                    z == _dimensions.z - 1) {
                    reach(x, y, z);
                }
            }
        }
    }
    while (!open.empty()) {
        size_t i = open.back();
        open.pop_back();
        glm::ivec3 c = cell(i);
        if (c.x > 0) reach(c.x - 1, c.y, c.z);
        if (c.x < _dimensions.x - 1) reach(c.x + 1, c.y, c.z);
        if (c.y > 0) reach(c.x, c.y - 1, c.z);
        if (c.y < _dimensions.y - 1) reach(c.x, c.y + 1, c.z);
        if (c.z > 0) reach(c.x, c.y, c.z - 1);
        if (c.z < _dimensions.z - 1) reach(c.x, c.y, c.z + 1);
    }
    _inside.assign(total, 0);
    for (size_t i = 0; i < total; i++) {
        if (!outside[i]) {
            _inside[i] = 1;
            _insideCount++;
        }
    }

    // depth: the exact distance from each inside voxel to the nearest outside voxel, one axis at a time
    std::vector<double> squared(total);
    for (size_t i = 0; i < total; i++) {
        squared[i] = outside[i] ? 0.0 : FAR_AWAY;
    }
    const int longest = std::max(_dimensions.x, std::max(_dimensions.y, _dimensions.z));
    std::vector<double> f(longest), d(longest), z(longest + 1);
    std::vector<int> v(longest);
    for (int axis = 0; axis < 3; axis++) {
        const int n = _dimensions[axis];
        const size_t stride = axis == 0 ? 1 : (axis == 1 ? row : slice);
        const int a = axis == 0 ? 1 : 0;  // the two other axes
        const int b = axis == 2 ? 1 : 2;
        for (int j = 0; j < _dimensions[b]; j++) {
            for (int i = 0; i < _dimensions[a]; i++) {
                glm::ivec3 start(0);
                start[a] = i;
                start[b] = j;
                size_t first = index(start.x, start.y, start.z);
                for (int q = 0; q < n; q++) {
                    f[q] = squared[first + q * stride];
                }
                distanceTransform1D(f, d, v, z, n);
                for (int q = 0; q < n; q++) {
                    squared[first + q * stride] = d[q];
                }
            }
        }
    }
    _depth.assign(total, 0.0f);
    for (size_t i = 0; i < total; i++) {
        if (_inside[i]) {
            _depth[i] = (float)std::sqrt(squared[i]) * _size;
        }
    }
}

glm::ivec3 VoxelBody::cell(size_t i) const {
    int x = (int)(i % _dimensions.x);
    int y = (int)((i / _dimensions.x) % _dimensions.y);
    int z = (int)(i / ((size_t)_dimensions.x * _dimensions.y));
    return glm::ivec3(x, y, z);
}

glm::vec3 VoxelBody::center(size_t i) const {
    return _origin + (glm::vec3(cell(i)) + glm::vec3(0.5f)) * _size;
}

long VoxelBody::find(const glm::vec3& point) const {
    glm::vec3 g = (point - _origin) / _size;
    int x = (int)std::floor(g.x);
    int y = (int)std::floor(g.y);
    int z = (int)std::floor(g.z);
    if (x < 0 || y < 0 || z < 0 || x >= _dimensions.x || y >= _dimensions.y || z >= _dimensions.z) {
        return -1;
    }
    return (long)index(x, y, z);
}

long VoxelBody::nearestInside(const glm::vec3& point, int radius) const {
    long home = find(point);
    if (home < 0) {
        return -1;
    }
    glm::ivec3 c = cell((size_t)home);
    long best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int z = std::max(0, c.z - radius); z <= std::min(_dimensions.z - 1, c.z + radius); z++) {
        for (int y = std::max(0, c.y - radius); y <= std::min(_dimensions.y - 1, c.y + radius); y++) {
            for (int x = std::max(0, c.x - radius); x <= std::min(_dimensions.x - 1, c.x + radius); x++) {
                size_t i = index(x, y, z);
                if (_inside[i]) {
                    float distance = glm::distance(center(i), point);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = (long)i;
                    }
                }
            }
        }
    }
    return best;
}

float VoxelBody::thickness(const glm::vec3& point, const glm::vec3& direction) const {
    return reach(point, direction) + reach(point, -direction);
}

float VoxelBody::reach(const glm::vec3& point, const glm::vec3& direction) const {
    long start = find(point);
    if (start < 0 || !_inside[start] || glm::length(direction) <= 0.0f) {
        return 0.0f;
    }
    const glm::vec3 unit = glm::normalize(direction);
    const float step = 0.5f * _size;
    float t = step;
    while (true) {
        long k = find(point + unit * t);
        if (k < 0 || !_inside[k]) {
            break;
        }
        t += step;
    }
    return t - 0.5f * step;
}

VoxelBody::Paths VoxelBody::shortestPaths(const std::vector<size_t>& seeds, bool medial, float maxDistance) const {
    const size_t total = _inside.size();
    Paths paths;
    paths.distance.assign(total, std::numeric_limits<float>::infinity());
    paths.previous.assign(total, -1);

    struct Step {
        int dx, dy, dz;
        float length;
    };
    std::vector<Step> steps;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx || dy || dz) {
                    steps.push_back({ dx, dy, dz, _size * std::sqrt((float)(dx * dx + dy * dy + dz * dz)) });
                }
            }
        }
    }

    using Item = std::pair<float, size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (size_t seed : seeds) {
        if (seed < total && _inside[seed]) {
            paths.distance[seed] = 0.0f;
            open.push(Item(0.0f, seed));
        }
    }
    while (!open.empty()) {
        Item top = open.top();
        open.pop();
        const float distance = top.first;
        const size_t i = top.second;
        if (distance > paths.distance[i]) {
            continue;
        }
        if (distance > maxDistance) {
            break;
        }
        const glm::ivec3 c = cell(i);
        for (const Step& s : steps) {
            int x = c.x + s.dx;
            int y = c.y + s.dy;
            int z = c.z + s.dz;
            if (x < 0 || y < 0 || z < 0 || x >= _dimensions.x || y >= _dimensions.y || z >= _dimensions.z) {
                continue;
            }
            size_t j = index(x, y, z);
            if (!_inside[j]) {
                continue;
            }
            float weight = 1.0f;
            if (medial) {
                float voxelsDeep = std::max(_depth[j] / _size, 0.5f);
                weight = 1.0f / (voxelsDeep * voxelsDeep);
            }
            float next = distance + s.length * weight;
            if (next < paths.distance[j]) {
                paths.distance[j] = next;
                paths.previous[j] = (int32_t)i;
                open.push(Item(next, j));
            }
        }
    }
    return paths;
}

std::vector<size_t> VoxelBody::trace(const Paths& paths, size_t from) const {
    std::vector<size_t> cells;
    long i = (long)from;
    while (i >= 0 && cells.size() < _inside.size()) {
        cells.push_back((size_t)i);
        i = paths.previous[(size_t)i];
    }
    return cells;
}
