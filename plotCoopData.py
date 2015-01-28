#!/usr/bin/python

# Parse coop data and generate plot
# $ ./plotCoopData.py <local data file>

import numpy as np
import matplotlib.pyplot as plt
from matplotlib import dates
from StringIO import StringIO
import os
import sys
import datetime

timestampIndex = 0
insideTempIndex = 1
outsideTempIndex= 2
lightIndex = 3
pressureIndex = 4

resistorDivisor = 10

def squashValues(data):
	for x in np.nditer(data, op_flags=['readwrite']):
		x[...] = x / resistorDivisor
		
def cToF(data):
	for x in np.nditer(data, op_flags=['readwrite']):
		x[...] = (x * 9) / 5 + 32

fileName = str(sys.argv[1])

with open(fileName) as f:
    data = f.read()

dataArray = np.genfromtxt(StringIO(data), delimiter=" ")

xTS = dataArray[:,timestampIndex]
yInsideTemp = dataArray[:,insideTempIndex]
yOutsideTemp = dataArray[:,outsideTempIndex]
yLight = dataArray[:,lightIndex]
yPressure = dataArray[:,pressureIndex]

squashValues(yLight)
squashValues(yPressure)
cToF(yInsideTemp)
cToF(yOutsideTemp)

# convert epoch to matplotlib float format
dts = map(datetime.datetime.fromtimestamp, xTS)
fds = dates.date2num(dts) # converted

# matplotlib date format object
hfmt = dates.DateFormatter('%m/%d %H:00')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.set_title("Coop Status (" + fileName + ")")    
ax1.set_xlabel('time') 

ax1.plot(fds, yPressure, 'g.', label='pressure')
ax1.plot(fds, yLight, 'm.', label='light')
ax1.plot(fds, yInsideTemp, 'r-', label='coop temp (F)')
ax1.plot(fds, yOutsideTemp, 'b-', label='run temp (F)')

ax1.xaxis.set_major_locator(dates.HourLocator(byhour=range(0,24,2)))
ax1.xaxis.set_major_formatter(hfmt)

plt.xticks(rotation='vertical')
plt.subplots_adjust(bottom=.2)

leg = ax1.legend(loc=4)

#plt.xlim(0, 20000)
#plt.ylim(-10, 100)

plt.show()


				
