# Effective PWM/DShot driver inventory, not electrical feedback or output permission.
uint64 timestamp
uint64 driver_instance
uint32 hardware_mask
uint16 reversed_mask
uint8 bank # 0 MAIN, 1 AUX; any other bank is unsupported
uint8 channel_count
uint8 protocol # 0 PWM; 1 DShot, where rate_hz is the bit rate in kbit/s and min/max/disarmed are driver units
bool ready
uint16[16] function
uint16[16] rate_hz # 0 means unavailable/unsupported, including OneShot
uint16[16] minimum_us
uint16[16] center_us
uint16[16] maximum_us
uint16[16] disarmed_us
uint16[16] failsafe_us # 65535 means unresolved/default; never accepted as safe
