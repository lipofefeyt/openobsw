import sys, struct

# Build a minimal PUS-C TC space packet: S17/1 are-you-alive on APID 0x010
# Primary header (6 bytes)
apid     = 0x010
seq      = 0xC001   # seq_flags=0b11 (unsegmented) | seq_count=1
data_len = 3        # 4 bytes of payload - 1

hdr = struct.pack('>HHH',
    (0 << 13) | (1 << 12) | (1 << 11) | apid,  # version=0, type=TC, sec_hdr=1
    seq,
    data_len)

# PUS-C secondary header (4 bytes): ver+ack, svc, subsvc, src_id_hi
payload = bytes([0x11, 17, 1, 0x00])

frame = hdr + payload

# Length-prefix and send
sys.stdout.buffer.write(struct.pack('>H', len(frame)))
sys.stdout.buffer.write(frame)
sys.stdout.buffer.flush()