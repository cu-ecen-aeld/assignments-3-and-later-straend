#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
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

    # Add your kernel build steps here
    # Patch double yylc
    sed -i '/^YYLTYPE yylloc;/d' scripts/dtc/dtc-lexer.l
    
    # Cleaning build tree
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} mrproper
    
    # Default configurations
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # copy manullay made .config
    #cp ${FINDER_APP_DIR}/kernel_${KERNEL_VERSION}.config .config

    # Disable some keys
    scripts/config --disable SYSTEM_TRUSTED_KEYS
    scripts/config --disable SYSTEM_REVOCATION_KEYS
    scripts/config --set-str SYSTEM_TRUSTED_KEYS ""

    # Actually build
    make -j8 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} all
    
    # Build modules
    #make -j8 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} modules
    
    # Device tree build
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} dtbs
    
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
ROOTFS="${OUTDIR}/rootfs"
mkdir -p ${ROOTFS}
cd ${ROOTFS}
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} distclean
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi

# Make and install busybox
make -j8 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE}
make -j8 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${ROOTFS} install

cd ${ROOTFS}

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
SOURCEDIR=$(which ${CROSS_COMPILE}gcc)
SOURCEDIR=$(dirname ${SOURCEDIR})

# @TODO: make this more dynamic instead of hardcoded
#        ie parse output from the library dependencies ^ 
cd "${SOURCEDIR}/.."
cp $(find . -name ld-linux-aarch64.so.1) ${OUTDIR}/rootfs/lib/
cp $(find . -name libm.so.6) ${OUTDIR}/rootfs/lib64/
cp $(find . -name libresolv.so.2) ${OUTDIR}/rootfs/lib64/
cp $(find . -name libc.so.6) ${OUTDIR}/rootfs/lib64/

# Make device nodes
cd "$ROOTFS"
sudo mknod dev/null c 1 3
sudo mknod dev/console c 5 1


# Clean and build the writer utility
cd "$FINDER_APP_DIR"
make CROSS_COMPILE="${CROSS_COMPILE}" clean
make CROSS_COMPILE="${CROSS_COMPILE}" writer
# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp finder-test.sh finder.sh writer.sh writer ${OUTDIR}/rootfs/home
cp -r ../conf ${OUTDIR}/rootfs
cp -r conf ${OUTDIR}/rootfs/home

cp autorun-qemu.sh ${OUTDIR}/rootfs/home
# Chown the root directory
# Seems like a double duty, as the cpio command later also chowns all files to root
#sudo chown -R root:root ${ROOTFS}

# Create initramfs.cpio.gz
cd ${ROOTFS}
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd $OUTDIR
gzip -f initramfs.cpio