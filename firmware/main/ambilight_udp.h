#pragma once

// Wi-Fi UDP ambilight receiver. Listens for WLED realtime packets (DRGB /
// DNRGB) on UDP port 21324 and drives the LED zones in real time via the
// output controller. A desktop tool such as HyperHDR (configured as a "WLED"
// device) sends screen-edge colours here.
//
// Call once after the Matter stack / Wi-Fi has been started.
void ambilight_udp_init(void);
