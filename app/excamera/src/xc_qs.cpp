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

constexpr size_t N = 16;

// serialized decoder state
using DecoderBuffer = vector<uint8_t>;

using shard_ivf_type = nu::ShardedVector<IVF_MEM, false_type>;
using shard_decoder_type = nu::ShardedVector<DecoderBuffer, false_type>;
using shard_idx_type = nu::ShardedVector<size_t, false_type>;
using shard_buffer_type = nu::ShardedVector<BufferReader, false_type>;

// The excamera states
typedef struct {
  shard_ivf_type vpx0_ivf, vpx1_ivf, xc_ivf;
  shard_decoder_type dec_state, enc_state;
  shard_buffer_type raster_buffer;
  vector<IVF_MEM> final_ivf;
  vector<DecoderBuffer> dec_state_vec, rebased_state;
  IVF_MEM ivf0;
  DecoderBuffer state0;
} xc_t;

typedef struct {
  uint64_t start;
  uint64_t end;
} task_time;

vector<task_time> decode_time, enc_given_state_time, rebase_time;

void usage(const string &program_name) {
  cerr << "Usage: " << program_name << " [nu_args] [input_dir]" << endl;
}

tuple<vector<RasterHandle>, uint16_t, uint16_t>
read_raster(const BufferReader &buffer_reader) {
  YUV4MPEGBufferReader input_reader = YUV4MPEGBufferReader( buffer_reader );

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
                size_t idx,
                const string prefix,
                BufferReader &raster_buffer) {
  bool two_pass = false;
  double kf_q_weight = 1.0;
  bool extra_frame_chunk = false;
  EncoderQuality quality = BEST_QUALITY;

  auto t = read_raster(raster_buffer);
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
      vector<tuple<DecoderBuffer, task_time>> outputs;
      while (true) {
        auto ivf = ivfs_range.pop();
        if (!ivf) {
          break;
        }
        task_time t;
        t.start = microtime();
        auto out = decode(*ivf);
        t.end = microtime();
        outputs.emplace_back(out, t);
      }
      return outputs;
    },
    ivfs_range);

  auto outputs_vectors = dis_exec.get();
  auto join_view = ranges::join_view(outputs_vectors);
  auto vecs = vector<tuple<DecoderBuffer, task_time>>(join_view.begin(), join_view.end());
  s->state0 = get<0>(vecs[0]);
  for (size_t i = 0; i < N-1; i++) {
    s->dec_state.push_back(get<0>(vecs[i]));
    s->dec_state_vec.push_back(get<0>(vecs[i]));
    decode_time.push_back(get<1>(vecs[i]));
  }
  s->vpx0_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
}

void encode_all(shared_ptr<xc_t> s, const string prefix) {
  shard_idx_type idxs = nu::make_sharded_vector<size_t, false_type>(N-1);
  for (size_t i = 1; i < N; i++) {
    idxs.push_back(i);
  }

  auto sealed_ivfs = nu::to_sealed_ds(move(s->vpx1_ivf));
  auto sealed_dec_state = nu::to_sealed_ds(move(s->dec_state));
  auto sealed_raster_buffer = nu::to_sealed_ds(move(s->raster_buffer));
  auto sealed_idxs = nu::to_sealed_ds(move(idxs));
  auto encode_range = nu::make_zipped_ds_range(sealed_ivfs, sealed_dec_state, sealed_raster_buffer, sealed_idxs);

  auto dist_exec = nu::make_distributed_executor(
    +[](decltype(encode_range) &encode_range, const string prefix) {
      vector<tuple<IVF_MEM, DecoderBuffer, task_time>> outputs;
      while (true) {
        auto pop = encode_range.pop();
        if (!pop) {
          break;
        }

        task_time t;
        t.start = microtime();
  
        auto ivf = get<0>(*pop);
        auto decoder = get<1>(*pop);
        auto raster_buffer = get<2>(*pop);
        auto idx = get<3>(*pop);

        // do enc given state twice to match the excamera's performance result 
        auto out = enc_given_state(ivf, decoder, nullptr, idx, prefix, raster_buffer);
        out = enc_given_state(ivf, decoder, nullptr, idx, prefix, raster_buffer);

        t.end = microtime();
        outputs.emplace_back(get<0>(out), get<1>(out), t);
      }
      return outputs;
    }, encode_range, prefix);

  auto outputs_vectors = dist_exec.get();
  auto join_view = ranges::join_view(outputs_vectors);
  auto vecs = vector<tuple<IVF_MEM, DecoderBuffer, task_time>>(join_view.begin(), join_view.end());

  s->vpx1_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
  s->dec_state = nu::to_unsealed_ds(move(sealed_dec_state));
  s->raster_buffer = nu::to_unsealed_ds(move(sealed_raster_buffer));
  idxs = nu::to_unsealed_ds(move(sealed_idxs));

  s->enc_state.push_back(s->state0);
  for (size_t i = 0; i < N-2; i++) {
    s->xc_ivf.push_back(get<0>(vecs[i]));
    s->enc_state.push_back(get<1>(vecs[i]));
    enc_given_state_time.push_back(get<2>(vecs[i]));
  }
  s->xc_ivf.push_back(get<0>(vecs[N-2]));
  enc_given_state_time.push_back(get<2>(vecs[N-2]));
}

