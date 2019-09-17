#!/bin/sh

cd /tmp

git clone https://github.com/hwangsaeul/gaeguli.git
cd gaeguli
dpkg-buildpackage -us -uc
cd ..
cp *.buildinfo *.deb *.tar.* *.changes *.dsc *.ddeb /mnt
