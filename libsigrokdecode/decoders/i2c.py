##
## This file is part of the sigrok project.
##
## Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
##

#
# I2C protocol decoder
#

#
# The Inter-Integrated Circuit (I2C) bus is a bidirectional, multi-master
# bus using two signals (SCL = serial clock line, SDA = serial data line).
#
# There can be many devices on the same bus. Each device can potentially be
# master or slave (and that can change during runtime). Both slave and master
# can potentially play the transmitter or receiver role (this can also
# change at runtime).
#
# Possible maximum data rates:
#  - Standard mode: 100 kbit/s
#  - Fast mode: 400 kbit/s
#  - Fast-mode Plus: 1 Mbit/s
#  - High-speed mode: 3.4 Mbit/s
#
# START condition (S): SDA = falling, SCL = high
# Repeated START condition (Sr): same as S
# STOP condition (P): SDA = rising, SCL = high
#
# All data bytes on SDA are exactly 8 bits long (transmitted MSB-first).
# Each byte has to be followed by a 9th ACK/NACK bit. If that bit is low,
# that indicates an ACK, if it's high that indicates a NACK.
#
# After the first START condition, a master sends the device address of the
# slave it wants to talk to. Slave addresses are 7 bits long (MSB-first).
# After those 7 bits, a data direction bit is sent. If the bit is low that
# indicates a WRITE operation, if it's high that indicates a READ operation.
#
# Later an optional 10bit slave addressing scheme was added.
#
# Documentation:
# http://www.nxp.com/acrobat/literature/9398/39340011.pdf (v2.1 spec)
# http://www.nxp.com/acrobat/usermanuals/UM10204_3.pdf (v3 spec)
# http://en.wikipedia.org/wiki/I2C
#

# TODO: Look into arbitration, collision detection, clock synchronisation, etc.
# TODO: Handle clock stretching.
# TODO: Handle combined messages / repeated START.
# TODO: Implement support for 7bit and 10bit slave addresses.
# TODO: Implement support for inverting SDA/SCL levels (0->1 and 1->0).
# TODO: Implement support for detecting various bus errors.

#
# I2C output format:
#
# The output consists of a (Python) list of I2C "packets", each of which
# has an (implicit) index number (its index in the list).
# Each packet consists of a Python dict with certain key/value pairs.
#
# TODO: Make this a list later instead of a dict?
#
# 'type': (string)
#   - 'S' (START condition)
#   - 'Sr' (Repeated START)
#   - 'AR' (Address, read)
#   - 'AW' (Address, write)
#   - 'DR' (Data, read)
#   - 'DW' (Data, write)
#   - 'P' (STOP condition)
# 'range': (tuple of 2 integers, the min/max samplenumber of this range)
#   - (min, max)
#   - min/max can also be identical.
# 'data': (actual data as integer ???) TODO: This can be very variable...
# 'ann': (string; additional annotations / comments)
#
# Example output:
# [{'type': 'S',  'range': (150, 160), 'data': None, 'ann': 'Foobar'},
#  {'type': 'AW', 'range': (200, 300), 'data': 0x50, 'ann': 'Slave 4'},
#  {'type': 'DW', 'range': (310, 370), 'data': 0x00, 'ann': 'Init cmd'},
#  {'type': 'AR', 'range': (500, 560), 'data': 0x50, 'ann': 'Get stat'},
#  {'type': 'DR', 'range': (580, 640), 'data': 0xfe, 'ann': 'OK'},
#  {'type': 'P',  'range': (650, 660), 'data': None, 'ann': None}]
#
# Possible other events:
#   - Error event in case protocol looks broken:
#     [{'type': 'ERROR', 'range': (min, max),
#      'data': TODO, 'ann': 'This is not a Microchip 24XX64 EEPROM'},
#     [{'type': 'ERROR', 'range': (min, max),
#      'data': TODO, 'ann': 'TODO'},
#   - TODO: Make list of possible errors accessible as metadata?
#
# TODO: I2C address of slaves.
# TODO: Handle multiple different I2C devices on same bus
#       -> we need to decode multiple protocols at the same time.
# TODO: range: Always contiguous? Splitted ranges? Multiple per event?
#

#
# I2C input format:
#
# signals:
# [[id, channel, description], ...] # TODO
#
# Example:
# {'id': 'SCL', 'ch': 5, 'desc': 'Serial clock line'}
# {'id': 'SDA', 'ch': 7, 'desc': 'Serial data line'}
# ...
#
# {'inbuf': [...],
#  'signals': [{'SCL': }]}
#

class Sample():
    def __init__(self, data):
        self.data = data
    def probe(self, probe):
        s = ord(self.data[probe / 8]) & (1 << (probe % 8))
        return True if s else False

def sampleiter(data, unitsize):
    for i in range(0, len(data), unitsize):
        yield(Sample(data[i:i+unitsize]))

class Decoder():
    name = 'I2C'
    longname = 'Inter-Integrated Circuit (I2C) bus'
    desc = 'I2C is a two-wire, multi-master, serial bus.'
    longdesc = '...'
    author = 'Uwe Hermann'
    email = 'uwe@hermann-uwe.de'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['i2c']
    probes = {
        'scl': {'ch': 0, 'name': 'SCL', 'desc': 'Serial clock line'},
        'sda': {'ch': 1, 'name': 'SDA', 'desc': 'Serial data line'},
    }
    options = {
        'address-space': ['Address space (in bits)', 7],
    }

    def __init__(self, unitsize, **kwargs):
        # Metadata comes in here, we don't care for now.
        # print kwargs
        self.unitsize = unitsize

        self.probes = Decoder.probes.copy()

        # TODO: Don't hardcode the number of channels.
        self.channels = 8

        self.samplenum = 0

        self.bitcount = 0
        self.databyte = 0
        self.wr = -1
        self.startsample = -1
        self.IDLE, self.START, self.ADDRESS, self.DATA = range(4)
        self.state = self.IDLE

        # Get the channel/probe number of the SCL/SDA signals.
        self.scl_bit = self.probes['scl']['ch']
        self.sda_bit = self.probes['sda']['ch']

        self.oldscl = None
        self.oldsda = None

    def report(self):
        pass

    def decode(self, data):
        """I2C protocol decoder"""

        out = []
        o = ack = d = ''

        # We should accept a list of samples and iterate...
        for sample in sampleiter(data["data"], self.unitsize):

            # TODO: Eliminate the need for ord().
            s = ord(sample.data)

            # TODO: Start counting at 0 or 1?
            self.samplenum += 1

            # First sample: Save SCL/SDA value.
            if self.oldscl == None:
                # Get SCL/SDA bit values (0/1 for low/high) of the first sample.
                self.oldscl = (s & (1 << self.scl_bit)) >> self.scl_bit
                self.oldsda = (s & (1 << self.sda_bit)) >> self.sda_bit
                continue

            # Get SCL/SDA bit values (0/1 for low/high).
            scl = (s & (1 << self.scl_bit)) >> self.scl_bit
            sda = (s & (1 << self.sda_bit)) >> self.sda_bit

            # TODO: Wait until the bus is idle (SDA = SCL = 1) first?

            # START condition (S): SDA = falling, SCL = high
            if (self.oldsda == 1 and sda == 0) and scl == 1:
                o = {'type': 'S', 'range': (self.samplenum, self.samplenum),
                     'data': None, 'ann': None},
                out.append(o)
                self.state = self.ADDRESS
                self.bitcount = self.databyte = 0

            # Data latching by transmitter: SCL = low
            elif (scl == 0):
                pass # TODO

            # Data sampling of receiver: SCL = rising
            elif (self.oldscl == 0 and scl == 1):
                if self.startsample == -1:
                    self.startsample = self.samplenum
                self.bitcount += 1

                # out.append("%d\t\tRECEIVED BIT %d:  %d\n" % \
                #     (self.samplenum, 8 - bitcount, sda))

                # Address and data are transmitted MSB-first.
                self.databyte <<= 1
                self.databyte |= sda

                if self.bitcount != 9:
                    continue

                # We received 8 address/data bits and the ACK/NACK bit.
                self.databyte >>= 1 # Shift out unwanted ACK/NACK bit here.
                ack = (sda == 1) and 'N' or 'A'
                d = (self.state == self.ADDRESS) and (self.databyte & 0xfe) or self.databyte
                if self.state == self.ADDRESS:
                    self.wr = (self.databyte & 1) and 1 or 0
                    self.state = self.DATA
                o = {'type': self.state,
                     'range': (self.startsample, self.samplenum - 1),
                     'data': d, 'ann': None}
                if self.state == self.ADDRESS and self.wr == 1:
                    o['type'] = 'AW'
                elif self.state == self.ADDRESS and self.wr == 0:
                    o['type'] = 'AR'
                elif self.state == self.DATA and self.wr == 1:
                    o['type'] = 'DW'
                elif self.state == self.DATA and self.wr == 0:
                    o['type'] = 'DR'
                out.append(o)
                o = {'type': ack, 'range': (self.samplenum, self.samplenum),
                     'data': None, 'ann': None}
                out.append(o)
                self.bitcount = self.databyte = self.startsample = 0
                self.startsample = -1

            # STOP condition (P): SDA = rising, SCL = high
            elif (self.oldsda == 0 and sda == 1) and scl == 1:
                o = {'type': 'P', 'range': (self.samplenum, self.samplenum),
                     'data': None, 'ann': None},
                out.append(o)
                self.state = self.IDLE
                self.wr = -1

            # Save current SDA/SCL values for the next round.
            self.oldscl = scl
            self.oldsda = sda

        # TODO: Which output format?
        # TODO: How to only output something after the last chunk of data?
        if out != []:
            sigrok.put(out)

# Use psyco (if available) as it results in huge performance improvements.
try:
    import psyco
    psyco.bind(decode)
except ImportError:
    pass

import sigrok

