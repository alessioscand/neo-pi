import re, sys
# neo-pi (experimental): the video chip's line drawing on core 1.
#
# Geolith draws the picture one line at a time in geo_lspc_scanline(), always
# at the same point of each line, and nothing in the console ever reads the
# result back. Core 0 (68000, Z80, sound) now only queues "draw line N" with
# the few values that change during a frame (line number, auto-animation
# counter, layer enables); core 1 draws the queued lines in order.
#
# Wake-ups: after storing the queue position, DSB then SEV. Without the DSB the
# other core can wake up before the store is visible, read the old value, go
# back to sleep and never wake again (seen on a real Pi 3: core 1 asleep at the
# end of a frame while core 0 waits for it).
#
# Rule that keeps the picture identical to single core: before anything that
# the drawing reads is changed (VRAM, palette, palette bank, shadow, fix ROM,
# buffers...), core 0 waits until core 1 has drawn every queued line. Games
# change these almost only in vertical blank, so the wait is rare.
#
# geo_lspc_mc_enabled = 0 keeps the original single core behaviour.
# Applied to the copy of geo_lspc.c in the build directory, never to
# third_party/geolith. Usage: python3 patches/geolith-mc.py build/rpi3mc/geo_lspc.c
p = sys.argv[1]
t = open(p).read()
if 'geo_lspc_mc_enabled' in t:
    sys.exit(0)

def one(a, b):
    global t
    assert t.count(a) == 1, a[:70]
    t = t.replace(a, b)

# 1. state of the line being drawn (core 1) and the queue
one('static unsigned reg_envideo = 0;\n', '''static unsigned reg_envideo = 0;

// ---- neo-pi multicore -------------------------------------------------------
unsigned geo_lspc_mc_enabled = 0;

// called by core 1 after each line it draws (neo-pi copies the finished line
// to the screen, with the scanlines, while core 0 is still emulating)
void (*geo_lspc_mc_linecb)(unsigned scanline) = NULL;

// values of the line being drawn (copied from the queue entry)
static unsigned r_scanline, r_aa_counter, r_aa_disable, r_disblspr, r_disblfix;

typedef struct {
    unsigned scanline, aa_counter, aa_disable, disblspr, disblfix;
} mcline_t;

#define MC_RING 512 // power of 2, more than one frame of lines
static mcline_t mc_ring[MC_RING];
static volatile unsigned mc_head = 0; // written by core 0 only
static volatile unsigned mc_tail = 0; // written by core 1 only

volatile unsigned geo_lspc_mc_alive = 0;  // set by core 1 when its loop starts
unsigned geo_lspc_mc_failed = 0;          // core 1 stopped answering: single core

// diagnostics (written by core 1): heartbeat, what it is doing, lines drawn
volatile unsigned geo_lspc_mc_beat = 0;   // +1 at every turn of core 1's loop
volatile unsigned geo_lspc_mc_stage = 0;  // 0 waiting, 1 drawing a line
volatile unsigned geo_lspc_mc_line = 0;   // line being drawn / last drawn
volatile unsigned geo_lspc_mc_lines = 0;  // lines drawn by core 1
// snapshot taken by core 0 when core 1 is found stuck
unsigned geo_lspc_mc_fail_head, geo_lspc_mc_fail_tail, geo_lspc_mc_fail_stage;
unsigned geo_lspc_mc_fail_line, geo_lspc_mc_fail_lines, geo_lspc_mc_fail_beatmoved;
unsigned long long geo_lspc_mc_freq = 0;

static inline uint64_t mc_ticks(void) {
    uint64_t v;
    __asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (v));
    return v;
}

static inline uint64_t mc_freq(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
    return v;
}

static void mc_fail(void);

// core 0: wait until core 1 has drawn every queued line (at most 0.5 s:
// then core 1 is considered stuck and core 0 goes on alone)
static inline void mc_sync(void) {
    if (!geo_lspc_mc_enabled)
        return;
    if (__atomic_load_n(&mc_tail, __ATOMIC_ACQUIRE) == mc_head)
        return;
    uint64_t limit = mc_ticks() + mc_freq() / 2;
    while (__atomic_load_n(&mc_tail, __ATOMIC_ACQUIRE) != mc_head) {
        if (mc_ticks() > limit) {
            mc_fail();
            return;
        }
        __asm__ volatile ("wfe");
    }
}

void geo_lspc_mc_sync(void) {
    mc_sync();
}
''')

# 2. the drawing functions use the values of the queued line
def rename(start, end, pairs):
    global t
    i = t.index(start)
    j = t.index(end, i)
    body = t[i:j]
    for a, b in pairs:
        body = body.replace(a, b)
    t = t[:i] + body + t[j:]

R = [('lspc.scanline', 'r_scanline'), ('lspc.aa_counter', 'r_aa_counter'),
     ('lspc.aa_disable', 'r_aa_disable')]
rename('static inline void geo_lspc_bdsprline(void) {', '// Read half of a a palette RAM entry', R)
rename('static void geo_lspc_fixline_default(void) {', '// Set the active Fix ROM', R)
rename('static inline void geo_lspc_sprcalc(void) {', 'static inline void geo_lspc_aa(void) {', R)
rename('static void geo_lspc_scanline(void) {', 'void geo_lspc_run(unsigned cycs) {',
       R + [('reg_disblspr', 'r_disblspr'), ('reg_disblfix', 'r_disblfix')])

