#! /usr/bin/env python

# Try to decode profile.raw (on stdin) into something a bit more
# human-readable.
import subprocess
import sys

def lookup_addr(addr):
    f = False
    a = addr
    for (start, end, offset, path) in maps:
        if start <= addr < end:
            bin = path
            if f:
                a -= start
            break
        f = True
    else:
        path = "./test-t"
    p = subprocess.Popen("addr2line -fie %s 0x%x | c++filt" % (bin, a),
                         stdout = subprocess.PIPE,
                         shell = True)
    return ["%x" % addr] + p.communicate()[0].split("\n")
    
samples = []
discard = None
maps = []
for l in sys.stdin.xreadlines():
    w = l.split()
    if w[0] == "discard":
        assert discard == None
        discard = int(w[1])
    elif w[0] == "map":
        maps.append((int(w[1], 16), int(w[2], 16), int(w[3], 16), w[4]))
    elif w[0] == "sample":
        samples.append((int(w[1]), lookup_addr(int(w[2], 16))))
    else:
        print "unknown line type %s" % w[0]
        sys.exit(1)

print "discard %d" % discard
samples.sort()
total = 0
for (c, _v) in samples:
    total += c
for (c, v) in samples:
    print "%10f %s" % ((c * 100.0) / total, v[0])
    for l in v[1:]:
        print "%10s %s" % ("", l)
