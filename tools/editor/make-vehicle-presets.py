#!/usr/bin/env python3
#
#  make-vehicle-presets.py
#  tools/editor
#
#  Created on 2026-10-25 for Build mode's Create > Special items > Vehicles.
#
#  Distributed under the MIT License.
#  See the accompanying file LICENSE-MIT or https://opensource.org/licenses/MIT
#
#  Writes the vehicles Build mode makes ready to drive (interface/resources/editor/vehicles/*.json): entity files as an
#  export writes them, each with its ground at y 0 and its front towards -z. The values are the ones physics tuned for
#  the test rigs: tests-manual/physics-baseline/landrover.js (the car), aircraft-rig.js and airship-rig.js, and the
#  monorail's beam and car. Run it again after a retune:
#      tools/editor/make-vehicle-presets.py [out dir]
#  A vehicle's type goes on in the file as it is; Build mode makes it in two steps (EditorUndo::createEntities).

import json
import math
import os
import sys
import uuid

VERSION = 141   # main's entity file version (an export's "Version")


def new_id():
    return "{%s}" % uuid.uuid4()


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


def roll(degrees):
    half = math.radians(degrees) * 0.5
    return {"x": 0.0, "y": 0.0, "z": math.sin(half), "w": math.cos(half)}


def part(parent, name, local_position, dimensions, color, collisionless=True, **more):
    # a child in its parent's frame: an export writes a child's position and rotation local to its parent
    entity = {"id": new_id(), "type": "Box", "shape": "Cube", "name": name, "parentID": parent["id"],
              "position": local_position, "dimensions": dimensions, "color": color, "dynamic": False,
              "collisionless": collisionless, "grab": {"grabbable": False}}
    entity.update(more)
    return entity


def car():
    # the four-seat car of landrover.js: a 2.235 m wheelbase on 0.37 m wheels, rear drive
    wheel_radius, wheel_width, wheelbase, track, travel = 0.37, 0.16, 2.235, 1.308, 0.18
    chassis_dimensions = vec(1.68, 1.1, 3.62)
    chassis_above_ground = 1.0
    wheel_top = (wheel_radius + travel * 0.5) - chassis_above_ground
    wheels = []
    for name, side, end in (("FL", -1, -1), ("FR", 1, -1), ("RL", -1, 1), ("RR", 1, 1)):
        front = end < 0
        wheels.append({"name": name, "center": [side * track * 0.5, wheel_top, end * wheelbase * 0.5],
                       "radius": wheel_radius, "width": wheel_width, "mass": 25, "suspensionDistance": travel,
                       "targetPosition": 0.5, "steer": 1 if front else 0, "brake": 1, "handBrake": 0 if front else 1})
    seats = [
        {"name": "driver", "position": [0.42, -0.05, -0.35], "yaw": 0, "driver": True},
        {"name": "passenger", "position": [-0.42, -0.05, -0.35], "yaw": 0},
        {"name": "rear left", "position": [-0.42, -0.05, 0.75], "yaw": 0},
        {"name": "rear right", "position": [0.42, -0.05, 0.75], "yaw": 0},
    ]
    chassis = {
        "id": new_id(), "type": "Box", "shape": "Cube", "name": "Car", "position": vec(0, chassis_above_ground + 0.05, 0),
        "dimensions": chassis_dimensions, "color": {"red": 200, "green": 96, "blue": 28}, "dynamic": True,
        "collisionless": False, "gravity": vec(0, -9.8, 0), "damping": 0.02, "angularDamping": 0.1, "friction": 0.6,
        "restitution": 0.0, "grab": {"grabbable": False},
        "vehicle": {
            "type": "wheeled", "mass": 1320, "centerOfMass": vec(0, -0.3, 0),
            "wheels": json.dumps(wheels), "seats": json.dumps(seats),
            "maxSteerAngle": 32, "maxTorque": 163, "minRPM": 700, "maxRPM": 4500,
            "gearRatios": json.dumps([3.68, 2.22, 1.5, 1.0]), "reverseGearRatio": 4.02, "finalDriveRatio": 4.7 * 1.148,
            "maxBrakeTorque": 1500, "maxHandBrakeTorque": 2500, "driveType": "rear",
            "suspensionFrequency": 1.4, "suspensionDamping": 0.6,
        },
    }
    entities = [chassis]
    for wheel in wheels:
        x, y, z = wheel["center"]
        entities.append({
            "id": new_id(), "type": "Shape", "shape": "Cylinder", "name": "Car wheel " + wheel["name"],
            "parentID": chassis["id"], "position": vec(x, y - wheel["suspensionDistance"] * 0.5, z), "rotation": roll(90),
            "dimensions": vec(2 * wheel["radius"], wheel["width"], 2 * wheel["radius"]),
            "color": {"red": 16, "green": 16, "blue": 22}, "dynamic": False, "collisionless": True,
            "vehicle": {"type": "wheel", "wheel": wheel["name"], "axle": "y"},
        })
    return entities


