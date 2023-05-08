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
#include <functional>
#include <tuple>

#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

#include <runtime.h>
#include <thread.h>
#include <sync.h>
#include "nu/runtime.hpp"
#include "nu/sharded_vector.hpp"
#include "nu/sharded_queue.hpp"
#include "nu/dis_executor.hpp"
#include "nu/sharded_ds_range.hpp"
#include "nu/zipped_ds_range.hpp"

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
using namespace chrono;
namespace fs = filesystem;

// x = 6, each batch has 16 * 6 = 96 frames
constexpr size_t N = 16;
constexpr size_t BATCH = 48;

// serialized decoder state
using DecoderBuffer = vector<uint8_t>;

using shard_ivf_type = nu::ShardedVector<IVF_MEM, true_type>;
using shard_decoder_type = nu::ShardedVector<DecoderBuffer, true_type>;

// The excamera states
typedef struct {
  shard_ivf_type vpx0_ivf, vpx1_ivf, xc_ivf;
  shard_decoder_type dec_state, enc_state;
  vector<IVF_MEM> final_ivf;
  IVF_MEM ivf0[BATCH];
  DecoderBuffer state0[BATCH];
} xc_t;

void usage(const string &program_name) {
  cerr << "Usage: " << program_name << " [nu_args] [input_dir]" << endl;
}

tuple<vector<RasterHandle>, uint16_t, uint16_t>
read_raster(const string prefix, const string fname, size_t batch, size_t idx) {
  ostringstream inputss;
  inputss << prefix << "/" << setw(2) << setfill('0') << batch << "/" << fname << setw(2) << setfill('0') << batch << "_" << setw(2) << setfill('0') << idx << ".y4m";
  const string input_file = inputss.str();

  YUV4MPEGBufferReader input_reader = YUV4MPEGBufferReader( input_file );

  // pre-read original rasters
  vector<RasterHandle> original_rasters;
  for ( size_t i = 0; ; i++ ) {
    auto next_raster = input_reader.get_next_frame();

    if ( next_raster.initialized() ) {
      original_rasters.emplace_back( next_raster.get() );
    } else {
      break;
    }
  }

  tuple<vector<RasterHandle>, uint16_t, uint16_t> output(original_rasters, input_reader.display_width(), input_reader.display_height());
  return output;
}

DecoderBuffer decode(IVF_MEM &ivf) {
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
      return odata.get();
    }
  }

  throw runtime_error( "invalid frame number" );
}

tuple<IVF_MEM, DecoderBuffer>
enc_given_state(IVF_MEM & pred_ivf,
                DecoderBuffer & input_state,
                DecoderBuffer * prev_state,
                const string prefix,
                const string fname,
                size_t batch,
                size_t idx) {
  bool two_pass = false;
  double kf_q_weight = 1.0;
  bool extra_frame_chunk = false;
  EncoderQuality quality = BEST_QUALITY;

  auto t = read_raster(prefix, fname, batch, idx);
  vector<RasterHandle> original_rasters = get<0>(t);
  uint16_t display_width = get<1>(t);
  uint16_t display_height = get<2>(t);

  Decoder pred_decoder( display_width, display_height );
  if (prev_state) {
    pred_decoder = EncoderStateDeserializer_MEM::build<Decoder>( *prev_state );
  }

  IVFWriter_MEM ivf_writer { "VP80", display_width, display_height, 1, 1 };

  /* pre-read all the prediction frames */
  vector<pair<Optional<KeyFrame>, Optional<InterFrame> > > prediction_frames;

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

  Encoder encoder( EncoderStateDeserializer_MEM::build<Decoder>(input_state) , two_pass, quality );

  ivf_writer.set_expected_decoder_entry_hash( encoder.export_decoder().get_hash().hash() );

  encoder.reencode( original_rasters, prediction_frames, kf_q_weight,
                    extra_frame_chunk, ivf_writer );

  EncoderStateSerializer odata = {};
  encoder.export_decoder().serialize(odata);
  tuple<IVF_MEM, DecoderBuffer> output(ivf_writer.ivf(), odata.get());
  return output;
}

IVF_MEM merge(IVF_MEM & ivf1, IVF_MEM & ivf2) {
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

  return ivf_writer.ivf();
}

void decode_all(shared_ptr<xc_t> s) {
  auto sealed_ivfs = nu::to_sealed_ds(move(s->vpx0_ivf));
  auto ivfs_range = nu::make_contiguous_ds_range(sealed_ivfs);
  
  auto dis_exec = nu::make_distributed_executor(
    +[](decltype(ivfs_range) &ivfs_range) {
      vector<DecoderBuffer> outputs;
      while (true) {
        auto ivf = ivfs_range.pop();
        if (!ivf) {
          break;
        }
        auto out = decode(*ivf);
        outputs.push_back(out);
      }
      return outputs;
    },
    ivfs_range);

  auto outputs_vectors = dis_exec.get();
  auto join_view = ranges::join_view(outputs_vectors);
  auto vecs = vector<DecoderBuffer>(join_view.begin(), join_view.end());
  for (size_t b = 0; b < BATCH; b++) {
    s->state0[b] = (vecs[b * (N - 1)]);
    for (size_t i = 0; i < N-1; i++) {
      s->dec_state.push_back(vecs[b * (N - 1) + i]);
    }
  }
  s->vpx0_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
}

