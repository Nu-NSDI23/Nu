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
namespace fs = std::filesystem;

size_t N = 16;

void usage(const string &program_name) {
  cerr << "Usage: " << program_name << " <input_dir>" << endl;
}

bool decode(const string input, const string output) {
  IVF ivf(input);
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
      decoder.serialize( odata );
      odata.write( output );
      return true;
    }
  }

  throw runtime_error( "invalid frame number" );
}

void enc_given_state(const string input_file,
                     const string output_file,
                     const string input_state, 
                     const string output_state,
                     const string pred,
                     const string prev_state) {
  bool two_pass = false;
  double kf_q_weight = 1.0;
  bool extra_frame_chunk = false;
  EncoderQuality quality = BEST_QUALITY;

  shared_ptr<FrameInput> input_reader = make_shared<YUV4MPEGReader>( input_file );

  Decoder pred_decoder( input_reader->display_width(), input_reader->display_height() );
  if (prev_state != "") {
    pred_decoder = EncoderStateDeserializer::build<Decoder>( prev_state );
  }

  IVFWriter output { output_file, "VP80", input_reader->display_width(), input_reader->display_height(), 1, 1 };
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

  /* pre-read all the prediction frames */
  vector<pair<Optional<KeyFrame>, Optional<InterFrame> > > prediction_frames;

  IVF pred_ivf { pred };

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

  Encoder encoder( EncoderStateDeserializer::build<Decoder>( input_state ),
                   two_pass, quality );

  output.set_expected_decoder_entry_hash( encoder.export_decoder().get_hash().hash() );

  encoder.reencode( original_rasters, prediction_frames, kf_q_weight,
                    extra_frame_chunk, output );

  EncoderStateSerializer odata = {};
  encoder.export_decoder().serialize(odata);
  odata.write(output_state);
}

void merge(const string input1, const string input2, const string output) {
  IVF ivf1( input1 );
  IVF ivf2( input2 );

  if ( ivf1.width() != ivf2.width() or ivf1.height() != ivf2.height() ) {
    throw runtime_error( "cannot merge ivfs with different dimensions." );
  }

  IVFWriter output_ivf( output, "VP80", ivf1.width(), ivf1.height(), 1, 1 );

  for ( size_t i = 0; i < ivf1.frame_count(); i++ ) {
    output_ivf.append_frame( ivf1.frame( i ) );
  }

  for ( size_t i = 0; i < ivf2.frame_count(); i++ ) {
    output_ivf.append_frame( ivf2.frame( i ) );
  }
}

void decode_all(const string prefix) {
  for (size_t i = 0; i < N; i++) {
    ostringstream inputss, outputss;
    inputss << prefix << "vpx_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string input_file = inputss.str();

    outputss << prefix << "dec_" << std::setw(2) << std::setfill('0') << i << ".state";
    const string output_file = outputss.str();
    
    decode(input_file, output_file);
  }
}

void encode_all(const string prefix) {
  // first pass
  for (size_t i = 0; i < N; i++) {
    ostringstream inputss, outputss, instatess, outstatess, predss;
    inputss << prefix << std::setw(2) << std::setfill('0') << i << ".y4m";
    const string input_file = inputss.str();

    outputss << prefix << "xc0_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string output_file = outputss.str();

    instatess << prefix << "dec_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".state";
    const string input_state = instatess.str();

    outstatess << prefix << "enc0_" << std::setw(2) << std::setfill('0') << i << ".state";
    const string output_state = outstatess.str();

    predss << prefix << "vpx_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string pred = predss.str();
    
    if (i == 0) {
      fs::copy(pred, output_file, fs::copy_options::update_existing);
      fs::copy(input_state, output_state, fs::copy_options::update_existing);
    } else {
      enc_given_state(input_file, output_file, input_state, output_state, pred, "");
    }
  }

  // second pass
  for (size_t i = 0; i < N; i++) {
    ostringstream inputss, outputss, instatess, prevstatess, outstatess, predss;
    inputss << prefix << std::setw(2) << std::setfill('0') << i << ".y4m";
    const string input_file = inputss.str();

    outputss << prefix << "xc1_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string output_file = outputss.str();

    instatess << prefix << "enc0_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".state";
    const string input_state = instatess.str();

    prevstatess << prefix << "dec_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".state";
    const string prev_state = prevstatess.str();

    outstatess << prefix << "enc1_" << std::setw(2) << std::setfill('0') << i << ".state";
    const string output_state = outstatess.str();

    predss << prefix << "xc0_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string pred = predss.str();
    
    if (i == 0) {
      fs::copy(pred, output_file, fs::copy_options::update_existing);
      fs::copy(input_state, output_state, fs::copy_options::update_existing);
    } else {
      enc_given_state(input_file, output_file, input_state, output_state, pred, prev_state);
    }
  }
}

void rebase(const string prefix) {
  // serial rebase
  for (size_t i = 0; i < N; i++) {
    ostringstream inputss, outputss, instatess, prevstatess, outstatess, predss, finalss, prev_finalss;
    inputss << prefix << std::setw(2) << std::setfill('0') << i << ".y4m";
    const string input_file = inputss.str();

    outputss << prefix << "rebased_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string output_file = outputss.str();

    instatess << prefix << "rebased_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".state";
    const string input_state = instatess.str();

    prevstatess << prefix << "enc0_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".state";
    const string prev_state = prevstatess.str();

    outstatess << prefix << "rebased_" << std::setw(2) << std::setfill('0') << i << ".state";
    const string output_state = outstatess.str();

    predss << prefix << "xc1_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string pred = predss.str();

    finalss << prefix << "final_" << std::setw(2) << std::setfill('0') << i << ".ivf";
    const string final_file = finalss.str();
    
    prev_finalss << prefix << "final_" << std::setw(2) << std::setfill('0') << ((i == 0) ? 0 : (i - 1)) << ".ivf";
    const string prev_final_file = prev_finalss.str();

    if (i == 0) {
      fs::copy(pred, final_file, fs::copy_options::update_existing);
      fs::copy(prev_state, output_state, fs::copy_options::update_existing);
    } else {
      enc_given_state(input_file, output_file, input_state, output_state, pred, prev_state);
      merge(prev_final_file, output_file, final_file);
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  // TODO: take file prefix to args
  string prefix = std::string(argv[1]) + "/sintel01_";

  decode_all(prefix);
  encode_all(prefix);
  rebase(prefix);

  return 0;
}
