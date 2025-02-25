#!/bin/bash
# Script outline to install and build kernel.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-


if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

#define own path variables
root_dir="${OUTDIR}/rootfs"

if [ ! -d $OUTDIR ]
then
    mkdir -p $OUTDIR

fi

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


    echo "apply patch"
    curl https://github.com/torvalds/linux/commit/e33a814e772cdc36436c8c188d8c42d019fda639.patch > /tmp/lin.patch
    git apply /tmp/lin.patch

    # TODO: Add your kernel build steps here

    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all -j 6
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/arm64/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

cd ${root_dir}

mkdir -p bin dev etc home lib lib64 proc sbin  sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin 
mkdir -p var/log


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

    make distclean
    make defconfig
else
    cd busybox
fi


# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j 4

make CONFIG_PREFIX=${root_dir} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
cd ${root_dir}

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"


# TODO: Add library dependencies to rootfs

#copy shared libraries from the aarch64 toolchain
gcc-path=$(which aarch64-none-linux-gnu-gcc)
lib-path = gcc-path/../../libc

cp -r lib64/* ${root_dir}/lib64
cp -r lib/* ${root_dir}/lib


# TODO: Make device nodes
 
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1 

sudo mknod -m 600 dev/tty c 5 0  #handle tty not found message



# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp writer ${root_dir}/home/
cp finder.sh ${root_dir}/home/
cp finder-test.sh ${root_dir}/home/
cp -r conf/ ${root_dir}/home/
cp autorun-qemu.sh ${root_dir}/home/



# TODO: Chown the root directory
cd ${root_dir}
sudo chown -R root:root *

sudo chmod -R a+rwx * #allow the deletion of rootfs through non-root users

# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio