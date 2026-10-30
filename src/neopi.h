/*
 * neo-pi - interface shared by the C shim and the C++ frontend
 */
#ifndef NEOPI_H
#define NEOPI_H

#define NEOPI_VERSION	"0.6.3"

/* Error codes = number of green LED blinks when the screen is unusable */
#define NEOPI_ERR_FB    2   /* framebuffer / HDMI */
#define NEOPI_ERR_SD    3   /* SD card or FAT filesystem */
#define NEOPI_ERR_BIOS  4   /* neopi/neogeo.zip missing or invalid */
#define NEOPI_ERR_GAME  5   /* neopi/game.neo missing or invalid */
#define NEOPI_ERR_CORE  6   /* emulator core error */
#define NEOPI_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void neopi_log(const char *fmt, ...);
void neopi_fatal(int code, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
