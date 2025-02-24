#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]; then
  echo "Using default directory ${OUTDIR} for output"
else
  OUTDIR=$1
  echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
  #Clone only if the repository does not exist.
  echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
  git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
  cd linux-stable
  echo "Checking out version ${KERNEL_VERSION}"
  git checkout ${KERNEL_VERSION}

  # TODO: Add your kernel build steps here
  cd ${OUTDIR}/linux-stable/
  make -j 4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
  make -j 4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE Image
  cp arch/arm64/boot/Image ${OUTDIR}
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]; then
  echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
  sudo rm -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,proc,sbin,sys,tmp,usr,var}
mkdir ${OUTDIR}/rootfs/usr/{bin,lib,sbin}
mkdir ${OUTDIR}/rootfs/var/log
tree -d ${OUTDIR}/rootfs
sudo chown -R root:root ${OUTDIR}/rootfs

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
  git clone git://busybox.net/busybox.git
  cd busybox
  git checkout ${BUSYBOX_VERSION}
  # TODO:  Configure busybox
  make distclean
  make defconfig
  sed -i 's/CONFIG_TC=y/CONFIG_TC=n/' .config
  sed -i 's|CONFIG_PREFIX="./_install"|CONFIG_PREFIX="../rootfs"|' .config
else
  cd busybox
fi

# TODO: Make and install busybox

make -j 4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
sudo make install

cd ${OUTDIR}/rootfs/
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
export ARM_SYSROOT=$(aarch64-none-linux-gnu-gcc -print-sysroot)
sudo cp -a $ARM_SYSROOT/lib64/libm.so.6 lib
sudo cp -a $ARM_SYSROOT/lib64/libresolv.so.2 lib
sudo cp -a $ARM_SYSROOT/lib64/libc.so.6 lib
# TODO: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1
# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

sudo cp writer /tmp/aeld/rootfs/home/
sudo cp *.sh /tmp/aeld/rootfs/home/
# TODO: Chown the  root directory

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root >../initramfs.cpio
cd ..
gzip initramfs.cpio
mkimage -A arm -O linux -T ramdisk -d initramfs.cpio.gz uRamdisk
