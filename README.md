# Nu System

Nu is a new datacenter system that enables developers to build fungible applications that can use datacenter resources wherever they are—even if they come from different machines—without the need of reserving them, thereby significantly improving resource efficiency. Currently, Nu supports C++ applications.

## Paper
* [Nu: Achieving Microsecond-Scale Resource Fungibility with Logical Processes](https://www.usenix.org/system/files/nsdi23-ruan.pdf)<br>
Zhenyuan Ruan, Seo Jin Park, Marcos Aguilera, Adam Belay, Malte Schwarzkopf<br>
20th USENIX Symposium on Networked Systems Design and Implementation ([NSDI '23](https://www.usenix.org/conference/nsdi23))<br>

## Supported Platform
We recommend you run Nu on [Cloudlab](https://www.cloudlab.us/). Currently, Nu supports Cloudlab's r650, r6525, c6525, d6515, and xl170 instances. In theory, Nu is compatible with any X86 server equipped with a Mellanox CX5/CX5 NIC and Ethernet network. However, it may take some porting effort to adapt Nu to the platform not listed above.

## Build Instructions
### Configure Cloudlab Instances
1) Apply for a Cloudlab account if you do not have one.
2) Now you have logged into the Cloublab console. Click `Experiments`|-->`Create Experiment Profile`. Upload `cloudlab.profile` we provided.
3) Instantiate machines using the profile. 

### Build Nu
You can build Nu by simply executing the `build.sh` script we provided. You can either build on all of your machines using the same path or build on one machine and then distribute the folder to other machines. The build script will automatically execute the `setup.sh` script to correctly set up the environment for running Nu. You need to manually rerun the setup script each time after rebooting the machine.

After building Nu, you can test it by executing the `test.sh` script on one machine (don't run it simultaneously on multiple machines as it will cause interference). You are expected to see all unit tests passed. All test files are located in the `test` folder; they also serve as a good reference to how to write Nu programs.

## Reproducing Paper Results
We include our code and push-button scripts in the ``exp`` folder.

## Repo Structure
```
Github Repo Root
 |---- app                 # Ported applications.
 |---- bench               # Microbenhmarks.
 |---- bin                 # AIFM code base.
 |---- caladan             # A modified Caladan used by Nu. DO NOT USE OTHER VERSIONS.
 |---- inc                 # Header files.
 |---- src                 # Source files.
 |---- test                # All unit tests.
 |---- 5.10-prezero.patch  # A kernel patch that speeds up mmap() by prezeroing pages.
 |---- build.sh            # A push-button build script.
 |---- cloudlab.profile    # Our cloudlab profile.
 |---- setup.sh            # A script that sets up the environment for running Nu.
 |---- shared.sh           # A helper script used by other scripts.
 |---- test.sh             # A push-button test script that runs all unit tests.
```

## Contact
Contact [Zain Ruan](mailto:zainruan@csail.mit.edu) for any questions.
