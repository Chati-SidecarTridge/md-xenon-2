/* The FatFs calls of shim/ff.h over the host's files, for tests that run
 * firmware units against a folder (test_x2set.py). Paths are used as
 * given. */
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* The shim's DIR is FatFs's; here POSIX's DIR (dirent.h) is needed too. */
#define DIR FF_DIR
#include "ff.h"
#undef DIR

FRESULT f_open(FIL *fp, const char *path, BYTE mode) {
  const char *m = (mode & FA_CREATE_ALWAYS) ? "w+b" : (mode & FA_WRITE) ? "r+b" : "rb";
  FILE *f = fopen(path, m);
  memset(fp, 0, sizeof(*fp));
  if (!f) return FR_NO_FILE;
  fp->host = f;
  return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
  *br = (UINT)fread(buff, 1, btr, (FILE *)fp->host);
  return ferror((FILE *)fp->host) ? FR_DISK_ERR : FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) {
  *bw = (UINT)fwrite(buff, 1, btw, (FILE *)fp->host);
  return *bw == btw ? FR_OK : FR_DISK_ERR;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
  return fseek((FILE *)fp->host, (long)ofs, SEEK_SET) == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_close(FIL *fp) {
  if (fp->host) fclose((FILE *)fp->host);
  fp->host = NULL;
  return FR_OK;
}

FRESULT f_stat(const TCHAR *path, FILINFO *fno) {
  struct stat st;
  if (stat(path, &st) != 0) return FR_NO_FILE;
  fno->fsize = (FSIZE_t)st.st_size;
  fno->fattrib = S_ISDIR(st.st_mode) ? AM_DIR : 0;
  return FR_OK;
}

FRESULT f_mkdir(const TCHAR *path) {
  if (mkdir(path, 0777) == 0) return FR_OK;
  return errno == EEXIST ? FR_EXIST : FR_NO_PATH;
}

FRESULT f_opendir(FF_DIR *dp, const TCHAR *path) {
  dp->host = opendir(path);
  return dp->host ? FR_OK : FR_NO_PATH;
}

FRESULT f_readdir(FF_DIR *dp, FILINFO *fno) {
  struct dirent *e;
  do {
    e = readdir((DIR *)dp->host);
  } while (e && (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0));
  if (!e) {
    fno->fname[0] = 0;
    return FR_OK;
  }
  snprintf(fno->fname, sizeof(fno->fname), "%s", e->d_name);
  fno->fattrib = e->d_type == DT_DIR ? AM_DIR : 0;
  fno->fsize = 0;
  return FR_OK;
}

FRESULT f_closedir(FF_DIR *dp) {
  if (dp->host) closedir((DIR *)dp->host);
  dp->host = NULL;
  return FR_OK;
}

TCHAR *f_gets(TCHAR *buff, int len, FIL *fp) {
  return fgets(buff, len, (FILE *)fp->host);
}

int f_puts(const TCHAR *str, FIL *fp) {
  return fputs(str, (FILE *)fp->host) >= 0 ? (int)strlen(str) : -1;
}

int f_printf(FIL *fp, const TCHAR *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfprintf((FILE *)fp->host, fmt, ap);
  va_end(ap);
  return n;
}
