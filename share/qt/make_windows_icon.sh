#!/bin/bash
# create multiresolution windows icon
#mainnet
ICON_SRC=../../src/qt/res/icons/fdreserve.png
ICON_DST=../../src/qt/res/icons/fdreserve.ico
convert ${ICON_SRC} -resize 16x16 fdreserve-16.png
convert ${ICON_SRC} -resize 32x32 fdreserve-32.png
convert ${ICON_SRC} -resize 48x48 fdreserve-48.png
convert fdreserve-16.png fdreserve-32.png fdreserve-48.png ${ICON_DST}
#testnet
ICON_SRC=../../src/qt/res/icons/fdreserve_testnet.png
ICON_DST=../../src/qt/res/icons/fdreserve_testnet.ico
convert ${ICON_SRC} -resize 16x16 fdreserve-16.png
convert ${ICON_SRC} -resize 32x32 fdreserve-32.png
convert ${ICON_SRC} -resize 48x48 fdreserve-48.png
convert fdreserve-16.png fdreserve-32.png fdreserve-48.png ${ICON_DST}
rm fdreserve-16.png fdreserve-32.png fdreserve-48.png
