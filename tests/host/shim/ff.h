/* Host stand-in for FatFs's ff.h: the types and calls the firmware units
 * under test use. test_audio*.c implement the reading calls over files in
 * memory; ff_stdio.c implements all of them over the host's files (the
 * `host` fields). */
#ifndef HOST_SHIM_FF_H
#define HOST_SHIM_FF_H

#include <stdint.h>

typedef unsigned int UINT;
typedef uint32_t FSIZE_t;
typedef unsigned char BYTE;
typedef char TCHAR;
typedef enum {
  FR_OK = 0,
  FR_DISK_ERR,
  FR_NO_FILE = 4,
  FR_NO_PATH = 5,
  FR_DENIED = 7,
  FR_EXIST = 8
} FRESULT;
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08
#define AM_DIR 0x10

typedef struct {
  const BYTE *data;
  FSIZE_t size;
  FSIZE_t pos;
  void *host; /* ff_stdio.c: the host's FILE */
} FIL;

typedef struct {
  void *host; /* ff_stdio.c: the host's DIR */
} DIR;

typedef struct {
  FSIZE_t fsize;
  BYTE fattrib;
  TCHAR fname[256];
} FILINFO;

FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_close(FIL *fp);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_stat(const TCHAR *path, FILINFO *fno);
FRESULT f_mkdir(const TCHAR *path);
FRESULT f_opendir(DIR *dp, const TCHAR *path);
FRESULT f_readdir(DIR *dp, FILINFO *fno);
FRESULT f_closedir(DIR *dp);
TCHAR *f_gets(TCHAR *buff, int len, FIL *fp);
int f_puts(const TCHAR *str, FIL *fp);
int f_printf(FIL *fp, const TCHAR *fmt, ...);

#endif
