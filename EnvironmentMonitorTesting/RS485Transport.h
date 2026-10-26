#pragma once
#include <stdint.h>
#include <stddef.h>

namespace RS485 {
// setDirection(true) enables the driver and disables the receiver; it must
// include the board's enable settling time. false restores receive mode.
template<class Port,class SetDirection>
void transmit(Port &port,const uint8_t *bytes,size_t length,bool automatic,SetDirection setDirection) {
  if(!automatic)setDirection(true);
  port.write(bytes,length);
  if(automatic) {
    // The receiver can already be active: preserve even an immediate reply.
    port.flush(true);
  } else {
    // Arduino-ESP32 flush(false) waits until TX is physically idle, then
    // clears the RX FIFO and buffer. RO is disabled here, so bytes collected
    // during our transmission cannot be a legitimate sensor response.
    port.flush(false);
    // Never clear RX after this point: a sensor may respond immediately.
    setDirection(false);
  }
}
}
