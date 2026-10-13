//
//  TerrainToolBar.h
//  interface/src/editor/ui
//
//  The second row of the build toolbar while the Terrain tool is active: the sculpt modes (Raise, Lower, Smooth,
//  Flatten, Dig, Fill), the two paint modes (Paint, for the paint map, and Paint Voxels, for the voxels' own
//  materials) and the brush's size, strength, hardness and material, as Unity's terrain inspector has them.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TerrainToolBar_h
#define hifi_TerrainToolBar_h

#include <QtWidgets/QToolBar>

#include "../EditorTerrain.h"

class QAbstractSpinBox;
class QAction;
class QActionGroup;
class QDoubleSpinBox;
class QSpinBox;
class ScrubLabel;

class TerrainToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit TerrainToolBar(QWidget* parent = nullptr);

    EditorTerrainBrush brush() const;
    void setMode(terrain::SculptMode mode);
    void setRadius(float metres);

signals:
    void brushChanged();

private:
    QAction* addMode(const QString& text, const QString& toolTip, terrain::SculptMode mode);

    QActionGroup* _modes { nullptr };
    // The field's label scrubs its value when dragged sideways, as the Inspector's do: step per pixel, Shift ten
    // times that, Alt a tenth; proportional makes the step a share of the current value, for a size.
    void attachScrub(ScrubLabel* label, QAbstractSpinBox* box, double step, bool proportional);

    QDoubleSpinBox* _size { nullptr };
    QDoubleSpinBox* _strength { nullptr };
    QDoubleSpinBox* _hardness { nullptr };
    QSpinBox* _material { nullptr };
};

#endif // hifi_TerrainToolBar_h
