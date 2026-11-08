/*
 * neo-pi - Neo Geo CD is out of scope: these stubs replace geo_disc.c and
 * geo_cue.c (CUE/FLAC parsing) so the cartridge core links without them.
 */
#include <stdint.h>
#include "geo_disc.h"

int geo_disc_open(const char *path) { (void)path; return 0; }
void geo_disc_close(void) { }
int geo_disc_read_sector(uint32_t lba, uint8_t *buf) { (void)lba; (void)buf; return 0; }
int geo_disc_read_audio(uint32_t lba, int16_t *buf) { (void)lba; (void)buf; return 0; }
unsigned geo_disc_num_tracks(void) { return 0; }
int geo_disc_track_is_audio(unsigned track) { (void)track; return 0; }
uint32_t geo_disc_track_start(unsigned track) { (void)track; return 0; }
uint32_t geo_disc_track_frames(unsigned track) { (void)track; return 0; }
uint32_t geo_disc_leadout(void) { return 0; }

void geo_disc_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f) {
    lba += 150;
    *m = (uint8_t)(lba / (60 * 75));
    *s = (uint8_t)((lba / 75) % 60);
    *f = (uint8_t)(lba % 75);
}

uint32_t geo_disc_msf_to_lba(uint8_t m, uint8_t s, uint8_t f) {
    return (uint32_t)((m * 60 + s) * 75 + f) - 150;
}
