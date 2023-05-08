# Big Buck Bunny
curl https://media.xiph.org/video/derf/y4m/big_buck_bunny_480p24.y4m.xz -o bunny_480p.y4m.xz
xz -d bunny_480p.y4m.xz
mkdir bunny_480p
# 96 frames per segment
ffmpeg -i bunny_480p.y4m -f segment -segment_time 4 bunny_480p/bunny%2d.y4m
