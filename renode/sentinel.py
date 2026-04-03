# sentinel.py — Renode Python peripheral for openobsw lockstep sync
#
# Mapped at 0x01A0 (1 byte). When the OBSW writes any value to this
# address, the sentinel emits 0xFF on UART0 — the lockstep sync byte
# that signals to the OBCEmulatorAdapter that one control cycle is done.
#
# This peripheral is the bridge between the OBSW control loop and
# OpenSVF's SimulationMaster. The sync byte tells the adapter:
#   "The OBC has finished processing this step — advance simulation time."
#
# Renode Python peripheral API:
#   request.isRead    — True if CPU is reading from this address
#   request.isWrite   — True if CPU is writing to this address
#   request.value     — value written (on write)
#   self.uart         — not available directly; use sysbus

# Called once when the peripheral is initialised
def init():
    self.sync_count = 0

# Called on every CPU access to 0x01A0
def handler(request):
    if request.isWrite:
        self.sync_count += 1
        # Emit 0xFF sync byte on UART0
        # Renode exposes machine peripherals via the 'machine' global
        uart = machine["sysbus.uart0"]
        uart.WriteChar(0xFF)