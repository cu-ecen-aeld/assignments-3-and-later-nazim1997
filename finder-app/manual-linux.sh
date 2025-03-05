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
NPROC=$(nproc)

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
  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
  make -j${NPROC} ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
  make -j${NPROC} ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all
  make -j${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
  make -j${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]; then
  echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
  sudo rm -rf ${OUTDIR}/rootfs
fi

echo "here 0"
# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p ${OUTDIR}/rootfs/usr/{bin,lib,sbin}
mkdir -p ${OUTDIR}/rootfs/var/log
echo "here 1"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
  git config --global core.compression 0
  git clone --depth 1 git://busybox.net/busybox.git
  cd busybox
  #git checkout ${BUSYBOX_VERSION}
  # TODO:  Configure busybox
  make distclean
  make defconfig
  sed -i 's/CONFIG_TC=y/CONFIG_TC=n/' .config
  sed -i 's|CONFIG_PREFIX="./_install"|CONFIG_PREFIX="../rootfs"|' .config
  make -j${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
  make -j${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
else
  cd busybox
  make distclean
  make defconfig
  sed -i 's/CONFIG_TC=y/CONFIG_TC=n/' .config
  sed -i 's|CONFIG_PREFIX="./_install"|CONFIG_PREFIX="../rootfs"|' .config
  echo "here 2"
  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
  echo "here 3"
  make -j${NPROC}  ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
fi

cd ${OUTDIR}/rootfs/
# TODO: Add library dependencies to rootfs
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
export ARM_SYSROOT=$(aarch64-none-linux-gnu-gcc -print-sysroot)
cp -r $ARM_SYSROOT/lib64/* lib64
cp -r $ARM_SYSROOT/lib/* lib
# TODO: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1
# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make -j${NPROC}  ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp "$FINDER_APP_DIR/writer" "$OUTDIR/rootfs/home"
cp "$FINDER_APP_DIR/finder.sh" "$OUTDIR/rootfs/home"
mkdir "$OUTDIR/rootfs/home/conf"
cp "$FINDER_APP_DIR/conf/username.txt" "$OUTDIR/rootfs/home/conf"
cp "$FINDER_APP_DIR/conf/assignment.txt" "$OUTDIR/rootfs/home/conf"
cp "$FINDER_APP_DIR/finder-test.sh" "$OUTDIR/rootfs/home"

cp "$FINDER_APP_DIR/autorun-qemu.sh" "$OUTDIR/rootfs/home"

# cp writer ${OUTDIR}/rootfs/home/
# cp *.sh ${OUTDIR}/rootfs/home/
# mkdir "$OUTDIR/rootfs/home/conf"

# cp "$FINDER_APP_DIR/conf/username.txt" "$OUTDIR/rootfs/home/conf"
# cp "$FINDER_APP_DIR/conf/assignment.txt" "$OUTDIR/rootfs/home/conf"
# TODO: Chown the  root directory
cd "$OUTDIR/rootfs"
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > "$OUTDIR/initramfs.cpio"

cd "$OUTDIR"
gzip -f initramfs.cpio

