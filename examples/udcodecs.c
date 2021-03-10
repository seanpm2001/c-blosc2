/*
Copyright (C) 2021  The Blosc developers
 http://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  Example program demonstrating use of the Blosc filter from C code.

  To compile this program:

  $ gcc udcodecs.c -o udcodecs -lblosc2

  To run:

  $ ./udcodecs
*/

#include <stdio.h>
#include <blosc2.h>

#define KB  1024.
#define MB  (1024*KB)
#define GB  (1024*MB)

#define CHUNKSIZE (1000 * 1000)
#define NCHUNKS 100
#define NTHREADS 4

typedef struct {
  uint8_t itemsize;
} codec_params;


int codec_encoder(const uint8_t* input, int32_t input_len,
                  uint8_t* output, int32_t output_len,
                  void* params) {
  codec_params *fparams = (codec_params *) params;
  if (fparams->itemsize != 4) {
    BLOSC_TRACE_ERROR("Itemsize %d != 4", fparams->itemsize);
    return BLOSC2_ERROR_FAILURE;
  }

  int32_t nelem = input_len / fparams->itemsize;
  int32_t *in_ = ((int32_t *) input);
  int32_t *out_ = ((int32_t *) output);

  // Check that is an arange
  int32_t start = in_[0];
  int32_t step = in_[1] - start;
  for (int i = 1; i < nelem - 1; ++i) {
    if (in_[i + 1] - in_[i] != step) {
      BLOSC_TRACE_ERROR("Buffer is not an arange");
      return BLOSC2_ERROR_FAILURE;
    }
  }

  if (8 > output_len) {
    return BLOSC2_ERROR_WRITE_BUFFER;
  }
  out_[0] = start;
  out_[1] = step;

  return 8;
}

int codec_decoder(const uint8_t* input, int32_t input_len,
                  uint8_t* output, int32_t output_len,
                  void* params) {

  codec_params *fparams = (codec_params *) params;
  if (fparams->itemsize != 4) {
    BLOSC_TRACE_ERROR("Itemsize %d != 4", fparams->itemsize);
    return BLOSC2_ERROR_FAILURE;
  }

  int32_t nelem = output_len / fparams->itemsize;
  int32_t *in_ = ((int32_t *) input);
  int32_t *out_ = ((int32_t *) output);

  if (8 > input_len) {
    return BLOSC2_ERROR_WRITE_BUFFER;
  }
  int32_t start = in_[0];
  int32_t step = in_[1];
  for (int i = 0; i < nelem; ++i) {
    out_[i] = start + i * step;
  }

  return output_len;
}

int main(void) {
  static int32_t data[CHUNKSIZE];
  static int32_t data_dest[CHUNKSIZE];
  int32_t isize = CHUNKSIZE * sizeof(int32_t);
  int dsize;
  int64_t nbytes, cbytes;

  codec_params params = {.itemsize=sizeof(int32_t)};
  blosc2_udcodec udcodec;
  udcodec.id = 128;
  udcodec.encoder = codec_encoder;
  udcodec.decoder = codec_decoder;
  udcodec.params = &params;

  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  cparams.udcodecs[0] = udcodec;
  cparams.compcode = BLOSC_UDCODEC;
  cparams.compcode_meta = 128;
  for (int i = 0; i < BLOSC2_MAX_FILTERS; ++i) {
    cparams.filters[i] = 0;
  }
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
  dparams.udcodecs[0] = udcodec;

  blosc2_schunk* schunk;
  int i, nchunk;
  blosc_timestamp_t last, current;
  double ttotal;

  printf("Blosc version info: %s (%s)\n", blosc_get_version_string(), BLOSC_VERSION_DATE);

  /* Create a super-chunk container */
  cparams.typesize = sizeof(int32_t);
  cparams.clevel = 9;
  blosc2_storage storage = {.cparams=&cparams, .dparams=&dparams};
  schunk = blosc2_schunk_new(&storage);

  blosc_set_timestamp(&last);
  for (nchunk = 0; nchunk < NCHUNKS; nchunk++) {
    for (i = 0; i < CHUNKSIZE; i++) {
      data[i] = i * nchunk;
    }
    int nchunks = blosc2_schunk_append_buffer(schunk, data, isize);
    if (nchunks != nchunk + 1) {
      printf("Unexpected nchunks!");
      return -1;
    }
  }
  /* Gather some info */
  nbytes = schunk->nbytes;
  cbytes = schunk->cbytes;
  blosc_set_timestamp(&current);
  ttotal = blosc_elapsed_secs(last, current);
  printf("Compression ratio: %.1f MB -> %.1f MB (%.1fx)\n",
         nbytes / MB, cbytes / MB, (1. * nbytes) / cbytes);
  printf("Compression time: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));

  /* Retrieve and decompress the chunks (0-based count) */
  blosc_set_timestamp(&last);
  for (nchunk = NCHUNKS-1; nchunk >= 0; nchunk--) {
    dsize = blosc2_schunk_decompress_chunk(schunk, nchunk, data_dest, isize);
    if (dsize < 0) {
      printf("Decompression error.  Error code: %d\n", dsize);
      return dsize;
    }
  }
  blosc_set_timestamp(&current);
  ttotal = blosc_elapsed_secs(last, current);
  printf("Decompression time: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));

  /* Check integrity of the second chunk (made of non-zeros) */
  blosc2_schunk_decompress_chunk(schunk, 1, data_dest, isize);
  for (i = 0; i < CHUNKSIZE; i++) {
    if (data_dest[i] != i) {
      printf("Decompressed data differs from original %d, %d!\n",
             i, data_dest[i]);
      return -1;
    }
  }

  printf("Successful roundtrip data <-> schunk !\n");

  /* Free resources */
  /* Destroy the super-chunk */
  blosc2_schunk_free(schunk);

  return 0;
}
