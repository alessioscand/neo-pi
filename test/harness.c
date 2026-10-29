/* neo-pi host harness: same core + same setup as kernel.cpp, dumps frames (PPM) and audio (WAV) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "geo.h"
#include "geo_lspc.h"
#include "geo_mixer.h"
#include "geo_neo.h"

static uint32_t vbuf[320*264];
static int16_t abuf[8192];
static FILE *wav; static size_t wavsamples; static size_t lastsamps;
static void audio_cb(size_t n) { fwrite(abuf, 2, n, wav); wavsamples += n; lastsamps = n; }
static void log_cb(int l, const char *f, ...) { if (l == GEO_LOG_DBG) return; va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); }
static int cmp(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static int g_frame; static int test_at=-1, test_len=10, dip1_until=-1; static char *keys=0;
/* keys: "frame:code,frame:code" code u d l r a b c D s(start) k(coin) */
static unsigned js(unsigned p) { if (p||!keys) return 0xff; unsigned v=0xff; char *k=keys; while (*k){ int f=atoi(k); char *c=strchr(k,':'); if(!c)break; char code=c[1];
 if (g_frame>=f && g_frame<f+6){ switch(code){case 'u':v&=~1u;break;case 'd':v&=~2u;break;case 'l':v&=~4u;break;case 'r':v&=~8u;break;case 'a':v&=~16u;break;case 'b':v&=~32u;break;case 'c':v&=~64u;break;case 'D':v&=~128u;break;} }
 char *n=strchr(k,','); if(!n)break; k=n+1;} return v; }
static unsigned startbits(void){ unsigned v=0x0f|0x30; if(!keys)return v; char *k=keys; while(*k){int f=atoi(k); char *c=strchr(k,':'); if(!c)break; if(c[1]=='s'&&g_frame>=f&&g_frame<f+6) v&=~1u; char *n=strchr(k,','); if(!n)break; k=n+1;} return v; }
static unsigned sa(void) { unsigned v=0x07|0x18; if(!keys)return v; char *k=keys; while(*k){int f=atoi(k); char *c=strchr(k,':'); if(!c)break; if(c[1]=='k'&&g_frame>=f&&g_frame<f+8) v&=~1u; char *n=strchr(k,','); if(!n)break; k=n+1;} return v; }
static unsigned sb(void) { return startbits(); }
static unsigned st(void) { return (test_at>=0 && g_frame>=test_at && g_frame<test_at+test_len) ? 0x40 : 0xc0; }
static unsigned dip(void) { return (g_frame < dip1_until) ? 0xfe : 0xff; }
static unsigned un(void) { return 0xff; }
static void *rd(const char *p, size_t *n) { FILE *f=fopen(p,"rb"); if(!f){perror(p);exit(1);} fseek(f,0,SEEK_END); *n=ftell(f); rewind(f); void *b=malloc(*n); if(fread(b,1,*n,f)!=*n)exit(1); fclose(f); return b; }
static void wr_hdr(FILE *f, size_t samples) { uint32_t dl=samples*2; fseek(f,0,SEEK_SET);
  fwrite("RIFF",1,4,f); uint32_t v=36+dl; fwrite(&v,4,1,f); fwrite("WAVEfmt ",1,8,f); v=16; fwrite(&v,4,1,f);
  uint16_t s=1; fwrite(&s,2,1,f); s=2; fwrite(&s,2,1,f); v=48000; fwrite(&v,4,1,f); v=48000*4; fwrite(&v,4,1,f); s=4; fwrite(&s,2,1,f); s=16; fwrite(&s,2,1,f);
  fwrite("data",1,4,f); fwrite(&dl,4,1,f); }
