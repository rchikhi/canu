
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
 *  Modifications by:
 *
 *    Brian P. Walenz beginning on 2017-OCT-03
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_global.H"

#include "gkStore.H"
#include "tgStore.H"



int
main (int argc, char **argv) {
  char            *gkpName        = NULL;
  char            *corName        = NULL;
  int32            corVers        = 1;

  vector<char *>   corInputs;
  char            *corInputsFile  = NULL;

  bool             updateCorStore = false;
  bool             loadQVs        = false;

  argc = AS_configure(argc, argv);

  vector<char *>  err;
  int             arg = 1;
  while (arg < argc) {
    if        (strcmp(argv[arg], "-G") == 0) {
      gkpName = argv[++arg];

    } else if (strcmp(argv[arg], "-C") == 0) {
      corName = argv[++arg];
      //corVers = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-L") == 0) {
      corInputsFile = argv[++arg];
      AS_UTL_loadFileList(corInputsFile, corInputs);

    } else if (strcmp(argv[arg], "-u") == 0) {
      updateCorStore = true;

    } else if (strcmp(argv[arg], "-qv") == 0) {
      loadQVs = true;

    } else if (AS_UTL_fileExists(argv[arg])) {
      corInputs.push_back(argv[arg]);

    } else {
      char *s = new char [1024];
      snprintf(s, 1024, "ERROR:  Unknown option '%s'.\n", argv[arg]);
      err.push_back(s);
    }

    arg++;
  }

  if (gkpName == NULL)
    err.push_back("ERROR:  no gatekeeper store (-G) supplied.\n");
  if (corName == NULL)
    err.push_back("ERROR:  no tig store (-T) supplied.\n");
  if ((corInputs.size() == 0) && (corInputsFile == NULL))
    err.push_back("ERROR:  no input tigs supplied on command line and no -L file supplied.\n");

  if (err.size() > 0) {
    fprintf(stderr, "usage: %s -G <gkpStore> -C <corStore> [input.cns]\n", argv[0]);
    fprintf(stderr, "  Load the output of falconsense into the corStore and gkpStore.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -G <gkpStore>         Path to the gatekeeper store\n");
    fprintf(stderr, "  -C <corStore>         Path to the corStore\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -L <file-of-files>    Load the tig(s) from files listed in 'file-of-files'\n");
    fprintf(stderr, "                        (WARNING: program will succeed if this file is empty)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -u                    Also load the populated tig layout into version 2 of the corStore.\n");
    fprintf(stderr, "                        (WARNING: not rigorously tested)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -qv                   Also load the QVs into the gatekeeper store.\n");
    fprintf(stderr, "\n");

    for (uint32 ii=0; ii<err.size(); ii++)
      if (err[ii])
        fputs(err[ii], stderr);

    exit(1);
  }

  gkStore     *gkpStore = gkStore::gkStore_open(gkpName, gkStore_extend);
  gkReadData  *readData = new gkReadData;
  tgStore     *corStore = new tgStore(corName, corVers, tgStoreModify);
  tgTig       *tig      = new tgTig;

  uint64       nSkip    = 0;
  uint64       nSkipTot = 0;

  uint64       nLoad    = 0;
  uint64       nLoadTot = 0;

  fprintf(stdout, "     read       raw corrected\n");
  fprintf(stdout, "       id    length    length\n");
  fprintf(stdout, "--------- --------- ---------\n");

  fprintf(stderr, "\n");
  fprintf(stderr, "   loaded   skipped                          input file\n");
  fprintf(stderr, "--------- --------- -----------------------------------\n");

  for (uint32 ff=0; ff<corInputs.size(); ff++) {
    nSkip = 0;
    nLoad = 0;

    FILE *TI = AS_UTL_openInputFile(corInputs[ff]);

    while (tig->loadFromStreamOrLayout(TI) == true) {
      uint32  rID  = tig->tigID();
      gkRead *read = gkpStore->gkStore_getRead(rID);

      if (tig->consensusExists() == false) {
        nSkip++;
        continue;
      }

      nLoad++;

      //  Load the data into corStore.

      if (updateCorStore == true)
        corStore->insertTig(tig, false);

      //  Load the data into gkpStore.

      if (loadQVs == false)
        tig->quals()[0] = 255;

      gkpStore->gkStore_loadReadData(tig->tigID(), readData);            //  Load old data into the read.
      readData->gkReadData_setBasesQuals(tig->bases(), tig->quals());    //  Insert new data.
      gkpStore->gkStore_stashReadData(readData);                         //  Write combined data.

      //  Log it.

      fprintf(stdout, "%9u %9u %9u\n", rID, read->gkRead_rawLength(), read->gkRead_correctedLength());

      assert(read->gkRead_correctedLength() == tig->length());
    }

    AS_UTL_closeFile(TI, corInputs[ff]);

    fprintf(stderr, "%9" F_U64P " %9" F_U64P " %35s\n", nLoad, nSkip, corInputs[ff]);

    nSkipTot += nSkip;
    nLoadTot += nLoad;
  }

  delete tig;
  delete corStore;

  delete readData;

  gkpStore->gkStore_close();

  fprintf(stderr, "--------- --------- -----------------------------------\n");
  fprintf(stderr, "%9" F_U64P " %9" F_U64P " %35" F_U64P "\n", nLoadTot, nSkipTot, corInputs.size());
  fprintf(stderr, "\n");
  fprintf(stderr, "Bye.\n");

  exit(0);
}