void encode_all(shared_ptr<xc_t> s, const string prefix, const string fname) {
  auto batches = nu::make_sharded_vector<size_t, true_type>((N-1) * BATCH);
  auto idxs = nu::make_sharded_vector<size_t, true_type>((N-1) * BATCH);
  for (size_t b = 0; b < BATCH; b++) {
    for (size_t i = 1; i < N; i++) {
      idxs.push_back(i);
      batches.push_back(b);
    }
  }

  auto sealed_ivfs = nu::to_sealed_ds(move(s->vpx1_ivf));
  auto sealed_dec_state = nu::to_sealed_ds(move(s->dec_state));
  auto sealed_batches = nu::to_sealed_ds(move(batches));
  auto sealed_idxs = nu::to_sealed_ds(move(idxs));
  auto encode_range = nu::make_zipped_ds_range(sealed_ivfs, sealed_dec_state, sealed_batches, sealed_idxs);

  auto dist_exec = nu::make_distributed_executor(
    +[](decltype(encode_range) &encode_range, const string prefix, const string fname) {
      uint64_t begin = microtime();
      vector<tuple<IVF_MEM, DecoderBuffer>> outputs;
      while (true) {
        auto pop = encode_range.pop();
        if (!pop) {
          break;
        }
  
        auto ivf = get<0>(*pop);
        auto decoder = get<1>(*pop);
        auto batch = get<2>(*pop);
        auto idx = get<3>(*pop);

        // do enc given state twice to match the excamera's performance result 
        auto out = enc_given_state(ivf, decoder, nullptr, prefix, fname, batch, idx);
        out = enc_given_state(ivf, decoder, nullptr, prefix, fname, batch, idx);
        outputs.emplace_back(out);
      }
      cout << microtime() - begin << endl;
      return outputs;
    }, encode_range, prefix, fname);

  auto outputs_vectors = dist_exec.get();
  auto join_view = ranges::join_view(outputs_vectors);
  auto vecs = vector<tuple<IVF_MEM, DecoderBuffer>>(join_view.begin(), join_view.end());

  idxs = nu::to_unsealed_ds(move(sealed_idxs));
  batches = nu::to_unsealed_ds(move(sealed_batches));
  s->vpx1_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
  s->dec_state = nu::to_unsealed_ds(move(sealed_dec_state));

  for (size_t b = 0; b < BATCH; b++) {
    s->enc_state.push_back(s->state0[b]);
    for (size_t i = 0; i < N-2; i++) {
      s->xc_ivf.push_back(get<0>(vecs[b * (N - 1) + i]));
      s->enc_state.push_back(get<1>(vecs[b * (N - 1) + i]));
    }
    s->xc_ivf.push_back(get<0>(vecs[b * (N - 1) + N - 2]));
  }
}