def aircraft():
    # the tilt-thrust aircraft of aircraft-rig.js: a 5 m body, an 11 m wing, a seat at the stick; flight assistance is
    # vehicles.js's, for whoever sits in it
    red = {"red": 180, "green": 40, "blue": 40}
    hull = {
        "id": new_id(), "type": "Box", "shape": "Cube", "name": "Aircraft", "position": vec(0, 0.2 + 0.05, 0),
        "dimensions": vec(2.4, 0.4, 5.0), "color": {"red": 200, "green": 200, "blue": 210}, "dynamic": True,
        "collisionless": False, "gravity": vec(0, -9.8, 0), "damping": 0.0, "angularDamping": 0.05, "friction": 0.8,
        "restitution": 0.0, "frame": True, "grab": {"grabbable": False},
        "vehicle": {
            "type": "aircraft", "mass": 1200, "centerOfMass": vec(0, -0.1, 0),
            "seats": json.dumps([{"name": "stick", "position": [0, 0.2, -1.5], "yaw": 0, "driver": True}]),
            "maxThrust": 20000, "thrustPoint": vec(0, 0.2, 0.5), "wingArea": 24, "wingSpan": 11,
            "wingPoint": vec(0, 0.2, 0.3), "elevatorArea": 4, "rudderArea": 2, "finPoint": vec(0, 0.5, 4.5),
            "maxFinAngle": 30, "dragArea": vec(3, 8, 0.8), "angularDragArea": vec(800, 800, 600), "tilt": 1,
        },
    }
    rail = {"red": 80, "green": 80, "blue": 90}
    return [
        hull,
        part(hull, "Aircraft wing", vec(0, 0.25, 0.3), vec(11, 0.1, 2.2), red),
        part(hull, "Aircraft fin", vec(0, 0.9, 2.3), vec(0.1, 1.4, 1.0), red),
        part(hull, "Aircraft tailplane", vec(0, 0.5, 2.3), vec(3.0, 0.1, 0.8), red),
        part(hull, "Aircraft rail", vec(1.15, 0.6, 0), vec(0.1, 0.8, 5.0), rail, collisionless=False),
        part(hull, "Aircraft rail", vec(-1.15, 0.6, 0), vec(0.1, 0.8, 5.0), rail, collisionless=False),
        part(hull, "Aircraft rail", vec(0, 0.6, -2.45), vec(2.4, 0.8, 0.1), rail, collisionless=False),
        part(hull, "Aircraft rail", vec(0, 0.6, 2.45), vec(2.4, 0.8, 0.1), rail, collisionless=False),
        part(hull, "Aircraft helm", vec(0, 0.7, -2.3), vec(0.3, 1.0, 0.3), {"red": 60, "green": 60, "blue": 70},
             collisionless=False),
    ]