int main(int argc, char **argv) {
  if (argc < 6) { fprintf(stderr,"usage: bios.zip game.neo frames every outdir\n"); return 1; }
  size_t bn, gn; void *bios = rd(argv[1], &bn); void *game = rd(argv[2], &gn);
  int frames = atoi(argv[3]), every = atoi(argv[4]); const char *out = argv[5];
  char p[512]; snprintf(p, sizeof p, "%s/audio.wav", out); wav = fopen(p, "wb"); wr_hdr(wav, 0);
  geo_log_set_callback(log_cb);
  geo_set_region(REGION_EU); geo_set_system(SYSTEM_MVS); geo_mixer_set_rate(48000); geo_mixer_init(); if (getenv("RAW")) geo_mixer_set_raw(1);
  geo_lspc_set_buffer(vbuf); geo_mixer_set_buffer(abuf); geo_mixer_set_callback(audio_cb);
  geo_init(); geo_set_region(REGION_EU);
  if (!geo_bios_load_mem(bios, bn)) { fprintf(stderr, "BIOS KO\n"); return 2; }
  if (!geo_neo_load(game, gn)) { fprintf(stderr, "NEO KO\n"); return 3; }
  geo_input_set_callback(0, js); geo_input_set_callback(1, js);
  geo_input_sys_set_callback(0, sa); geo_input_sys_set_callback(1, sb); geo_input_sys_set_callback(2, st);
  geo_input_sys_set_callback(3, dip); geo_input_sys_set_callback(4, un);
  if(getenv("TEST_AT"))test_at=atoi(getenv("TEST_AT")); if(getenv("TEST_LEN"))test_len=atoi(getenv("TEST_LEN")); if(getenv("DIP1"))dip1_until=atoi(getenv("DIP1")); keys=getenv("KEYS");
  geo_lspc_set_palette(0); geo_reset(1);
  if (getenv("FILL")) for (int i = 0; i < 320*264; i++) vbuf[i] = 0x00FF00FF;
  if (getenv("SECOND")) {
    /* play a while, then reload exactly like the Pi frontend (UnloadGame + LoadGame) */
    int n = atoi(getenv("SECOND"));
    for (int i = 0; i < n; i++) geo_exec();
    geo_deinit(); geo_bios_unload();
    geo_mixer_deinit(); geo_set_system(SYSTEM_MVS); geo_set_region(REGION_EU);
    geo_mixer_set_rate(48000); geo_mixer_init(); geo_init(); geo_set_region(REGION_EU);
    if (!geo_bios_load_mem(bios, bn)) return 2;
    if (!geo_neo_load(game, gn)) return 3;
    if (getenv("CLEAR")) {   /* same clearing as CKernel::ClearVideoMemory */
      for (unsigned bk = 0; bk < 2; bk++) { geo_lspc_palram_bank(bk); for (uint32_t a = 0; a < 0x2000; a += 2) geo_lspc_palram_wr16(a, 0); }
      geo_lspc_palram_bank(0); geo_lspc_vrammod_wr(1);
      geo_lspc_vramaddr_wr(0); for (int i = 0; i < 0x8000; i++) geo_lspc_vram_wr(0);
      geo_lspc_vramaddr_wr(0x8000); for (int i = 0; i < 0x800; i++) geo_lspc_vram_wr(0);
      geo_lspc_vramaddr_wr(0); geo_lspc_vrammod_wr(0);
    }
    geo_lspc_set_palette(0); geo_reset(1);
  }
  struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
  size_t minS = 1<<30, maxS = 0;
  static double ft[100000]; int nft=0;
  for (int i = 1; i <= frames; i++) {
    struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a);
    g_frame=i; geo_exec();
    clock_gettime(CLOCK_MONOTONIC,&b); if (i>600) ft[nft++]=(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;
    if (lastsamps < minS) minS = lastsamps; if (lastsamps > maxS) maxS = lastsamps;
    if (i % every == 0) {
      snprintf(p, sizeof p, "%s/f%05d.ppm", out, i); FILE *f = fopen(p, "wb");
      fprintf(f, "P6 304 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 304; x++) { uint32_t c = vbuf[(y+24)*320 + x + 8]; unsigned char rgb[3] = { c>>16, c>>8, c }; fwrite(rgb,1,3,f); }
      fclose(f);
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  wr_hdr(wav, wavsamples); fclose(wav);
  double s = (t1.tv_sec-t0.tv_sec) + (t1.tv_nsec-t0.tv_nsec)/1e9;
  printf("%d frame in %.2f s (%.3f ms/frame, host x86) audio %zu..%zu int16/frame\n", frames, s, s*1000/frames, minS, maxS);
  qsort(ft,nft,sizeof(double),cmp); double sum=0; for(int i=0;i<nft;i++)sum+=ft[i];
  printf("after boot: avg %.3f p99 %.3f max %.3f ms\n", sum/nft, ft[nft*99/100], ft[nft-1]);
  return 0;
}
