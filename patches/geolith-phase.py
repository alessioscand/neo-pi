import sys
# neo-pi: where the emulated frame starts (input lag, one frame less).
#
# Geolith's geo_exec() runs one frame from wherever the video chip happens to
# be when the core starts: after a reset that is line 0, so the vertical blank
# (IRQ at line 249, when games read the pads) comes at the very end of the
# frame, and the lines that show the game's answer are only drawn by the next
# geo_exec(): one whole frame later than on a real Neo Geo.
#
# geo_exec_rephase(line) runs the core until the video chip enters `line` and
# makes that point the start of the frame. With line 248 (the first line after
# the picture) every geo_exec() runs: vertical blank (pads read) -> all the
# visible lines, so the frame shown at the next vsync already answers the pads
# read just before it. Nothing else changes: same cycles, same timings, only
# where the frontend cuts the stream of frames. line 0 = the old behaviour.
#
# Applied to the copies of geo.c and geo_lspc.c in the build directory, after
# geolith-mc.py. Usage: python3 patches/geolith-phase.py build/rpi4
d = sys.argv[1]

def patch(name, pairs):
    p = f'{d}/{name}'
    t = open(p).read()
    if 'geo_exec_rephase' in t or 'geo_lspc_get_scanline' in t:
        return
    for a, b in pairs:
        assert t.count(a) == 1, (name, a[:70])
        t = t.replace(a, b)
    open(p, 'w').write(t)

# geo_lspc.c: the current line of the video chip, and a hook called when it
# changes (beam racing: the frontend waits there for the HDMI scan-out)
patch('geo_lspc.c', [('void geo_lspc_init(void) {', '''// neo-pi: line the video chip is on (geo_exec_rephase)
unsigned geo_lspc_get_scanline(void) {
    return lspc.scanline;
}

// neo-pi: called on the emulating core each time the line counter changes
void (*geo_lspc_line_hook)(unsigned scanline) = NULL;

void geo_lspc_init(void) {'''),
    ('''            lspc.scanline = (lspc.scanline + 1) % LSPC_SCANLINES;
        }''', '''            lspc.scanline = (lspc.scanline + 1) % LSPC_SCANLINES;
            if (geo_lspc_line_hook)
                geo_lspc_line_hook(lspc.scanline);
        }''')])

# geo.c: the body of the frame loop becomes one step (inlined, same code),
# shared by geo_exec() and geo_exec_rephase()
LOOP_HEAD = '''void geo_exec(void) {
    while (mcycs < MCYC_PER_FRAME) {
'''
LOOP_TAIL = '''        }
    }

    mcycs %= MCYC_PER_FRAME;'''

p = f'{d}/geo.c'
t = open(p).read()
if 'geo_exec_rephase' not in t:
    assert t.count(LOOP_HEAD) == 1 and t.count(LOOP_TAIL) == 1
    i = t.index(LOOP_HEAD)
    j = t.index(LOOP_TAIL, i)
    body = t[i + len(LOOP_HEAD):j + len('        }\n')]
    # body lines are indented for the while loop: one level less in the function
    body = ''.join(l[4:] if l.startswith('    ') else l for l in body.splitlines(True))
    step = 'static inline void geo_exec_step(void) {\n' + body + '}\n\n'
    end = t.index('\n}\n', j) + 3
    rest = t[j + len(LOOP_TAIL):end]
    new_exec = ('void geo_exec(void) {\n'
                '    while (mcycs < MCYC_PER_FRAME)\n'
                '        geo_exec_step();\n\n'
                '    mcycs %= MCYC_PER_FRAME;' + rest)
    rephase = '''
// neo-pi: run until the video chip enters `line` (its counter changes there),
// then make that point the start of the frame. Less than two frames of
// emulated time; the sound made meanwhile goes to the frontend as usual.
unsigned geo_lspc_get_scanline(void);

static inline void geo_exec_rephase_step(void) {
    geo_exec_step();
    if (mcycs >= MCYC_PER_FRAME) {
        mcycs -= MCYC_PER_FRAME;
        zcycs -= MCYC_PER_FRAME;
        geo_mixer_output(ymsamps);
        ymsamps = 0;
    }
}

void geo_exec_rephase(unsigned line) {
    while (geo_lspc_get_scanline() == line) // already inside it: go round once
        geo_exec_rephase_step();
    while (geo_lspc_get_scanline() != line)
        geo_exec_rephase_step();
    zcycs -= mcycs; // the Z80 keeps its small lead over the 68000
    mcycs = 0;
    geo_mixer_output(ymsamps);
    ymsamps = 0;
}
'''
    t = t[:i] + step + new_exec + rephase + t[end:]
    open(p, 'w').write(t)
print(f'{d}: frame phase patch ok')
