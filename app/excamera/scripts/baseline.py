#!/usr/bin/env python3

import os
import shutil
import subprocess
import time

# ExCamera[6,16]
frame, N = 6, 16

workspace = os.path.join("..", "samples", "workspace")
prefix = "sintel01"

vpxenc_bin = os.path.join("..", "bin", "vpxenc")
xcdump_bin = "xc-dump"
xcenc_bin = "xc-enc"
xcmerge_bin = "xc-merge"
vp8dec_bin = "vp8decode"

input_fn = os.path.join(workspace, prefix + "_{:02d}.y4m")
vpx_fn = os.path.join(workspace, prefix + "_vpx_{:02d}.ivf")
dec_state_fn = os.path.join(workspace, prefix + "_dec_{:02d}.state")
enc_state0_fn = os.path.join(workspace, prefix + "_enc0_{:02d}.state")
enc_state1_fn = os.path.join(workspace, prefix + "_enc1_{:02d}.state")
xcenc0_fn = os.path.join(workspace, prefix + "_xc0_{:02d}.ivf")
xcenc1_fn = os.path.join(workspace, prefix + "_xc1_{:02d}.ivf")
rebase_state_fn = os.path.join(workspace, prefix + "_rebase_{:02d}.state")
rebased_fn = os.path.join(workspace, prefix + "_rebased_{:02d}.ivf")
final_fn = os.path.join(workspace, prefix + "_final_{:02d}.ivf")
final_file = os.path.join(workspace, prefix + "_output.ivf")
final_decoded_file = os.path.join(workspace, prefix + "_output.y4m")

def run():
  prepare()
  t1 = time.time()
  vpxenc()
  t2 = time.time()
  decode()
  t3 = time.time()
  enc_given_state()
  t4 = time.time()
  rebase()
  t5 = time.time()
  redecode()

  print(f"vpxenc time = {t2 - t1}; decode time = {t3 - t2}; enc-given-state time = {t4 - t3}; rebase time = {t5 - t4}")

"""
Split the video into 6 frames chunks
"""
def prepare():
  input_file = os.path.join(workspace, prefix + ".y4m")
  output_file = os.path.join(workspace, prefix + "_%2d.y4m")
  subprocess.run(["ffmpeg", "-i", input_file, "-f", "segment", "-segment_time", "0.25", output_file])

"""
2. (Parallel)
Each thread runs Google's vpxenc VP8 encoder. The output is N compressed
frames: one key frame (typically about one megabyte) followed by N-1
interframes (about 10-30 kilobytes apiece).
"""
def vpxenc():
  for i in range(N):
    input_file = input_fn.format(i)
    output_file = vpx_fn.format(i)
    subprocess.run([vpxenc_bin, "--codec=vp8", "--ivf", "--good", "--cpu-used=0", "--end-usage=cq", "--min-q=0", "--max-q=63", "--cq-level=31", "--buf-initial-sz=10000", "--buf-optimal-sz=20000", "--buf-sz=40000", "--undershoot-pct=100", "--passes=2", "--auto-alt-ref=1", "--threads=1", "--token-parts=0", "--tune=ssim", "--target-bitrate=4294967295", "-o", output_file, input_file])

"""
3. (Parallel)
Each thread runs ExCamera's decode operator N times to calculate the final
state, then sends that state to the next thread in the batch.
"""
def decode():
  for i in range(N):
    input_file = vpx_fn.format(i)
    output_file = dec_state_fn.format(i)
    subprocess.run([xcdump_bin, input_file, output_file])

"""
4. (Parallel)
The first thread is now finished and uploads its output, starting with a key
frame. The other N - 1 threads run encode-given-state to encode the first
image as an interframe, given the state received from the previous thread.
The key frame from vpxenc is thrown away; encode-given-state works de novo
from the original raw image.
"""
def enc_given_state():
  # first thread is done
  shutil.copyfile(vpx_fn.format(0), xcenc0_fn.format(0))
  shutil.copyfile(vpx_fn.format(0), xcenc1_fn.format(0))
  shutil.copyfile(dec_state_fn.format(0), enc_state0_fn.format(0))
  shutil.copyfile(dec_state_fn.format(0), enc_state1_fn.format(0))

  for i in range(1, N):
    input_file = input_fn.format(i)
    output_file = xcenc0_fn.format(i)
    state_file = dec_state_fn.format(i-1)
    output_state_file = enc_state0_fn.format(i)
    pred_file = vpx_fn.format(i)
    subprocess.run([xcenc_bin, "-i", "y4m", "-O", output_state_file, "-o", output_file, "-r", "-I", state_file, "-p", pred_file, "--no-wait", input_file])

  for i in range(1, N):
    input_file = input_fn.format(i)
    output_file = xcenc1_fn.format(i)
    prev_state_file = dec_state_fn.format(i-1)
    state_file = enc_state0_fn.format(i-1)
    output_state_file = enc_state1_fn.format(i)
    pred_file = vpx_fn.format(i)
    subprocess.run([xcenc_bin, "-i", "y4m", "-O", output_state_file, "-o", output_file, "-r", "-I", state_file, "-S", prev_state_file, "-p", pred_file, "--no-wait", input_file])

"""
5. (Serial) 
The first remaining thread runs rebase to rewrite interframes 2..N in terms
of the state left behind by its new first frame. It sends its final state to
the next thread, which runs rebase to rewrite all its frames in terms of the
given state. Each thread continues in turn. After a thread completes, it
uploads its transformed output and quits.
"""
def rebase():
  shutil.copyfile(xcenc1_fn.format(0), final_fn.format(0))
  shutil.copyfile(enc_state1_fn.format(0), rebase_state_fn.format(0))

  # rebase all videos
  for i in range(1, N):
    input_file = input_fn.format(i)
    output_file = rebased_fn.format(i)
    prev_state_file = enc_state0_fn.format(i-1)
    state_file = rebase_state_fn.format(i-1)
    output_state_file = rebase_state_fn.format(i)
    pred_file = xcenc1_fn.format(i)
    subprocess.run([xcenc_bin, "-i", "y4m", "-O", output_state_file, "-o", output_file, "-r", "-I", state_file, "-S", prev_state_file, "-p", pred_file, "--no-wait", input_file])

  # stitch them together
  for i in range(1, N):
    input_file = final_fn.format(i-1)
    input2_file = rebased_fn.format(i)
    output_file = final_fn.format(i)
    subprocess.run([xcmerge_bin, input_file, input2_file, output_file])

  shutil.copyfile(final_fn.format(N-1), final_file)

"""
Decode back to y4m for viewing
"""
def redecode():
  subprocess.run([vp8dec_bin, "-o", final_decoded_file, final_file])

if __name__ == "__main__":
  run()
