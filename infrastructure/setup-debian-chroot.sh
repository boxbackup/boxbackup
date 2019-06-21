#!/bin/sh

set -ex

CHROOT_BASE=/var/chroot
LSB_RELEASE=$(lsb_release -s -c)

apt-get update
apt-get install -y debootstrap schroot

mkdir -p ${CHROOT_BASE}
debootstrap --arch=i386 --variant=buildd "${LSB_RELEASE}" "${CHROOT_BASE}/${LSB_RELEASE}-i386" http://deb.debian.org/debian
cat > /etc/schroot/chroot.d/ci <<EOF
[${LSB_RELEASE}]
type=directory
directory=${CHROOT_BASE}/${LSB_RELEASE}-i386
personality=linux32
EOF
