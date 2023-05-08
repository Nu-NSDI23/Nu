#!/usr/bin/env python3

import os
import shutil
import subprocess
import time

# ExCamera[6,16]
frame, N = 6, 16
batch = 48
src = "bunny_480p"

workspace = os.path.join("..", "samples", "workspace")
prefix = "bunny{:02d}"
src_dir = os.path.join("..", "samples", src)

vpxenc_bin = os.path.join("..", "bin", "vpxenc")
vp8dec_bin = "vp8decode"

def run():
  for i in range(batch):
    dir_name = os.path.join(workspace, "{:02d}".format(i))
    if not os.path.exists(dir_name):
      os.makedirs(dir_name)
    shutil.copy(os.path.join(src_dir, prefix.format(i) + ".y4m"), dir_name)

    prepare(i)
    vpxenc(i)

"""
Split the video into 6 frames chunks
"""
def prepare(i):
  input_file = os.path.join(workspace, "{:02d}".format(i), prefix.format(i) + ".y4m")
  output_file = os.path.join(workspace, "{:02d}".format(i), prefix.format(i) + "_%2d.y4m")
  subprocess.run(["ffmpeg", "-i", input_file, "-f", "segment", "-segment_time", "0.25", output_file])

"""
2. (Parallel)
Each thread runs Google's vpxenc VP8 encoder. The output is N compressed
frames: one key frame (typically about one megabyte) followed by N-1
interframes (about 10-30 kilobytes apiece).
"""
def vpxenc(idx):
  input_fn = os.path.join(workspace, "{:02d}".format(idx), prefix.format(idx) + "_{:02d}.y4m")
  vpx_fn = os.path.join(workspace, "{:02d}".format(idx), prefix.format(idx) + "_vpx_{:02d}.ivf")

  for i in range(N):
    input_file = input_fn.format(i)
    output_file = vpx_fn.format(i)
    subprocess.run([vpxenc_bin, "--codec=vp8", "--ivf", "--good", "--cpu-used=0", "--end-usage=cq", "--min-q=0", "--max-q=63", "--cq-level=31", "--buf-initial-sz=10000", "--buf-optimal-sz=20000", "--buf-sz=40000", "--undershoot-pct=100", "--passes=2", "--auto-alt-ref=1", "--threads=31", "--token-parts=0", "--tune=ssim", "--target-bitrate=4294967295", "-o", output_file, input_file])

if __name__ == "__main__":
  run()
