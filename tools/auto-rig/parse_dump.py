"""Turn skeleton-dump output into JSON: index, name, absolute and relative default pose (trans cm, rot xyzw)."""
import json, re, sys
rows, cur = [], None
for line in open(sys.argv[1], errors="replace"):
    line = re.sub(r"^.*\[hifi\.animation\]\s*", "", line.rstrip())
    m = re.match(r"index = (\d+)", line)
    if m:
        cur = {"index": int(m.group(1))}; rows.append(cur); continue
    if cur is None:
        continue
    m = re.match(r'name = "(.*)"', line)
    if m:
        cur["name"] = m.group(1); continue
    m = re.match(r"(abs|rel)DefaultPose = AnimPose, trans = \( (\S+) (\S+) (\S+) \), rot = \( (\S+) (\S+) (\S+) (\S+) \)", line)
    if m:
        cur[m.group(1)] = {"t": [float(v) for v in m.group(2, 3, 4)], "r": [float(v) for v in m.group(5, 6, 7, 8)]}
json.dump(rows, open(sys.argv[2], "w"), indent=0)
print(len(rows), "joints ->", sys.argv[2])
