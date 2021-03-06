
/******************************************************************************
 *
 *  This file is part of canu, a software program that assembles whole-genome
 *  sequencing reads into contigs.
 *
 *  This software is based on:
 *    'Celera Assembler' (http://wgs-assembler.sourceforge.net)
 *    the 'kmer package' (http://kmer.sourceforge.net)
 *  both originally distributed by Applera Corporation under the GNU General
 *  Public License, version 2.
 *
 *  Canu branched from Celera Assembler at its revision 4587.
 *  Canu branched from the kmer project at its revision 1994.
 *
 *  This file is derived from:
 *
 *    src/AS_OVL/overlap_partition.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2011-JUN-12 to 2013-AUG-01
 *      are Copyright 2011-2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Sergey Koren from 2012-JUL-29 to 2013-DEC-02
 *      are Copyright 2012-2013 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz from 2014-NOV-21 to 2015-AUG-25
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz beginning on 2015-DEC-07
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_global.H"
#include "gkStore.H"
#include "AS_UTL_decodeRange.H"

//  Reads gkpStore, outputs three files:
//    ovlbat - batch names
//    ovljob - job names
//    ovlopt - overlapper options
//
//  From (very) old paper notes, overlapInCore only computes overlaps for referenceID < hashID.

uint32  batchMax = 1000;

void
outputJob(FILE   *BAT,
          FILE   *JOB,
          FILE   *OPT,
          uint32  hashBeg, uint32  hashEnd, uint32  numHashReads, uint64  numHashBases,
          uint32  refBeg,  uint32  refEnd,  uint32  numRefReads,  uint64  numRefBases,
          uint32 &batchSize,
          uint32 &batchName,
          uint32 &jobName) {

  fprintf(BAT, "%03" F_U32P "\n", batchName);
  fprintf(JOB, "%06" F_U32P "\n", jobName);

  if (numHashReads == 0)
    fprintf(OPT, "-h " F_U32 "-" F_U32 " -r " F_U32 "-" F_U32 "\n", hashBeg, hashEnd, refBeg, refEnd);
  else
    fprintf(OPT, "-h " F_U32 "-" F_U32 " -r " F_U32 "-" F_U32 " --hashstrings " F_U32 " --hashdatalen " F_U64 "\n", hashBeg, hashEnd, refBeg, refEnd, numHashReads, numHashBases);

  fprintf(stderr, "%5" F_U32P " %10" F_U32P "-%-10" F_U32P " %9" F_U32P " %12" F_U64P "  %10" F_U32P "-%-10" F_U32P " %9" F_U32P " %12" F_U64P "\n", jobName, hashBeg, hashEnd, numHashReads, numHashBases, refBeg, refEnd, numRefReads, numRefBases);

  refBeg = refEnd + 1;

  batchSize++;

  if (batchSize >= batchMax) {
    batchSize = 0;
    batchName++;
  }

  jobName++;
}



uint32 *
loadReadLengths(gkStore *gkp,
                set<uint32> &libToHash, uint32 &hashMin, uint32 &hashMax,
                set<uint32> &libToRef,  uint32 &refMin,  uint32 &refMax) {
  uint32     numReads = gkp->gkStore_getNumReads();
  uint32     numLibs  = gkp->gkStore_getNumLibraries();
  uint32    *readLen  = new uint32 [numReads + 1];

  bool testHash = false;
  bool testRef  = false;

  if (libToHash.size() > 0) {
    testHash  = true;
    hashMin   = UINT32_MAX;
    hashMax   = 0;
  }

  if (libToRef.size() > 0) {
    testRef  = true;
    refMin   = UINT32_MAX;
    refMax   = 0;
  }

  bool  *doHash = new bool [numLibs + 1];
  bool  *doRef  = new bool [numLibs + 1];

  for (uint32 i=0; i<=numLibs; i++) {
    doHash[i] = (libToHash.count(i) == 0) ? false : true;
    doRef[i]  = (libToRef.count(i)  == 0) ? false : true;
  }

  //fprintf(stderr, "Loading lengths of " F_U32 " fragments (" F_SIZE_T "mb)\n",
  //        numReads, (numReads * sizeof(uint32)) >> 20);

  memset(readLen, 0, sizeof(uint32) * (numReads + 1));

  uint64  rawReads = 0, rawBases = 0;
  uint64  corReads = 0, corBases = 0;
  uint64  triReads = 0, triBases = 0;

  fprintf(stderr, "\n");
  fprintf(stderr, "       Raw          Raw  Corrected    Corrected    Trimmed      Trimmed\n");
  fprintf(stderr, "     Reads        Bases      Reads        Bases      Reads        Bases\n");
  fprintf(stderr, "---------- ------------ ---------- ------------ ---------- ------------\n");

  uint32  reportInterval = max(numReads / 40, (uint32)1);

  for (uint32 ii=1; ii<=numReads; ii++) {
    gkRead  *read = gkp->gkStore_getRead(ii);

    if (read->gkRead_readID() != ii)
      fprintf(stderr, "ERROR: readID=%u != ii=%u\n",
              read->gkRead_readID(), ii);
    assert(read->gkRead_readID() == ii);

    if (read->gkRead_rawLength() > 0) {
      rawReads += 1;
      rawBases += read->gkRead_rawLength();
    }

    if (read->gkRead_correctedLength() > 0) {
      corReads += 1;
      corBases += read->gkRead_correctedLength();
    }

    if (read->gkRead_trimmedLength() > 0) {
      triReads += 1;
      triBases += read->gkRead_trimmedLength();
    }

    readLen[ii] = read->gkRead_sequenceLength();

    if ((testHash == true) && (doHash[read->gkRead_libraryID()] == true)) {
      hashMin = min(hashMin, ii);
      hashMax = max(hashMax, ii);
    }

    if ((testRef == true) && (doRef[read->gkRead_libraryID()] == true)) {
      refMin = min(refMin, ii);
      refMax = max(refMax, ii);
    }

    if ((ii % reportInterval) == 0)
      fprintf(stderr, "%10" F_U64P " %12" F_U64P " %10" F_U64P " %12" F_U64P " %10" F_U64P " %12" F_U64P "\n",
              rawReads, rawBases, corReads, corBases, triReads, triBases);
  }

  fprintf(stderr, "---------- ------------ ---------- ------------ ---------- ------------\n");
  fprintf(stderr, "%10" F_U64P " %12" F_U64P " %10" F_U64P " %12" F_U64P " %10" F_U64P " %12" F_U64P "\n",
          rawReads, rawBases, corReads, corBases, triReads, triBases);
  fprintf(stderr, "\n");

  delete [] doHash;
  delete [] doRef;

  return(readLen);
}



void
partitionLength(gkStore      *gkp,
                uint32       *readLen,
                FILE         *BAT,
                FILE         *JOB,
                FILE         *OPT,
                uint32        minOverlapLength,
                uint64        ovlHashBlockLength,
                uint64        ovlRefBlockLength,
                set<uint32>  &libToHash,
                uint32        hashMin,
                uint32        hashMax,
                set<uint32>  &libToRef,
                uint32        refMin,
                uint32        refMax) {
  uint32  hashBeg   = 1;
  uint32  hashEnd   = 0;
  uint32  hashReads = 0;
  uint64  hashBases = 0;

  uint32  refBeg    = 1;
  uint32  refEnd    = 0;
  uint32  refReads  = 0;
  uint64  refBases  = 0;

  uint32  batchSize = 0;    //  Number of jobs in this directory
  uint32  batchName = 1;    //  Name of the directory
  uint32  jobName   = 1;    //  Name of the job

  uint32  numReads = gkp->gkStore_getNumReads();

  if (hashMax > numReads)
    hashMax = numReads;
  if (refMax > numReads)
    refMax = numReads;

  //fprintf(stderr, "Partitioning for hash: " F_U32 "-" F_U32 " ref: " F_U32 "," F_U32 "\n",
  //        hashMin, hashMax, refMin, refMax);

  hashBeg = hashMin;
  hashEnd = hashMin - 1;

  while (hashBeg < hashMax) {
    uint64  hashLen = 0;

    assert(hashEnd == hashBeg - 1);

    //  Non deleted reads contribute one byte per untrimmed base, and every fragment contributes one
    //  more byte for the terminating zero.  In canu, there are no deleted reads.

    hashReads = 0;
    hashBases = 0;

    do {
      hashEnd++;

      if (readLen[hashEnd] < minOverlapLength)
        continue;

      hashLen += readLen[hashEnd] + 1;

      hashReads += 1;
      hashBases += readLen[hashEnd] + 1;
    } while ((hashLen < ovlHashBlockLength) && (hashEnd < hashMax));

    assert(hashEnd <= hashMax);

    refBeg = refMin;
    refEnd = 0;

    while ((refBeg < refMax) &&
           ((refBeg < hashEnd) || (libToHash.size() != 0 && libToHash == libToRef))) {
      uint64  refLen = 0;

      refReads  = 0;
      refBases  = 0;

      do {
        refEnd++;

        if (readLen[refEnd] < minOverlapLength)
          continue;

        refLen += readLen[refEnd];

        refReads += 1;
        refBases += readLen[refEnd] + 1;
      } while ((refLen < ovlRefBlockLength) && (refEnd < refMax));

      if (refEnd > refMax)
        refEnd = refMax;
      if ((refEnd > hashEnd) && (libToHash.size() == 0 || libToHash != libToRef))
        refEnd = hashEnd;

      outputJob(BAT,
                JOB,
                OPT,
                hashBeg, hashEnd, hashReads, hashBases,
                refBeg,  refEnd,  refReads,  refBases,
                //hashEnd - hashBeg + 1, hashLen,
                batchSize, batchName, jobName);

      refBeg = refEnd + 1;
    }

    hashBeg = hashEnd + 1;
  }

}



FILE *
openOutput(char *prefix, char *type) {
  char  A[FILENAME_MAX];

  snprintf(A, FILENAME_MAX, "%s.%s.WORKING", prefix, type);

  errno = 0;

  FILE *F = fopen(A, "w");

  if (errno)
    fprintf(stderr, "Failed to open '%s': %s\n", A, strerror(errno)), exit(1);

  return(F);
}



void
renameToFinal(char *prefix, char *type) {
  char  A[FILENAME_MAX];
  char  B[FILENAME_MAX];

  snprintf(A, FILENAME_MAX, "%s.%s.WORKING", prefix, type);
  snprintf(B, FILENAME_MAX, "%s.%s",         prefix, type);

  AS_UTL_rename(A, B);
}



int
main(int argc, char **argv) {
  char            *gkpStoreName        = NULL;
  gkStore         *gkpStore            = NULL;

  char            *outputPrefix        = NULL;
  char             outputName[FILENAME_MAX];

  uint64           ovlHashBlockLength  = 0;
  uint64           ovlRefBlockLength   = 0;

  uint32           minOverlapLength    = 0;

  bool             checkAllLibUsed     = true;

  set<uint32>      libToHash;
  set<uint32>      libToRef;

  AS_configure(argc, argv);

  int arg = 1;
  int err = 0;
  while (arg < argc) {
    if        (strcmp(argv[arg], "-G") == 0) {
      gkpStoreName = argv[++arg];

    } else if (strcmp(argv[arg], "-hl") == 0) {
      ovlHashBlockLength = strtouint64(argv[++arg]);

    } else if (strcmp(argv[arg], "-rl") == 0) {
      ovlRefBlockLength  = strtouint64(argv[++arg]);

    } else if (strcmp(argv[arg], "-ol") == 0) {
      minOverlapLength   = strtouint32(argv[++arg]);

    } else if (strcmp(argv[arg], "-H") == 0) {
      AS_UTL_decodeRange(argv[++arg], libToHash);

    } else if (strcmp(argv[arg], "-R") == 0) {
      AS_UTL_decodeRange(argv[++arg], libToRef);

    } else if (strcmp(argv[arg], "-C") == 0) {
       checkAllLibUsed = false;

    } else if (strcmp(argv[arg], "-o") == 0) {
      outputPrefix = argv[++arg];

    } else {
      fprintf(stderr, "ERROR:  Unknown option '%s'\n", arg[argv]);
      err++;
    }

    arg++;
  }

  if (ovlHashBlockLength == 0)
    fprintf(stderr, "ERROR:  Hash length (-hl) must be specified.\n"), err++;

  if (ovlRefBlockLength == 0)
    fprintf(stderr, "ERROR:  Reference length (-rl) must be specified.\n"), err++;

  if (gkpStoreName == NULL)
    fprintf(stderr, "ERROR:  gkpStore (-G) must be supplied.\n"), err++;

  if (err) {
    fprintf(stderr, "usage: %s [opts]\n", argv[0]);
    fprintf(stderr, "  Someone should write the command line help.\n");
    fprintf(stderr, "  But this is only used interally to canu, so...\n");
    exit(1);
  }

  fprintf(stderr, "\n");
  fprintf(stderr, "Configuring for:\n");
  fprintf(stderr, "  hash table:   %12" F_U64P " bases.\n", ovlHashBlockLength);
  fprintf(stderr, "  read stream:  %12" F_U64P " bases.\n",  ovlRefBlockLength);
  fprintf(stderr, "\n");

  gkStore   *gkp         = gkStore::gkStore_open(gkpStoreName);
  uint32     numLibs     = gkp->gkStore_getNumLibraries();
  uint32     invalidLibs = 0;

  for (set<uint32>::iterator it=libToHash.begin(); it != libToHash.end(); it++)
    if (numLibs < *it)
      fprintf(stderr, "ERROR: -H " F_U32 " is invalid; only " F_U32 " libraries in '%s'\n",
              *it, numLibs, gkpStoreName), invalidLibs++;

  for (set<uint32>::iterator it=libToRef.begin(); it != libToRef.end(); it++)
    if (numLibs < *it)
      fprintf(stderr, "ERROR: -R " F_U32 " is invalid; only " F_U32 " libraries in '%s'\n",
              *it, numLibs, gkpStoreName), invalidLibs++;

  if ((libToHash.size() > 0) && (libToRef.size() > 0)) {
    for (uint32 lib=1; lib<=numLibs; lib++) {
      if ((libToHash.find(lib) == libToHash.end()) &&
          (libToRef.find(lib)  == libToRef.end())) {
        if (checkAllLibUsed == true)
          fprintf(stderr, "ERROR: library " F_U32 " is not mentioned in either -H or -R.\n", lib), invalidLibs++;
        else
          fprintf(stderr, "Warning: library " F_U32 " is not mentioned in either -H or -R.\n", lib);
       }
    }
  }
  if (invalidLibs > 0)
    fprintf(stderr, "ERROR: one of -H and/or -R are invalid.\n"), exit(1);


  assert(ovlHashBlockLength > 0);


  uint32  hashMin = 1;
  uint32  hashMax = UINT32_MAX;

  uint32  refMin  = 1;
  uint32  refMax  = UINT32_MAX;

  uint32 *readLen = loadReadLengths(gkp, libToHash, hashMin, hashMax, libToRef, refMin, refMax);

  FILE *BAT = openOutput(outputPrefix, "ovlbat");
  FILE *JOB = openOutput(outputPrefix, "ovljob");
  FILE *OPT = openOutput(outputPrefix, "ovlopt");

  fprintf(stderr, "  Job       Hash Range        # Reads      # Bases      Stream Range        # Reads      # Bases\n");
  fprintf(stderr, "----- --------------------- --------- ------------  --------------------- --------- ------------\n");

  partitionLength(gkp, readLen, BAT, JOB, OPT, minOverlapLength, ovlHashBlockLength, ovlRefBlockLength, libToHash, hashMin, hashMax, libToRef, refMin, refMax);

  AS_UTL_closeFile(BAT);
  AS_UTL_closeFile(JOB);
  AS_UTL_closeFile(OPT);

  delete [] readLen;

  renameToFinal(outputPrefix, "ovlbat");
  renameToFinal(outputPrefix, "ovljob");
  renameToFinal(outputPrefix, "ovlopt");

  gkp->gkStore_close();

  exit(0);
}
