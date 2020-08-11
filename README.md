# Lattice UC120 USB-C PD PHY driver

This is the clean-room implementation of Lattice UC120, a long-abandoned USB-C PD PHY chip that exclusively used on Lumia 950 and 950 XL. 
It is actually a specialized iCE40 FPGA programmed with a USB-C PD PHY logic bitstream.

Lattice stopped supporting this chip long time ago, and there is few documentation available on the Internet. They even removed references
to UC120 in their website.

## Status

This driver only serves as the PHY portion and needs to be used with upper USB-C controller drivers. Currently it can act as a "drop-in" replacement 
for the stock `ice5lp_2k.sys` driver in Windows Phone firmware except for manufacturing calibration features.