# 3. queue a line instead of drawing it; core 1's loop
one('void geo_lspc_run(unsigned cycs) {', '''// core 0: draw this line now (single core) or queue it for core 1
static void geo_lspc_mc_post(void) {
    unsigned sl = lspc.scanline;
    if (!geo_lspc_mc_enabled) {
        r_scanline = sl;
        r_aa_counter = lspc.aa_counter;
        r_aa_disable = lspc.aa_disable;
        r_disblspr = reg_disblspr;
        r_disblfix = reg_disblfix;
        geo_lspc_scanline();
        return;
    }

    // only the lines geo_lspc_scanline() draws something on
    if (!((sl >= LSPC_LINE_BORDER_TOP && sl < LSPC_LINE_BORDER_BOTTOM) ||
        sl == LSPC_LINE_BUFSTART || sl == LSPC_LINE_BUFSTART + 1))
        return;

    unsigned h = mc_head;
    if (h - __atomic_load_n(&mc_tail, __ATOMIC_ACQUIRE) >= MC_RING) {
        mc_sync(); // queue full (never in practice)
        if (!geo_lspc_mc_enabled) {
            geo_lspc_mc_post();
            return;
        }
    }

    mcline_t *e = &mc_ring[h & (MC_RING - 1)];
    e->scanline = sl;
    e->aa_counter = lspc.aa_counter;
    e->aa_disable = lspc.aa_disable;
    e->disblspr = reg_disblspr;
    e->disblfix = reg_disblfix;
    __atomic_store_n(&mc_head, h + 1, __ATOMIC_RELEASE);
    __asm__ volatile ("dsb ish; sev" ::: "memory"); // store visible before the wake-up
}

// core 1 stuck: draw what is left in the queue on core 0, then single core
static void mc_fail(void) {
    geo_lspc_mc_fail_head = mc_head;
    geo_lspc_mc_fail_tail = mc_tail;
    geo_lspc_mc_fail_stage = geo_lspc_mc_stage;
    geo_lspc_mc_fail_line = geo_lspc_mc_line;
    geo_lspc_mc_fail_lines = geo_lspc_mc_lines;
    geo_lspc_mc_freq = mc_freq();
    unsigned beat = geo_lspc_mc_beat;
    __asm__ volatile ("dsb ish; sev" ::: "memory"); // store visible before the wake-up
    uint64_t until = mc_ticks() + mc_freq() / 50; // 20 ms: is core 1 still turning?
    while (mc_ticks() < until)
        ;
    geo_lspc_mc_fail_beatmoved = geo_lspc_mc_beat - beat;
    geo_lspc_mc_enabled = 0;
    geo_lspc_mc_failed = 1;
    for (unsigned tl = mc_tail; tl != mc_head; ++tl) {
        mcline_t *e = &mc_ring[tl & (MC_RING - 1)];
        r_scanline = e->scanline;
        r_aa_counter = e->aa_counter;
        r_aa_disable = e->aa_disable;
        r_disblspr = e->disblspr;
        r_disblfix = e->disblfix;
        geo_lspc_scanline();
    }
    mc_tail = mc_head;
}

// core 1: draw the queued lines in order, until *stop is set
void geo_lspc_mc_worker(volatile unsigned *stop) {
    geo_lspc_mc_alive = 1;
    __asm__ volatile ("dsb ish; sev" ::: "memory"); // store visible before the wake-up
    for (;;) {
        unsigned tl = mc_tail;
        while (__atomic_load_n(&mc_head, __ATOMIC_ACQUIRE) == tl) {
            if (*stop)
                return;
            geo_lspc_mc_beat++;
            __asm__ volatile ("wfe");
        }
        geo_lspc_mc_beat++;
        mcline_t *e = &mc_ring[tl & (MC_RING - 1)];
        r_scanline = e->scanline;
        r_aa_counter = e->aa_counter;
        r_aa_disable = e->aa_disable;
        r_disblspr = e->disblspr;
        r_disblfix = e->disblfix;
        geo_lspc_mc_line = e->scanline;
        geo_lspc_mc_stage = 1;
        geo_lspc_scanline();
        if (geo_lspc_mc_linecb)
            geo_lspc_mc_linecb(e->scanline);
        geo_lspc_mc_stage = 0;
        geo_lspc_mc_lines++;
        __atomic_store_n(&mc_tail, tl + 1, __ATOMIC_RELEASE);
        __asm__ volatile ("dsb ish; sev" ::: "memory"); // store visible before the wake-up
    }
}

void geo_lspc_run(unsigned cycs) {''')
one('            geo_lspc_scanline();\n', '            geo_lspc_mc_post();\n')

# 4. wait for core 1 before changing anything the drawing reads
SYNC = ['geo_lspc_set_buffer', 'geo_lspc_set_sprlimit', 'geo_lspc_set_palette',
        'geo_lspc_set_skip_render', 'geo_lspc_disblspr_wr', 'geo_lspc_disblfix_wr',
        'geo_lspc_envideo_wr', 'geo_lspc_postload', 'geo_lspc_palram_wr08',
        'geo_lspc_palram_wr16', 'geo_lspc_palram_bank', 'geo_lspc_vram_wr',
        'geo_lspc_shadow_wr', 'geo_lspc_init', 'geo_lspc_set_fix',
        'geo_lspc_set_fix_banksw', 'geo_lspc_state_load', 'geo_lspc_state_save']
for f in SYNC:
    m = re.findall(r'\nvoid ' + f + r'\([^)]*\) \{\n', t)
    assert len(m) == 1, f
    t = t.replace(m[0], m[0] + '    mc_sync();\n')

open(p, 'w').write(t)
print(p, 'ok')