def airship():
    # the small airship of airship-rig.js: a 14 m deck under a 36 m envelope. Its ballast and fill matter: without
    # the ballast it is 219 kg lighter than air and floats away
    envelope_center = vec(0, 7, 0)
    fin_point = vec(0, 7, 17)
    red = {"red": 180, "green": 40, "blue": 40}
    wood = {"red": 80, "green": 60, "blue": 30}
    hull = {
        "id": new_id(), "type": "Box", "shape": "Cube", "name": "Airship", "position": vec(0, 0.2 + 0.05, 0),
        "dimensions": vec(4, 0.4, 14), "color": {"red": 120, "green": 90, "blue": 50}, "dynamic": True,
        "collisionless": False, "gravity": vec(0, -9.8, 0), "damping": 0.0, "angularDamping": 0.05, "friction": 0.6,
        "restitution": 0.0, "frame": True, "grab": {"grabbable": False},
        "vehicle": {
            "type": "airship", "mass": 1350, "centerOfMass": vec(0, -0.3, 0),
            "seats": json.dumps([{"name": "helm", "position": [0, 0.2, -4.6], "yaw": 0, "driver": True}]),
            "envelopeVolume": 1500, "centerOfBuoyancy": envelope_center, "gasDensity": 0.1786, "maxThrust": 4000,
            "maxVerticalThrust": 3000, "thrustPoint": vec(0, 0.5, 2), "dragArea": vec(450, 300, 12),
            "angularDragArea": vec(400000, 400000, 20000), "rudderArea": 12, "elevatorArea": 12, "finPoint": fin_point,
            "maxFinAngle": 25, "ballastRate": 20, "ventRate": 0.01, "ballast": 250, "fill": 1,
        },
    }
    return [
        hull,
        part(hull, "Airship envelope", envelope_center, vec(9, 9, 36), {"red": 220, "green": 220, "blue": 210},
             type="Shape", shape="Sphere"),
        part(hull, "Airship fin", fin_point, vec(0.2, 6, 5), red),
        part(hull, "Airship fin", fin_point, vec(6, 0.2, 5), red),
        part(hull, "Airship rail", vec(1.95, 0.7, 0), vec(0.1, 1.0, 14), wood, collisionless=False),
        part(hull, "Airship rail", vec(-1.95, 0.7, 0), vec(0.1, 1.0, 14), wood, collisionless=False),
        part(hull, "Airship rail", vec(0, 0.7, 6.95), vec(4, 1.0, 0.1), wood, collisionless=False),
        part(hull, "Airship rail", vec(0, 0.7, -6.95), vec(4, 1.0, 0.1), wood, collisionless=False),
        part(hull, "Airship helm", vec(0, 0.75, -5.6), vec(0.3, 1.1, 0.3), {"red": 60, "green": 40, "blue": 20},
             collisionless=False),
    ]


def rail():
    # a short closed loop of monorail beam, 4 m up on pylons (2.2 m of headroom under the beam): two 10 m straights
    # joined by 10 m-radius half circles, at 6 m/s (0.37 g in the turns: the train doesn't slow for curves, and the
    # beam isn't banked); a station mid-straight, its first point, where the car waits 8 s; and the car there.
    # Not boardable yet: the cabin's walls and roof and the station's platform and ramp are still to come
    height = 4.0
    radius, straight = 10.0, 10.0
    half = straight * 0.5
    # the station mid-straight (physics, 10-25): a car stopped at a straight's end would stand half in the curve
    points = [(radius, 0, 0.0), (radius, 0, -half)]
    for degrees in (45, 90, 135):
        a = math.radians(degrees)
        points.append((radius * math.cos(a), 0, -half - radius * math.sin(a)))
    points += [(-radius, 0, -half), (-radius, 0, half)]
    for degrees in (225, 270, 315):
        a = math.radians(degrees)
        points.append((radius * math.cos(a), 0, half - radius * math.sin(a)))
    points.append((radius, 0, half))
    points = [(round(x, 3), y, round(z, 3)) for x, y, z in points]
    dwell = [8.0] + [0.0] * (len(points) - 1)
    track = {
        "id": new_id(), "type": "Track", "name": "Monorail loop", "position": vec(0, height, 0),
        # the box covers the pylons down to the floor too: the entity server sends a client what the box says
        "dimensions": vec(2 * radius + 0.9 + 2, 2 * height + 2, straight + 2 * radius + 0.9 + 2),
        "trackPoints": [vec(*p) for p in points], "trackBank": [0.0] * len(points), "trackDwell": dwell,
        "trackClosed": True, "trackSpeed": 6, "trackAcceleration": 1.2, "trackProfile": "beam", "trackWidth": 0.9,
        "trackHeight": 1.1, "trackClearance": 0.7, "trackPylonSpacing": 12, "trackPylonFloor": -height,
        "color": {"red": 215, "green": 215, "blue": 222},
    }
    first = points[0]
    car = {
        "id": new_id(), "type": "Box", "shape": "Cube", "name": "Monorail car",
        "position": vec(first[0], height + 1.1 * 0.5 + 0.7 + 1.4 * 0.5, first[2]), "dimensions": vec(2.4, 1.4, 6.8),
        "color": {"red": 32, "green": 52, "blue": 124}, "frame": True, "trackID": track["id"], "trackOffset": 0,
        "dynamic": False, "collisionless": False, "grab": {"grabbable": False},
    }
    return [track, car]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "interface", "resources", "editor", "vehicles")
    os.makedirs(out, exist_ok=True)
    for name, make in (("car", car), ("aircraft", aircraft), ("airship", airship), ("rail", rail)):
        path = os.path.join(out, name + ".json")
        entities = make()
        with open(path, "w") as f:
            json.dump({"DataVersion": 0, "Entities": entities, "Id": new_id(), "Version": VERSION}, f, indent=1)
            f.write("\n")
        print(f"{len(entities)} entities -> {os.path.relpath(path)}")


if __name__ == "__main__":
    main()
