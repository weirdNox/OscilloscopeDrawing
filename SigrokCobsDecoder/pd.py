import sigrokdecode as srd

class Decoder(srd.Decoder):
    api_version = 3
    id = 'cobs_modified'
    name = 'COBS (modified)'
    longname = 'Consistent Overhead Byte Stuffing (modified)'
    desc = 'Packet framing.'
    license = 'gplv2+'
    inputs = ['uart']
    outputs = ['cobs_modified']
    annotations = (
        ('packet-start', 'Packet start'),
        ('packet', 'Packet'),
    )
    annotation_rows = (
        ('decoded-packets', 'Decoded packets', (0, 1)),
    )
    def __init__(self):
        self.skipPacket = True
        self.code = 0xFF
        self.copy = 0
        self.nextPair = (0, 0)

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def nextPacket(self, byte, ss, es):
        if not byte:
            self.skipPacket = False
            self.code = 0xFF
            self.copy = 0
            self.put(ss, es, self.out_ann, [0, ['Start', 'St', 'S']])

    def decode(self, ss, es, data):
        (ptype, rxtx, pdata) = data
        if ptype == 'DATA':
            byte = pdata[0]

            if self.skipPacket or (not byte):
                self.nextPacket(byte, ss, es)
                return

            if self.copy == 0:
                self.copy = self.code = byte
            else:
                self.put(self.nextPair[0], self.nextPair[1], self.out_ann, [1, ['0x{:02X}'.format(byte)]])

            self.nextPair = (ss, es)

            self.copy -= 1
            if self.copy == 0 and self.code != 0xFF:
                self.put(self.nextPair[0], self.nextPair[1], self.out_ann, [1, ['0x00']])