void rebase(shared_ptr<xc_t> s, const string prefix) {
  s->final_ivf.push_back(s->ivf0);
  s->rebased_state.push_back(s->state0);

  auto sealed_ivfs = nu::to_sealed_ds(std::move(s->xc_ivf));
  auto sealed_dec_state = nu::to_sealed_ds(std::move(s->dec_state));
  auto sealed_raster_buffer = nu::to_sealed_ds(std::move(s->raster_buffer));
  auto ivf_it = sealed_ivfs.cbegin();
  auto state_it = sealed_dec_state.cbegin();
  auto buffer_it = sealed_raster_buffer.cbegin();
  for (size_t i = 1; ivf_it != sealed_ivfs.cend() && state_it != sealed_dec_state.cend() && buffer_it != sealed_raster_buffer.cend(); ++ivf_it, ++state_it, ++buffer_it, ++i) {
    task_time t;
    t.start = microtime();
    IVF_MEM ivf = *ivf_it;
    DecoderBuffer decoder = s->rebased_state[i-1];
    DecoderBuffer prev_decoder = *state_it;
    BufferReader buffer = *buffer_it;

    auto output = enc_given_state(ivf, decoder, &prev_decoder, i, prefix, buffer);
    auto rebased_ivf = get<0>(output);
    s->rebased_state.push_back(get<1>(output));
    s->final_ivf.push_back(merge(s->final_ivf[i-1], rebased_ivf));
    t.end = microtime();
    rebase_time.push_back(t);
  }

  s->xc_ivf = nu::to_unsealed_ds(move(sealed_ivfs));
  s->dec_state = nu::to_unsealed_ds(move(sealed_dec_state));
  s->raster_buffer = nu::to_unsealed_ds(move(sealed_raster_buffer));
}

void write_output(shared_ptr<xc_t> s, const string prefix) {
  const string final_file = prefix + "final.ivf";
  s->final_ivf[N-1].write(final_file);
}

void read_input(shared_ptr<xc_t> s, const string prefix) {
  for (size_t i = 0; i < N; i++) {
    ostringstream vpxss;
    vpxss << prefix << "vpx_" << setw(2) << setfill('0') << i << ".ivf";
    const string vpx_file = vpxss.str();
    if (i != N - 1) {
      s->vpx0_ivf.push_back(IVF_MEM(vpx_file));
    }
    if (i != 0) {
      s->vpx1_ivf.push_back(IVF_MEM(vpx_file));
      ostringstream inputss;
      inputss << prefix << setw(2) << setfill('0') << i << ".y4m";
      const string input_file = inputss.str();
      s->raster_buffer.push_back(BufferReader(input_file));
    } else {
      s->ivf0 = IVF_MEM(vpx_file);
    }
  }
}

int do_work(const string prefix) {
  auto s = make_shared<xc_t>();

  s->vpx0_ivf = nu::make_sharded_vector<IVF_MEM, false_type>(N-1);
  s->vpx1_ivf = nu::make_sharded_vector<IVF_MEM, false_type>(N-1);
  s->xc_ivf = nu::make_sharded_vector<IVF_MEM, false_type>(N-1);
  s->dec_state = nu::make_sharded_vector<DecoderBuffer, false_type>(N-1);
  s->enc_state = nu::make_sharded_vector<DecoderBuffer, false_type>(N-1);
  s->raster_buffer = nu::make_sharded_vector<BufferReader, false_type>(N-1);

  auto t0 = microtime();
  read_input(s, prefix);
  auto t1 = microtime();
  decode_all(s);
  auto t2 = microtime();
  encode_all(s, prefix);
  auto t3 = microtime();
  rebase(s, prefix);
  auto t4 = microtime();
  write_output(s, prefix);

  cout << "load data: " << t0 - t0 << ", " << t1 - t0 << endl;

  cout << "decode: " << t1 - t0 << ", " << t2 - t0 << endl;
  for (auto t : decode_time) {
    cout << t.start - t0 << ", ";
  }
  cout << endl;
  for (auto t : decode_time) {
    cout << t.end - t0 << ", ";
  }
  cout << endl;

  cout << "encode given state: " << t2 - t0 << ", " << t3 - t0 << endl;
  for (auto t : enc_given_state_time) {
    cout << t.start - t0 << ", ";
  }
  cout << endl;
  for (auto t : enc_given_state_time) {
    cout << t.end - t0 << ", ";
  }
  cout << endl;

  cout << "rebase: " << t3 - t0 << ", " << t4 - t0 << endl;
  for (auto t : rebase_time) {
    cout << t.start - t0 << ", ";
  }
  cout << endl;
  for (auto t : rebase_time) {
    cout << t.end - t0 << ", ";
  }
  cout << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  // TODO: take file prefix to args
  string prefix = string(argv[argc-1]) + "/" + string(argv[argc-2]) + "01_";
  return nu::runtime_main_init(argc, argv, [=](int, char **) { do_work(prefix); });
}
