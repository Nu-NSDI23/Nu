#!/bin/bash
# run with sudo

# needed for the iokernel's shared memory
sysctl -w kernel.shm_rmid_forced=1
sysctl -w kernel.shmmax=18446744073692774399
sysctl -w vm.hugetlb_shm_group=27
sysctl -w vm.max_map_count=16777216
sysctl -w net.core.somaxconn=3072

# set up the ksched module
rmmod ksched
rm /dev/ksched
insmod $(dirname $0)/../ksched/build/ksched.ko
mknod /dev/ksched c 280 0
chmod uga+rwx /dev/ksched

# reserve huge pages
for n in /sys/devices/system/node/node*; do
echo 5192 > ${n}/hugepages/hugepages-2048kB/nr_hugepages
done

# disable ASLR
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space >/dev/null

# enable page prezeroing
echo 1 > /sys/kernel/mm/zero_page/enabled

# load msr module
modprobe msr

# clean cache
sync; echo 3 > /proc/sys/vm/drop_caches
