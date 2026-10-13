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

**Frame mode (PHYSICS.md round 2):** `run.sh <app> <results dir> frame` runs `baseline-frame.js`, which makes the cabin a moving reference frame (userData `{"frame": true}`). The engine then parents the avatar to the cabin itself whenever the avatar stands in the cabin's volume, so the "unparented" phases measure that automatic membership, the summary adds how many frames of each phase the avatar was parented and the world-position step at every parent change, and a last phase, **step-off**, rides for two seconds and then walks off the open back end of the moving cabin onto the ground. Expected on round 2: zero drift and zero stored-local jitter in every riding phase, one crossing out in step-off with a 0.5 m drop and no re-entry.

Run it from a shell, serverless:

```sh
D=$(cd "$(dirname "$0")" && pwd)   # this directory
R=/tmp/physics-baseline && mkdir -p "$R"
.../interface.app/Contents/MacOS/interface --url "file://$D/world.json" --testScript "$D/baseline.js" --testResultsLocation "$R" 2>&1 | tee "$R/client.log"
grep PHYSICS-BASELINE "$R/client.log"
```

The samples are in `$R/physics-baseline.json`. The client quits by itself when the schedule ends (about 80 s).

Caveat: the computed local position is made from two reads a moment apart (the cabin's transform and the avatar's position), so it carries up to one frame of the cabin's motion as noise (about 6 mm rms, 83 mm max at 5 m/s and 60 Hz); the stored local position has no such noise and is the number to read whenever it is printed.

**Two clients (PHYSICS.md round 3):** `run-pair.sh <app> <results dir> [udp port]` runs the frame baseline on a domain with a second client watching. Start a sandbox domain from the same build first (`~/git/tivolicloud/domain-sandbox.sh physics <build dir> 42102` in the docs repo); the mover creates the ground, the light and a station platform behind the cabin when the world lacks them, and gives every entity a lifetime. The observer (`observer.js`) steps onto the stopped cabin from the platform, rides every phase, steps off behind the cabin when the mover does and watches it go from the ground. It reads the phase from the cabin's description and prints `PHYSICS-OBSERVER` lines, per phase:

- its own stored local position's jitter while riding (the engine's value, no read race);
- the other rider's stored local position as the avatar mixer delivers it (`AvatarList.getAvatar(id).localPosition`, added for this);
- the other rider relative to itself in the cabin's frame (what a rider sees of the other; two reads a moment apart);
- the cabin's own world step per frame on this non-owning client, while riding and from the ground: the evenness of the glide;
- its crossings, with the world-position step at each.

The samples are in `<results dir>/observer/physics-observer.json` and the mover's in `<results dir>/mover/physics-baseline.json`.

Results so far are in `results/`: `2026-10-12-bullet.json` (round 0), `2026-10-12-jolt.json` (round 1), `2026-10-12-jolt-frame.json` (round 2, frame mode) and `2026-10-12-jolt-round2-plain.json` (round 2 build, plain mode).

**On a track (PHYSICS.md round 4):** `run.sh <app> <results dir> track` runs `track.js`: a closed, stadium-shaped `Track` entity (straights of 40 m, half circles of 15 m, a little bank at the apex of each curve) with two stations ten seconds each, and the cabin as a frame that rides it (`frame: true`, `trackID`), moved by the engine's track follower instead of a script. The avatar steps on from the platform while the cabin stands at the station, rides a lap and steps off onto the platform when the cabin is back. The summary (`PHYSICS-TRACK` lines) is per phase of the ride as read off the cabin's own motion (standing, accelerating, cruising, curve, braking), with the same drift and jitter numbers as the other modes, the cabin's own world step per frame, and how far from the track point the cabin stood at the far station. Samples in `<results dir>/physics-track.json`. `run-pair.sh <app> <results dir> <port> track` runs it as the mover on a domain with `observer.js` on the second client, which boards at the station and steps off when the mover does, as in round 3.

**The physics server (PHYSICS.md round 5):** `run-monorail.sh <app> <results dir> [udp port] [riders] [walkers] [loss]` is the acceptance test of the whole job. It restarts the physics sandbox from the app's build (`domain-sandbox.sh`, whose assignment-client monitor now also runs the physics server), with `bots/monorail-bot.js` as the domain's persistent scripts: twenty agents that board the cabin and sit (their parent and local position go through the avatar mixer as a real rider's do) and twenty that walk a circle on the ground below. Then `observer.js` runs under `TIVOLI_PACKET_LOSS=0.05` (five per cent of its datagrams dropped, in and out; a test knob in the UDP socket, off unless set) and `track.js` makes the track and the cabin and rides a lap. The server takes the cabin from its creator at simulation priority 255 (`track.js` logs the owner whenever it changes, with the train state the server publishes), drives the train and publishes the train's state on the Track entity; both clients run the same follower on from it, so neither is the owner any more and the cabin's glide on each is the number to read: `cabin world step` (its mean, its rms deviation and how many steps are over twice the mean, which are corrections landing). The observer also reports the crowd (avatars seen, riders aboard) and the riders' stored local positions in aggregate. Before the start the harness stops the sandbox and strips every `baseline-*` entity of an earlier run from its world file (their lifetime is 600 s, and the server would resume their train); the mover also deletes what it can see, and the observer and the bots board the newest cabin made since they started and keep it. The sandbox's logs of the run are copied beside the results (the physics server's lines are in the assignment-client log, under `[physics-server]`). At the end the bots are taken out of the sandbox's settings and it is restarted clean. Bots only while Auxi's mind is off, and never in the main world.
