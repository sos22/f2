#! /usr/bin/env python

# Try to decode profile.raw (on stdin) into something a bit more
# human-readable.
import subprocess
import sys

def lookup_addr(addr):
    p = subprocess.Popen("addr2line -fie ./test-t 0x%x | c++filt" % addr,
                         stdout = subprocess.PIPE,
                         shell = True)
    return p.communicate()[0].split("\n")
    
samples = []
discard = None
for l in sys.stdin.xreadlines():
    w = l.split("=")
    if len(w) == 2:
        assert w[0] == "discard"
        assert discard == None
        discard = int(w[1])
        continue
    assert len(w) == 1
    w = l.split()
    samples.append((int(w[0]), lookup_addr(int(w[1], 16))))

print "discard %d" % discard
samples.sort()
total = 0
for (c, _v) in samples:
    total += c
for (c, v) in samples:
    print "%20f %s" % ((c * 100.0) / total, v[0])
    for l in v[1:]:
        print "%20s %s" % ("", l)
