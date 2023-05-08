#include <getopt.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <vector>

#include <runtime.h>
#include <thread.h>
#include <sync.h>
#include "nu/runtime.hpp"

#include "ivf.hh"
#include "uncompressed_chunk.hh"
#include "frame_input.hh"
#include "ivf_reader.hh"
#include "yuv4mpeg.hh"
#include "frame.hh"
#include "player.hh"
#include "vp8_raster.hh"
#include "decoder.hh"
#include "encoder.hh"
#include "macroblock.hh"
#include "ivf_writer.hh"
#include "display.hh"
#include "enc_state_serializer.hh"

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

size_t N = 16;

// serialized decoder state
using DecoderBuffer = vector<uint8_t>;

typedef struct {
  vector<IVF_MEM> vpx_ivf, xc0_ivf, xc1_ivf, rebased_ivf, final_ivf;
  vector<DecoderBuffer> dec_state, enc0_state, enc1_state, rebased_state;
  vector<vector<RasterHandle>> rasters;
  uint16_t display_width, display_height;
} xc_t;

vector<uint32_t> stage1_time, stage2_time, stage3_time;

void usage(const string &program_name) {
  cerr << "Usage: " << program_name << " [nu_args] [input_dir]" << endl;
}

bool decode(vector<DecoderBuffer> & output, vector<IVF_MEM> & ivfs, size_t index) {
  IVF_MEM ivf = ivfs[index];

  Decoder decoder = Decoder(ivf.width(), ivf.height());
  if ( not decoder.minihash_match( ivf.expected_decoder_minihash() ) ) {
    throw Invalid( "Decoder state / IVF mismatch" );
  }

  size_t frame_number = ivf.frame_count() - 1;
  for ( size_t i = 0; i < ivf.frame_count(); i++ ) {
    UncompressedChunk uch { ivf.frame( i ), ivf.width(), ivf.height(), false };

    if ( uch.key_frame() ) {
      KeyFrame frame = decoder.parse_frame<KeyFrame>( uch );
      decoder.decode_frame( frame );
    }
    else {
      InterFrame frame = decoder.parse_frame<InterFrame>( uch );
      decoder.decode_frame( frame );
    }

    if ( i == frame_number ) {
      EncoderStateSerializer odata;
      decoder.serialize(odata);
      output[index] = odata.get();
      return true;
    }
  }

  throw runtime_error( "invalid frame number" );
}

void enc_given_state(vector<IVF_MEM> & output_ivf,
                     vector<DecoderBuffer> & input_state, 
                     vector<DecoderBuffer> & output_state,
                     vector<IVF_MEM> & pred,
                     vector<DecoderBuffer> * prev_state,
                     vector<RasterHandle> & rasters,
                     uint16_t display_width, uint16_t display_height,
                     size_t index) {
  bool two_pass = false;
  double kf_q_weight = 1.0;
  bool extra_frame_chunk = false;
  EncoderQuality quality = BEST_QUALITY;

  Decoder pred_decoder( display_width, display_height );
  if (prev_state) {
    pred_decoder = EncoderStateDeserializer_MEM::build<Decoder>( prev_state->at(index - 1) );
  }

  IVFWriter_MEM ivf_writer { "VP80", display_width, display_height, 1, 1 };
  vector<RasterHandle> original_rasters = rasters;

  /* pre-read all the prediction frames */
  vector<pair<Optional<KeyFrame>, Optional<InterFrame> > > prediction_frames;

  IVF_MEM pred_ivf = pred[index];

  if ( not pred_decoder.minihash_match( pred_ivf.expected_decoder_minihash() ) ) {
    throw Invalid( "Mismatch between prediction IVF and prediction_ivf_initial_state" );
  }

  for ( unsigned int i = 0; i < pred_ivf.frame_count(); i++ ) {
    UncompressedChunk unch { pred_ivf.frame( i ), pred_ivf.width(), pred_ivf.height(), false };

    if ( unch.key_frame() ) {
      KeyFrame frame = pred_decoder.parse_frame<KeyFrame>( unch );
      pred_decoder.decode_frame( frame );

      prediction_frames.emplace_back( move( frame ), Optional<InterFrame>() );
    } else {
      InterFrame frame = pred_decoder.parse_frame<InterFrame>( unch );
      pred_decoder.decode_frame( frame );

      prediction_frames.emplace_back( Optional<KeyFrame>(), move( frame ) );
    }
  }

  Encoder encoder( EncoderStateDeserializer_MEM::build<Decoder>(input_state[index-1]) , two_pass, quality );

  ivf_writer.set_expected_decoder_entry_hash( encoder.export_decoder().get_hash().hash() );

  encoder.reencode( original_rasters, prediction_frames, kf_q_weight,
                    extra_frame_chunk, ivf_writer );

  output_ivf[index] = ivf_writer.ivf();
  EncoderStateSerializer odata = {};
  encoder.export_decoder().serialize(odata);
  output_state[index] = odata.get();
}

void merge(vector<IVF_MEM> & input1, vector<IVF_MEM> & input2, vector<IVF_MEM> & output, size_t index) {
  IVF_MEM ivf1 = input1[index-1];
  IVF_MEM ivf2 = input2[index];

  if ( ivf1.width() != ivf2.width() or ivf1.height() != ivf2.height() ) {
    throw runtime_error( "cannot merge ivfs with different dimensions." );
  }

  IVFWriter_MEM ivf_writer("VP80", ivf1.width(), ivf1.height(), 1, 1 );

  for ( size_t i = 0; i < ivf1.frame_count(); i++ ) {
    ivf_writer.append_frame( ivf1.frame( i ) );
  }

  for ( size_t i = 0; i < ivf2.frame_count(); i++ ) {
    ivf_writer.append_frame( ivf2.frame( i ) );
  }

  output[index] = ivf_writer.ivf();
}

