#!/usr/bin/python

import SocketServer
import time
import signal
import sys
from Queue import Queue 
from threading import Thread, Timer

# "constants"
DATA_GATHERING_MODE = True
HEATER_STATUS_INDEX = 5

SERVER_IPADDRESS = "10.0.1.16"
UDP_PORT = 9999

MSG_ALIVE = 'A'
MSG_REQ_STATUS = 'R'
MSG_STATUS = 'S'
MSG_REQ_TUNING = 'Q'
MSG_TUNING = 'T'
MSG_SET_TUNING = 'P'
MSG_DOOR = 'D'
MSG_HEAT = 'H'
MSG_ERROR = 'E'
MSG_NO_OP = 'N'

SHUTDOWN_PLEASE = 'Z'

STATUS_POLLING_INTERVAL = 5.0
RECEIVE_MSG_TIMEOUT_SEC = 10

# wish these were an enum that were forced to be sequential
# then receiveTuning() would be less ugly
# indices
LIGHT_THRESHOLD = 1
PRESSURE_THRESHOLD = 2
TEMP_HEATER_ON = 3
TEMP_HEATER_OFF = 4
SMOOTHING_FACTOR = 5

# globals
arduinoSocket = None
arduinoAddress = None
server = None
timer = None
receive_queue = Queue()
job_queue = Queue()

# for reading and setting tuning values
# index 0 is doing nothing at the moment, makes accessing tokens easier since the message type is sitting in that spot
# packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
tuningParameters = [0, 0, 0, 0, 0, 0]


def receiveAlive(socket, clientAddress, tokens):
	print "Received alive message"
	
def receiveStatus(socket, clientAddress, tokens):
	if DATA_GATHERING_MODE:
		fileHandle = open('coopData', 'a')
		fileHandle.write("{} {}\n".format(int(time.time()), " ".join(tokens[1:-1])))
		fileHandle.close()
		global job_queue
		timer = Timer(STATUS_POLLING_INTERVAL, requestStatus, [job_queue,]) # reschedule
		timer.start()		
	
def receiveTuning(socket, clientAddress, tokens):
	global tuningParameters
	# this should be re-written as the indexes are sequential
	tuningParameters[LIGHT_THRESHOLD] = int(tokens[LIGHT_THRESHOLD])
	tuningParameters[PRESSURE_THRESHOLD] = int(tokens[PRESSURE_THRESHOLD])
	tuningParameters[TEMP_HEATER_ON] = int(tokens[TEMP_HEATER_ON])
	tuningParameters[TEMP_HEATER_OFF] = int(tokens[TEMP_HEATER_OFF])
	tuningParameters[SMOOTHING_FACTOR] = int(tokens[SMOOTHING_FACTOR])
		
def handleError(socket, clientAddress, tokens):
	print "Error:"
	print tokens
	
available_actions = {MSG_ALIVE: receiveAlive,
					 MSG_STATUS: receiveStatus,
					 MSG_TUNING: receiveTuning,
					 MSG_ERROR: handleError}
		
# def operateDoor():

# def operateHeat():

#def checkHeater(heaterStatus):

def processMessage(socket, clientAddress, data):
	tokens = data.split()
	if tokens:
		action = tokens[0]
		available_actions.get(action, lambda: None)(socket, clientAddress, tokens)
	else:
		print "Empty message"

# set the arduino socket and address
def setArduinoAddress(socket, address):
	global arduinoSocket
	global arduinoAddress
	arduinoSocket = socket
	arduinoAddress = address

# This handler puts received messages on the receive_queue
# and in the case of alive messages, records the arduino network address		
class MyUDPHandler(SocketServer.BaseRequestHandler):
	def handle(self):
		# print "MyUDPHandler called"
		data = self.request[0].strip()
		if data:
			if data.split()[0] is MSG_ALIVE:
				setArduinoAddress(self.request[1], self.client_address)
			global receive_queue
			receive_queue.put(data)
			receive_queue.task_done()
			
def sendUDPMessage(msg):
	# print "sendUDPMessage: " + msg
	global arduinoSocket
	global arduinoAddress
	if arduinoSocket is not None and arduinoAddress is not None:
		arduinoSocket.sendto(msg, arduinoAddress)
	else:
		print "Don't know the network address for the Arduino yet! Must receive an alive message first."

