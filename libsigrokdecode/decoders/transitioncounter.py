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
    name = 'Transition counter'
    longname = '...'
    desc = 'Counts rising/falling edges in the signal.'
    longdesc = '...'
    author = 'Uwe Hermann'
    email = 'uwe@hermann-uwe.de'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['transitioncounts']
    probes = {}
    options = {}

    def __init__(self, unitsize, **kwargs):
        # Metadata comes in here, we don't care for now.
        # print kwargs
        self.unitsize = unitsize

        self.probes = Decoder.probes.copy()

        # TODO: Don't hardcode the number of channels.
        self.channels = 8

        self.lastsample = None
        self.oldbit = [0] * self.channels
        self.transitions = [0] * self.channels
        self.rising = [0] * self.channels
        self.falling = [0] * self.channels

    def report(self):
        pass

    def decode(self, data):
        """Counts the low->high and high->low transitions in the specified
           channel(s) of the signal."""

        # We should accept a list of samples and iterate...
        for sample in sampleiter(data["data"], self.unitsize):

            # TODO: Eliminate the need for ord().
            s = ord(sample.data)

            # Optimization: Skip identical samples (no transitions).
            if self.lastsample == s:
                continue

            # Upon the first sample, store the initial values.
            if self.lastsample == None:
                self.lastsample = s
                for i in range(self.channels):
                    self.oldbit[i] = (self.lastsample & (1 << i)) >> i

            # Iterate over all channels/probes in this sample.
            # Count rising and falling edges for each channel.
            for i in range(self.channels):
                curbit = (s & (1 << i)) >> i
                # Optimization: Skip identical bits (no transitions).
                if self.oldbit[i] == curbit:
                    continue
                elif (self.oldbit[i] == 0 and curbit == 1):
                    self.rising[i] += 1
                elif (self.oldbit[i] == 1 and curbit == 0):
                    self.falling[i] += 1
                self.oldbit[i] = curbit

            # Save the current sample as 'lastsample' for the next round.
            self.lastsample = s

        # Total number of transitions = rising + falling edges.
        for i in range(self.channels):
            self.transitions[i] = self.rising[i] + self.falling[i]

        # TODO: Which output format?
        # TODO: How to only output something after the last chunk of data?
        outdata = []
        for i in range(self.channels):
            outdata += [[self.transitions[i], self.rising[i], self.falling[i]]]
        sigrok.put(outdata)

# Use psyco (if available) as it results in huge performance improvements.
try:
    import psyco
    psyco.bind(decode)
except ImportError:
    pass

import sigrok

