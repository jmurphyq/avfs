#ifndef BZCONF_H
#define BZCONF_H

#define BZ_PREFIX 1

#ifdef BZ_PREFIX
#  define BZ2_blockSort ABZ_BZ2_blockSort
#  define BZ2_hbAssignCodes ABZ_BZ2_hbAssignCodes
#  define BZ2_hbCreateDecodeTables ABZ_BZ2_hbCreateDecodeTables
#  define BZ2_hbMakeCodeLengths ABZ_BZ2_hbMakeCodeLengths
#  define BZ2_bsInitWrite ABZ_BZ2_bsInitWrite
#  define BZ2_compressBlock ABZ_BZ2_compressBlock
#  define BZ2_decompress ABZ_BZ2_decompress
#  define BZ2_bzBuffToBuffCompress ABZ_BZ2_bzBuffToBuffCompress
#  define BZ2_bzBuffToBuffDecompress ABZ_BZ2_bzBuffToBuffDecompress
#  define BZ2_bzCompress ABZ_BZ2_bzCompress
#  define BZ2_bzCompressEnd ABZ_BZ2_bzCompressEnd
#  define BZ2_bzCompressInit ABZ_BZ2_bzCompressInit
#  define BZ2_bzDecompress ABZ_BZ2_bzDecompress
#  define BZ2_bzDecompressEnd ABZ_BZ2_bzDecompressEnd
#  define BZ2_bzDecompressInit ABZ_BZ2_bzDecompressInit
#  define BZ2_bzRestoreBlockEnd ABZ_BZ2_bzRestoreBlockEnd
#  define BZ2_bzSetBlockEndHandler ABZ_BZ2_bzSetBlockEndHandler
#  define BZ2_bzlibVersion ABZ_BZ2_bzlibVersion
#  define BZ2_indexIntoF ABZ_BZ2_indexIntoF
#  define BZ2_crc32Table ABZ_BZ2_crc32Table
#  define BZ2_rNums ABZ_BZ2_rNums
#endif

#endif