void rebase(shared_ptr<xc_t> s, const string prefix, const string fname) {
  auto sealed_ivfs = nu::to_sealed_ds(std::move(s->xc_ivf));
  auto sealed_dec_state = nu::to_sealed_ds(std::move(s->dec_state));
  auto ivf0 = nu::make_sharded_vector<IVF_MEM, true_type>(BATCH);
  auto state0 = nu::make_sharded_vector<DecoderBuffer, true_type>(BATCH);
  
  auto stitch_ivfs = nu::make_sharded_vector<vector<IVF_MEM>, true_type>(BATCH);
  auto stitch_dec_state = nu::make_sharded_vector<vector<DecoderBuffer>, true_type>(BATCH);
  auto batches = nu::make_sharded_vector<size_t, true_type>(BATCH);

  auto ivf_it = sealed_ivfs.cbegin();
  auto state_it = sealed_dec_state.cbegin();
  for (size_t b = 0; b < BATCH; b++) {
    vector<IVF_MEM> batch_ivf;
    vector<DecoderBuffer> batch_dec_state;
    for (size_t i = 0; i < N - 1; ++ivf_it, ++state_it, ++i) {
      batch_ivf.push_back(*ivf_it);
      batch_dec_state.push_back(*state_it);
    }
    stitch_ivfs.push_back(batch_ivf);
    stitch_dec_state.push_back(batch_dec_state);
    ivf0.push_back(s->ivf0[b]);
    state0.push_back(s->state0[b]);
    batches.push_back(b);
  }

  auto sealed_stitch_ivfs = nu::to_sealed_ds(move(stitch_ivfs));
  auto sealed_stitch_dec_state = nu::to_sealed_ds(move(stitch_dec_state));
  auto sealed_batches = nu::to_sealed_ds(move(batches));
  auto sealed_ivf0 = nu::to_sealed_ds(move(ivf0));
  auto sealed_state0 = nu::to_sealed_ds(move(state0));
  auto stitch_range = nu::make_zipped_ds_range(sealed_stitch_ivfs, sealed_stitch_dec_state, sealed_batches, sealed_ivf0, sealed_state0);

  auto dist_exec = nu::make_distributed_executor(
    +[](decltype(stitch_range) &stitch_range, const string prefix, const string fname) {
      uint64_t begin = microtime();
      vector<IVF_MEM> outputs;
      while (true) {
        auto pop = stitch_range.pop();
        if (!pop) {
          break;
        }
  
        auto ivfs = get<0>(*pop);
        auto dec_state = get<1>(*pop);
        auto batch = get<2>(*pop);
        auto prev_ivf = get<3>(*pop);
        auto prev_state = get<4>(*pop);

        for (size_t i = 0; i < N - 1; i++) {
          IVF_MEM ivf = ivfs[i];
          DecoderBuffer prev_decoder = dec_state[i];

          auto output =  enc_given_state(ivf, prev_state, &prev_decoder, prefix, fname, batch, i);
          auto rebased_ivf = get<0>(output);
          prev_state = get<1>(output);
          prev_ivf = merge(prev_ivf, rebased_ivf);
        }
        outputs.push_back(prev_ivf);
      }
      cout << microtime() - begin << endl;
      return outputs;
    }, stitch_range, prefix, fname);
  auto outputs_vectors = dist_exec.get();
  auto join_view = ranges::join_view(outputs_vectors);
  s->final_ivf = vector<IVF_MEM>(join_view.begin(), join_view.end());

  ivf0 = nu::to_unsealed_ds(move(sealed_ivf0));
  state0 = nu::to_unsealed_ds(move(sealed_state0));
  stitch_ivfs = nu::to_unsealed_ds(move(sealed_stitch_ivfs));
  stitch_dec_state = nu::to_unsealed_ds(move(sealed_stitch_dec_state));
  batches = nu::to_unsealed_ds(move(sealed_batches));
  s->xc_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
  s->dec_state = nu::to_unsealed_ds(move(sealed_dec_state));
}

void write_output(shared_ptr<xc_t> s, const string prefix, const string fname) {
  for (size_t b = 0; b < BATCH; b++) {
    ostringstream finalss;
    finalss << prefix << "/" << setw(2) << setfill('0') << b << "/" << fname << setw(2) << setfill('0') << b << ".y4m";
    const string final_file = finalss.str();
    s->final_ivf[b].write(final_file);
  }
}

void read_input(shared_ptr<xc_t> s, const string prefix, const string fname) {
  for (size_t b = 0; b < BATCH; b++) {
    for (size_t i = 0; i < N; i++) {
      ostringstream vpxss;
      vpxss << prefix << "/" << setw(2) << setfill('0') << b << "/" << fname << setw(2) << setfill('0') << b << "_vpx_" << setw(2) << setfill('0') << i << ".ivf";
      const string vpx_file = vpxss.str();
      if (i != N - 1) {
        s->vpx0_ivf.push_back(IVF_MEM(vpx_file));
      }
      if (i != 0) {
        s->vpx1_ivf.push_back(IVF_MEM(vpx_file));
      } else {
        s->ivf0[b] = IVF_MEM(vpx_file);
      }
    }
  }
}

int do_work(const string prefix, const string fname) {
  auto s = make_shared<xc_t>();

  s->vpx0_ivf = nu::make_sharded_vector<IVF_MEM, true_type>(BATCH * (N - 1));
  s->vpx1_ivf = nu::make_sharded_vector<IVF_MEM, true_type>(BATCH * (N - 1));
  s->xc_ivf = nu::make_sharded_vector<IVF_MEM, true_type>(BATCH * (N - 1));
  s->dec_state = nu::make_sharded_vector<DecoderBuffer, true_type>(BATCH * (N - 1));
  s->enc_state = nu::make_sharded_vector<DecoderBuffer, true_type>(BATCH * (N - 1));

  auto t0 = microtime();
  read_input(s, prefix, fname);
  auto t1 = microtime();
  cout << "parallel" << endl;
  decode_all(s);
  encode_all(s, prefix, fname);
  auto t3 = microtime();
  cout << "serial" << endl;
  rebase(s, prefix, fname);
  auto t4 = microtime();
  write_output(s, prefix, fname);

  cout << t1 - t0 << ", " << t3 - t1 << ", " << t4 - t3 << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  // TODO: take file prefix to args
  string prefix = string(argv[argc-1]);
  string fname = string(argv[argc-2]);
  return nu::runtime_main_init(argc, argv, [=](int, char **) { do_work(prefix, fname); });
}
