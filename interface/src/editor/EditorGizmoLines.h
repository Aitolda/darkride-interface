//
//  EditorGizmoLines.h
//  interface/src/editor
//
//  Lines for gizmos, drawn as thin strips turned to face the camera so that they keep their width on screen, and
//  filled squares: everything goes into one vertex buffer, coloured per vertex, drawn as triangles.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EditorGizmoLines_h
#define hifi_EditorGizmoLines_h

#include <cmath>

#include <QtCore/QVector>

#include <glm/glm.hpp>

#include <GeometryCache.h>
#include <GLMHelpers.h>

class EditorGizmoLines {
public:
    EditorGizmoLines() {}
    explicit EditorGizmoLines(const glm::vec3& toCamera) : _toCamera(toCamera) {}

    // the direction to the camera from what is drawn next: the strips turn to face it
    void setToCamera(const glm::vec3& toCamera) { _toCamera = toCamera; }

    bool isEmpty() const { return _points.isEmpty(); }
    void clear() {
        _points.clear();
        _colors.clear();
    }

    void addLine(const QVector<glm::vec3>& line, bool closed, float width, const glm::vec4& color) {
        int count = line.size();
        if (count < 2) {
            return;
        }
        // each point is widened across the line's direction there, so the pieces of a curve meet
        QVector<glm::vec3> across(count);
        for (int i = 0; i < count; ++i) {
            const glm::vec3& previous = i > 0 ? line[i - 1] : (closed ? line[count - 1] : line[i]);
            const glm::vec3& next = i < count - 1 ? line[i + 1] : (closed ? line[0] : line[i]);
            across[i] = acrossLine(next - previous, width * 0.5f);
        }
        int pieces = closed ? count : count - 1;
        for (int i = 0; i < pieces; ++i) {
            int j = (i + 1) % count;
            glm::vec3 a0 = line[i] + across[i];
            glm::vec3 a1 = line[i] - across[i];
            glm::vec3 b0 = line[j] + across[j];
            glm::vec3 b1 = line[j] - across[j];
            push(a0, color);
            push(a1, color);
            push(b0, color);
            push(b0, color);
            push(a1, color);
            push(b1, color);
        }
    }

    // a straight line, longer by half its width at each end so that lines meeting at a corner close it
    void addSegment(const glm::vec3& from, const glm::vec3& to, float width, const glm::vec4& color) {
        glm::vec3 direction = to - from;
        float length = glm::length(direction);
        if (length < 1.0e-6f) {
            return;
        }
        glm::vec3 extra = direction * (width * 0.5f / length);
        addLine({ from - extra, to + extra }, false, width, color);
    }

    void addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color) {
        push(a, color);
        push(b, color);
        push(c, color);
        push(a, color);
        push(c, color);
        push(d, color);
    }

    void draw(gpu::Batch& batch, GeometryCache& geometryCache, int bufferId) const {
        geometryCache.updateVertices(bufferId, _points, _colors);
        geometryCache.renderVertices(batch, gpu::TRIANGLES, bufferId);
    }

private:
    // half the line's width, across `direction` and facing the camera
    glm::vec3 acrossLine(const glm::vec3& direction, float halfWidth) const {
        glm::vec3 across = glm::cross(direction, _toCamera);
        float length = glm::length(across);
        if (length < 1.0e-6f) {
            // seen end on: any direction across it will do
            across = glm::cross(direction, std::abs(direction.y) < 0.9f * glm::length(direction) ? Vectors::UNIT_Y : Vectors::UNIT_X);
            length = glm::length(across);
            if (length < 1.0e-6f) {
                return glm::vec3(0.0f);
            }
        }
        return across * (halfWidth / length);
    }

    void push(const glm::vec3& point, const glm::vec4& color) {
        _points << point;
        _colors << color;
    }

    glm::vec3 _toCamera { 0.0f, 0.0f, 1.0f };
    QVector<glm::vec3> _points;
    QVector<glm::vec4> _colors;
};

#endif // hifi_EditorGizmoLines_h