void decode_all(const string prefix, xc_t &s) {
  vector<rt::Thread> ths;
  for (size_t i = 0; i < N; i++) {    
    ths.emplace_back( [i, &s] {
      auto start = microtime();
      decode(s.dec_state, s.vpx_ivf, i);
      auto end = microtime();
      stage1_time[i] = end - start;
    } );
  }

  for (auto &th : ths) {
    th.Join();
  }
}

void encode_all(const string prefix, xc_t &s) {
  vector <rt::Thread> ths;
  // first pass
  for (size_t i = 0; i < N; i++) {    
    if (i == 0) {
      s.xc0_ivf[0] = s.vpx_ivf[0];
      s.enc0_state[0] = s.dec_state[0];
    } else {
      ths.emplace_back( [i, &s] {
        auto start = microtime();
        enc_given_state(s.xc0_ivf, s.dec_state, s.enc0_state, s.vpx_ivf, nullptr, s.rasters[i], s.display_width, s.display_height, i);
        auto end = microtime();
        stage2_time[i] = end - start;
      } );
    }
  }
  for (auto &th : ths) {
    th.Join();
  }

  ths.clear();

  // second pass
  for (size_t i = 0; i < N; i++) {    
    if (i == 0) {
      s.xc1_ivf[0] = s.xc0_ivf[0];
      s.enc1_state[0] = s.enc0_state[0];
    } else {
      vector<DecoderBuffer> *prev_state_ptr = &(s.dec_state);
      ths.emplace_back( [i, prev_state_ptr, &s] {
        auto start = microtime();
        enc_given_state(s.xc1_ivf, s.enc0_state, s.enc1_state, s.xc0_ivf, prev_state_ptr, s.rasters[i], s.display_width, s.display_height, i);
        auto end = microtime();
        stage2_time[i] += end - start;
      } );
    }
  }
  for (auto &th : ths) {
    th.Join();
  }
}

void rebase(const string prefix, xc_t &s) {
  // serial rebase
  for (size_t i = 0; i < N; i++) {
    if (i == 0) {
      s.final_ivf[0] = s.xc1_ivf[0];
      s.rebased_ivf[0] = s.xc1_ivf[0];
      s.rebased_state[0] = s.enc0_state[0];
    } else {
      auto start = microtime();
      enc_given_state(s.rebased_ivf, s.rebased_state, s.rebased_state, s.xc1_ivf, &(s.enc0_state), s.rasters[i], s.display_width, s.display_height, i);
      merge(s.final_ivf, s.rebased_ivf, s.final_ivf, i);
      auto end = microtime();
      stage3_time[i] = end - start;
    }
  }
}

void write_output(const std::string prefix, xc_t &s) {
  const string final_file = prefix + "final.ivf";
  s.final_ivf[N-1].write(final_file);
}

void read_input(const std::string prefix, xc_t &s) {
  for (size_t i = 0; i < N; i++) {
    ostringstream inputss, vpxss;
    inputss << prefix << std::setw(2) << std::setfill('0') << i << ".y4m";
    const string input_file = inputss.str();
    
    shared_ptr<FrameInput> input_reader = make_shared<YUV4MPEGReader>( input_file );
    if (i == 0) {
      s.display_width = input_reader->display_width();
      s.display_height = input_reader->display_height();
    }

    IVFWriter_MEM ivf_writer { "VP80", input_reader->display_width(), input_reader->display_height(), 1, 1 };
    // pre-read original rasters
    vector<RasterHandle> original_rasters;
    for ( size_t i = 0; ; i++ ) {
      auto next_raster = input_reader->get_next_frame();

      if ( next_raster.initialized() ) {
        original_rasters.emplace_back( next_raster.get() );
      } else {
        break;
      }
    }
    s.rasters.push_back(original_rasters);

    vpxss << prefix << "vpx_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string vpx_file = vpxss.str();
    IVF_MEM ivf(vpx_file);
    s.vpx_ivf[i] = ivf;
  }
}

int do_work(const string & prefix) {
  // if (argc != 2) {
  //   usage(argv[0]);
  //   return EXIT_FAILURE;
  // }

  xc_t s;

  s.vpx_ivf.resize(N);
  s.xc0_ivf.resize(N);
  s.xc1_ivf.resize(N);
  s.rebased_ivf.resize(N);
  s.final_ivf.resize(N);
  s.dec_state.resize(N);
  s.enc0_state.resize(N);
  s.enc1_state.resize(N);
  s.rebased_state.resize(N);

  stage1_time.resize(N);
  stage2_time.resize(N);
  stage3_time.resize(N);

  read_input(prefix, s);
  auto t1 = microtime();
  decode_all(prefix, s);
  auto t2 = microtime();
  encode_all(prefix, s);
  auto t3 = microtime();
  rebase(prefix, s);
  auto t4 = microtime();
  write_output(prefix, s);

  auto stage1 = t2 - t1;
  auto stage2 = t3 - t2;
  auto stage3 = t4 - t3;

  cout << "decode: " << stage1 << ". encode_given_state: " << stage2 << ". rebase: " << stage3 << "." << endl;

  cout << "Stage 1: {";
  for (auto t : stage1_time) {
    cout << t << ", ";
  }
  cout << "}" << endl;
  cout << "Stage 2: {";
  for (auto t : stage2_time) {
    cout << t << ", ";
  }
  cout << "}" << endl;
  cout << "Stage 3: {";
  for (auto t : stage3_time) {
    cout << t << ", ";
  }
  cout << "}" << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  // TODO: take file prefix to args
  string prefix = std::string(argv[argc-1]) + "/sintel01_";
  return nu::runtime_main_init(argc, argv, [=](int, char **) { do_work(prefix); });
}