def processJobs(jobQueue, receiveQueue):
	print "Starting worker thread"
	# get job, send message, receive message, process message, job done
	while True:
		outgoingMsg = jobQueue.get()		# blocking
		# print "Worker thread: processing job: " + outgoingMsg
		if outgoingMsg is not MSG_ALIVE:
			sendUDPMessage(outgoingMsg)
		# else: we don't know the Arduino's address yet, wait for incoming msg
		try:
			incomingMsg = receiveQueue.get(True, RECEIVE_MSG_TIMEOUT_SEC)
			# print "Worker thread: processing incoming msg: " + incomingMsg
			global arduinoSocket
			global arduinoAddress
			processMessage(arduinoSocket, arduinoAddress, incomingMsg)
		except:
			# the arduino is unresponsive. Assume it needs to be rebooted
			print "The arduino is unresponsive. Waiting for it to be rebooted..."
			# start over fresh
			timer.cancel()
			while not job_queue.empty():
				job_queue.get()
			job_queue.put(MSG_ALIVE) 
		jobQueue.task_done()	# always signal done

def startServer(server):
	print "Starting UDP server"	   
	server.serve_forever()
	
def requestStatus(jobQueue):
	jobQueue.put(MSG_REQ_STATUS)
	
def signalHandler(signal, frame):
	print "\n******** Received SIGINT *************"
	receive_queue.join()
	job_queue.join()
	server.shutdown()
	sys.exit(0)
	
#  There are 3 threads in this script:
#  1) main thread to accept tuning requests from user
#  2) worker thread to send UDP messages and process responses
#  3) UDP server thread to receive messages

if __name__ == "__main__":

	signal.signal(signal.SIGINT, signalHandler)
	
	HOST, PORT = SERVER_IPADDRESS, UDP_PORT
	server = SocketServer.UDPServer((HOST, PORT), MyUDPHandler)
	
	job_queue.put(MSG_ALIVE)	# gotta get the arduino address before sending UDP msgs
	
	# start the UDP server thread
	serverThread = Thread(target=startServer, args=(server,))
	serverThread.setDaemon(True)
	serverThread.start()

	# start the worker thread to start processing jobs
	worker = Thread(target=processJobs, args=(job_queue, receive_queue))
	worker.setDaemon(True)
	worker.start()

	# start a timer to begin request status loop
	if DATA_GATHERING_MODE:
		timer = Timer(STATUS_POLLING_INTERVAL, requestStatus, [job_queue,])
		timer.start()

	# user parameter tuning
	# get current parameter values
		job_queue.put(MSG_REQ_TUNING)
		job_queue.join()			# make sure the tuning params are updated before continuing
	while True:
		print("*******************************************************")
		print("*				 Tunable parameters					 *")
		print("*******************************************************")
		print("\tCode\tParameter\tRange\t\tValue") 
		print("-------------------------------------------------------")
		print("\t%d\tLight\t\t[0-1000]\t%d" % (LIGHT_THRESHOLD, tuningParameters[LIGHT_THRESHOLD]))
		print("\t%d\tPressure\t[0-1000]\t%d" % (PRESSURE_THRESHOLD, tuningParameters[PRESSURE_THRESHOLD]))
		print("\t%d\tHeater On\t[0-40]\t\t%d" % (TEMP_HEATER_ON, tuningParameters[TEMP_HEATER_ON]))
		print("\t%d\tHeater Off\t[0-70]\t\t%d" % (TEMP_HEATER_OFF, tuningParameters[TEMP_HEATER_OFF]))
		print("\t%d\tSmoothing\t[0-100]\t\t%d" % (SMOOTHING_FACTOR, tuningParameters[SMOOTHING_FACTOR]))
		print("*******************************************************\n")
	
		parameterType = int(input('Enter the code for the parameter to tune: '))
		parameterValue = int(input('Enter the new value: ') )	
		tuningParameters[parameterType] = parameterValue	# no error checking
		print "Transmitting..."
	
		job_queue.put("%c %d %d %d %d %d " % (MSG_SET_TUNING, tuningParameters[LIGHT_THRESHOLD], tuningParameters[PRESSURE_THRESHOLD], tuningParameters[TEMP_HEATER_ON], tuningParameters[TEMP_HEATER_OFF], tuningParameters[SMOOTHING_FACTOR]))
		job_queue.join()			# make sure the tuning params are updated before continuing
		
