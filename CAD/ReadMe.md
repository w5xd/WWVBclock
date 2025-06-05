### CAD models

The SolidEdge models are what eventually succeeded in modeling a 3D printable
enclosure for the clock.

The GatewayEnclosure.FCStd is not for the clock. The WWVBclock PCB happens
to be suitable for a high performance packet gateway based on the RFM69 chip.
Specifically, the Teensy has the ability to store and forward over 32KB of
messages, while the Uno (Atmega328 based) is only capable of 1KB. The
GatewayEnclosure is for a WWVBclock PCB populated with only an RFM69 and Teensy
and nothing else.
