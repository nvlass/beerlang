/* include/aot.h — AOT compilation (.beerc) interface */

#ifndef BEERLANG_AOT_H
#define BEERLANG_AOT_H

/* Register beer.beerc natives: file-mtime, crc32, file-exists?,
 * list-dir, read-header, compile-file!                              */
void core_register_aot(void);

#endif /* BEERLANG_AOT_H */
