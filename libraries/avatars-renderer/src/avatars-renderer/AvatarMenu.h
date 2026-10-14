//
//  AvatarMenu.h
//  libraries/avatars-renderer/src/avatars-renderer
//
//  Created on 2026-10-14 for Darkride's import of avatars from Unity packages.
//
//  Distributed under the MIT License.
//  See the accompanying file LICENSE-MIT or https://opensource.org/licenses/MIT
//

#ifndef hifi_AvatarMenu_h
#define hifi_AvatarMenu_h

#include <mutex>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <hfm/HFM.h>

class AnimSkeleton;

// An avatar's menu, the parameters it sets, and the effects layer's state machines those parameters drive, from the
// .fst's "avatarMenu" line (the avatar import writes it; unity-import's AvatarMenuImport.h has the
// format). Each layer follows its transitions as the parameters change, and each state's effect sets what the avatar's
// animator would: objects shown or hidden, the model's own blend shapes, and joints moved, turned or scaled. A state
// that writes defaults first puts back what its layer animates. Effects are applied at once (a transition's blend
// time is not yet), and exit times count from entering the state.
class AvatarMenu {
public:
    struct JointPose {
        bool hasTranslation { false };
        bool hasRotation { false };
        bool hasScale { false };
        glm::vec3 translation;
        glm::quat rotation;
        glm::vec3 scale { 1.0f };
    };

    // Reads the data and resolves it against the avatar's model and skeleton; empty data clears.
    void setup(const QJsonObject& data, const hfm::Model& model, const AnimSkeleton& skeleton);
    void clear();
    bool isActive() const;

    // For scripts and the menu: the menu as the data has it, and each parameter with its value.
    QJsonObject getMenu() const;
    QVariantList getParameters() const;
    bool setParameter(const QString& name, float value);
    float getParameter(const QString& name, float fallback = 0.0f) const;

    // Follows the layers' transitions; true when what the avatar shows changed since the last call.
    bool update(float deltaTime);
    QSet<int> getHiddenParts() const;
    QHash<int, float> getOwnBlendshapes() const;           // index into the model's own blend shapes -> 0 to 1
    QHash<int, JointPose> getJointPoses() const;           // skeleton joint -> the parts the layers set

private:
    struct Condition {
        int parameter { -1 };
        int mode { 0 };  // 1 if, 2 ifNot, 3 greater, 4 less, 6 equals, 7 notEqual
        float threshold { 0.0f };
    };
    struct Transition {
        int to { -1 };   // -1: the state machine's exit, back to its entry
        std::vector<Condition> conditions;
        bool hasExitTime { false };
        float exitTime { 0.0f };
    };
    struct Effect {
        std::vector<std::pair<int, bool>> active;            // object -> shown
        std::vector<std::pair<int, float>> blendshapes;      // own blend shape -> weight
        std::vector<std::pair<int, JointPose>> joints;       // joint -> parts set
    };
    struct State {
        bool writeDefaults { false };
        Effect effect;
        std::vector<Transition> transitions;
    };
    struct Layer {
        int defaultState { -1 };
        std::vector<Transition> entry;
        std::vector<Transition> anyState;
        std::vector<State> states;
        int current { -1 };
        float timeInState { 0.0f };
        QSet<int> animatedObjects, animatedBlendshapes, animatedJoints;  // what any of its states sets
    };
    struct Parameter {
        QString name;
        QString type;
        float value { 0.0f };
        float defaultValue { 0.0f };
        bool saved { false };
        bool synced { false };
        bool builtin { false };
    };

    bool holds(const Transition& transition, float timeInState) const;
    int enterFromEntry(const Layer& layer) const;
    void enter(Layer& layer, int state);

    mutable std::mutex _mutex;
    QJsonObject _menu;
    std::vector<Parameter> _parameters;
    QHash<QString, int> _parameterIndex;
    std::vector<Layer> _layers;
    std::vector<std::vector<int>> _objectParts;
    // what the avatar shows now, and before any layer wrote
    std::vector<bool> _objectActive, _baseObjectActive;
    QHash<int, float> _blendshapes, _baseBlendshapes;
    QHash<int, JointPose> _joints, _baseJoints;
    bool _changed { false };
    bool _parametersChanged { false };
};

#endif // hifi_AvatarMenu_h
