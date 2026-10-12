# Physics baseline (PHYSICS.md round 0)

How well does the client, as it stands, keep an avatar on a moving platform? This test gives the number every later round has to beat.

`baseline.js` creates a cabin (a 4 × 8 m floor with low walls) in the serverless `world.json`, puts the avatar on it, and runs five phases of about ten seconds each:

| Phase | The cabin | The avatar |
|---|---|---|
| straight-unparented | velocity 5 m/s, straight | standing, not parented |
| straight-parented | velocity 5 m/s, straight | standing, parented to the cabin |
| curve-unparented | position and rotation set every frame around a 15 m circle at 5 m/s | standing, not parented |
| curve-parented | the same circle | standing, parented |
| walk-parented | velocity 5 m/s, straight | parented, walking across the cabin and back with the scripted motor |

Every script frame it records the cabin's transform, the avatar's world position, the avatar's position in the cabin's frame (computed), and, when parented, the engine's own stored local position (the value other clients receive). The summary reports, per phase:

- **drift**: how far the avatar moved in the cabin's frame from the start of the phase to its end. Standing still should give zero. This is the slide.
- **jitter**: the frame-to-frame change of the avatar's local position with the phase's mean change removed, as rms and maximum, horizontal and vertical. Standing still should give zero. This is what the rider sees and what other clients receive.

Run it from a shell, serverless:

```sh
D=$(cd "$(dirname "$0")" && pwd)   # this directory
R=/tmp/physics-baseline && mkdir -p "$R"
.../interface.app/Contents/MacOS/interface --url "file://$D/world.json" --testScript "$D/baseline.js" --testResultsLocation "$R" 2>&1 | tee "$R/client.log"
grep PHYSICS-BASELINE "$R/client.log"
```

The samples are in `$R/physics-baseline.json`. The client quits by itself when the schedule ends (about 80 s).

Caveat: the computed local position is made from two reads a moment apart (the cabin's transform and the avatar's position), so for the unparented phases it carries up to one frame of the cabin's motion as noise; the stored local position in the parented phases has no such noise.
