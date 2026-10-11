//
//  EditorBounds.cpp
//  interface/src/editor
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "EditorBounds.h"

#include <cfloat>
#include <cmath>

#include <QtCore/QVector>

#include <GeometryCache.h>
#include <GLMHelpers.h>
#include <PerfStat.h>
#include <RenderHifi.h>

#include "Application.h"

namespace {
    const int CIRCLE_SEGMENTS = 64;

    // every shape becomes line segments in world space, so all of them go in one draw call
    void appendBox(const EditorBoundsShape& shape, QVector<glm::vec3>& points, QVector<glm::vec4>& colors) {
        glm::vec3 half = shape.dimensions * 0.5f;
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            glm::vec3 local((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
            corners[i] = shape.position + shape.rotation * local;
        }
        // the twelve edges: corners that differ in exactly one axis bit
        for (int a = 0; a < 8; ++a) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                int b = a | bit;
                if (b != a) {
                    points << corners[a] << corners[b];
                    colors << shape.color << shape.color;
                }
            }
        }
    }

    void appendSphere(const EditorBoundsShape& shape, QVector<glm::vec3>& points, QVector<glm::vec4>& colors) {
        // three great circles, as Unity draws a light's range
        glm::vec3 radius = shape.dimensions * 0.5f;
        for (int axis = 0; axis < 3; ++axis) {
            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            glm::vec3 previous;
            for (int i = 0; i <= CIRCLE_SEGMENTS; ++i) {
                float angle = (float)i / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
                glm::vec3 local(0.0f);
                local[u] = std::cos(angle) * radius[u];
                local[v] = std::sin(angle) * radius[v];
                glm::vec3 point = shape.position + shape.rotation * local;
                if (i > 0) {
                    points << previous << point;
                    colors << shape.color << shape.color;
                }
                previous = point;
            }
        }
    }
}

namespace {
    void appendCone(const EditorBoundsShape& shape, QVector<glm::vec3>& points, QVector<glm::vec4>& colors) {
        // a spot light's beam as Unity draws it: four lines from the light out to the rim of the beam where its
        // reach ends, the rim, and two arcs across the end (the reach is a distance, so the end is curved)
        float angle = glm::clamp(shape.angle, 0.0f, (float)M_PI_2);
        glm::vec3 forward = shape.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 right = shape.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = shape.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        auto line = [&](const glm::vec3& a, const glm::vec3& b) {
            points << a << b;
            colors << shape.color << shape.color;
        };
        glm::vec3 rimCenter = shape.position + forward * (shape.range * std::cos(angle));
        float rimRadius = shape.range * std::sin(angle);
        glm::vec3 previous;
        for (int i = 0; i <= CIRCLE_SEGMENTS; ++i) {
            float around = (float)i / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
            glm::vec3 point = rimCenter + (right * std::cos(around) + up * std::sin(around)) * rimRadius;
            if (i > 0) {
                line(previous, point);
            }
            if (i % (CIRCLE_SEGMENTS / 4) == 0 && i < CIRCLE_SEGMENTS) {
                line(shape.position, point);
            }
            previous = point;
        }
        for (const glm::vec3& side : { right, up }) {
            const int segments = CIRCLE_SEGMENTS / 2;
            for (int i = 0; i <= segments; ++i) {
                float tilt = angle * (2.0f * (float)i / segments - 1.0f);
                glm::vec3 point = shape.position + (forward * std::cos(tilt) + side * std::sin(tilt)) * shape.range;
                if (i > 0) {
                    line(previous, point);
                }
                previous = point;
            }
        }
    }
}

void EditorBoundsShared::setShapes(const std::vector<EditorBoundsShape>& shapes) {
    std::lock_guard<std::mutex> lock(_mutex);
    _shapes = shapes;
}

render::Item::Bound EditorBoundsShared::bound() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_shapes.empty()) {
        return render::Item::Bound(glm::vec3(0.0f), 0.0f);
    }
    glm::vec3 low(FLT_MAX);
    glm::vec3 high(-FLT_MAX);
    for (const auto& shape : _shapes) {
        float reach = glm::length(shape.dimensions) * 0.5f;
        low = glm::min(low, shape.position - glm::vec3(reach));
        high = glm::max(high, shape.position + glm::vec3(reach));
    }
    return render::Item::Bound(low, high - low);
}

void EditorBoundsShared::render(RenderArgs* args) {
    if (!args || !args->_batch) {
        return;
    }
    std::vector<EditorBoundsShape> shapes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        shapes = _shapes;
    }
    if (shapes.empty()) {
        return;
    }
    PerformanceTimer perfTimer("editorBounds");
    QVector<glm::vec3> points;
    QVector<glm::vec4> colors;
    for (const auto& shape : shapes) {
        if (shape.kind == EditorBoundsShape::Sphere) {
            appendSphere(shape, points, colors);
        } else if (shape.kind == EditorBoundsShape::Cone) {
            appendCone(shape, points, colors);
        } else {
            appendBox(shape, points, colors);
        }
    }
    auto geometryCache = DependencyManager::get<GeometryCache>();
    if (_linesId < 0) {
        _linesId = geometryCache->allocateID();
    }
    auto& batch = *args->_batch;
    geometryCache->bindSimpleProgram(batch, false, false, true, false, true, true, graphics::MaterialKey::CULL_NONE);
    batch.setModelTransform(Transform());
    geometryCache->updateVertices(_linesId, points, colors);
    geometryCache->renderVertices(batch, gpu::LINES, _linesId);
}

namespace render {
    template <> const ItemKey payloadGetKey(const EditorBoundsRenderData::Pointer& data) {
        return ItemKey::Builder::opaqueShape().withLayer(render::hifi::LAYER_3D_FRONT)
            .withTagBits(ItemKey::TAG_BITS_0).withoutShadowCaster();
    }
    template <> const Item::Bound payloadGetBound(const EditorBoundsRenderData::Pointer& data) {
        return data ? data->_shared->bound() : Item::Bound();
    }
    template <> void payloadRender(const EditorBoundsRenderData::Pointer& data, RenderArgs* args) {
        if (data) {
            data->_shared->render(args);
        }
    }
}

void EditorBounds::ensureItem() {
    if (render::Item::isValidID(_item)) {
        return;
    }
    auto scene = qApp->getMain3DScene();
    if (!scene) {
        return;
    }
    render::Transaction transaction;
    _item = scene->allocateID();
    transaction.resetItem(_item, std::make_shared<EditorBoundsRenderData::Payload>(
        std::make_shared<EditorBoundsRenderData>(_shared)));
    scene->enqueueTransaction(transaction);
}

void EditorBounds::setShapes(const std::vector<EditorBoundsShape>& shapes) {
    if (shapes.empty() && _empty) {
        return;
    }
    _empty = shapes.empty();
    _shared->setShapes(shapes);
    if (!_empty) {
        ensureItem();
    }
    // the item's bound moved with the shapes: let the scene know
    if (render::Item::isValidID(_item)) {
        if (auto scene = qApp->getMain3DScene()) {
            render::Transaction transaction;
            transaction.updateItem(_item);
            scene->enqueueTransaction(transaction);
        }
    }
}
