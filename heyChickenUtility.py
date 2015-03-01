#!/usr/bin/python
# -*- coding: utf-8 -*-

import time
import signal
import sys
import threading
from threading import Thread, Timer, Lock
import socket
from socket import AF_INET, SOCK_DGRAM

# "constants"
ARDUINO_IPADDRESS = "10.0.1.6"
UDP_PORT = 9999
UDP_PACKET_SIZE = 48

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

# timing
UDP_MSG_TIMEOUT_SEC = 60
STATUS_POLLING_INTERVAL = 300.0		# 5 minutes
STATUS_RETRIES = 1
TUNING_RETRIES = 3
RETRY_DELAY_SEC = 120


# tuning
tuning_parameters = None
LIGHT_THRESHOLD = 1
PRESSURE_THRESHOLD = 2
TEMP_HEATER_ON = 3
TEMP_HEATER_OFF = 4
SMOOTHING_FACTOR = 5

# status
status_vars = None
TEMP_COOP = 0
TEMP_RUN = 1
LIGHT = 2
PRESSURE = 3
HEATER_STATUS = 4
DAY_NIGHT = 5
ROOST_STATUS = 6
TIMESTAMP = 7

# globals
timer = None
lets_shutdown = False
udp_lock = None
status_lock = None
data_gathering = True
tweeting = False


def signal_handler(signal, frame):
	print "\n******** Received SIGINT *************"
	shutdown(None)
	sys.exit(0)

def log_error(msg):
	if msg is not None:
		fileHandle = open('coopErrors', 'a')
		fileHandle.write("{}\t{}\n".format(time.ctime(), msg))
		fileHandle.close()
		print "\n{}".format(msg)
		
def send_datagram(msg, client_socket, retries):
	success = True
	bytes_sent = client_socket.sendto(msg, (ARDUINO_IPADDRESS, UDP_PORT))
	print "bytes_sent: ", bytes_sent
	if bytes_sent < len(msg):
		for i in range(retries):
			print "\n...retry {}...".format(i)
			time.sleep(RETRY_DELAY_SEC)
			bytes_sent = client_socket.sendto(msg, (ARDUINO_IPADDRESS, UDP_PORT))
			if bytes_sent:
				print "...success!..."
				break
	if bytes_sent < 1:
		log_error("UDP sendto failed in send_datagram, bytes_sent: {}".format(bytes_sent))
		success = False	
	return success
	
def receive_datagram(client_socket, retries):
	reply = None
	try:
		reply, server_address_info = client_socket.recvfrom(UDP_PACKET_SIZE)
	except socket.timeout:
		for i in range(retries):
			print "\n...retry {}...".format(i)
			time.sleep(RETRY_DELAY_SEC)
			try:
				reply, server_address_info = client_socket.recvfrom(UDP_PACKET_SIZE)
			except socket.timeout:
				reply = None		# redundant just in case
			if reply is not None:
				print "...success!..."
				break				
	if reply is None:
		log_error("UDP recvfrom failed in receive_datagram")
	return reply
	
def send_message(msg, client_socket, retries):
	global udp_lock
	reply = None
	with udp_lock:
		if send_datagram(msg, client_socket, retries):
			reply = receive_datagram(client_socket, retries)
	return reply		 

def receive_status(tokens):
	global status_lock, status_vars
	with status_lock:
		status_vars = tokens[1:-1]
		status_vars.append(time.ctime())
	if data_gathering:
		fileHandle = open('coopData', 'a')
		fileHandle.write("{} {}\n".format(int(time.time()), " ".join(tokens[1:-1])))
		fileHandle.close()
	
def request_status(client_socket):
	# print "current thread: ", threading.current_thread()
	# print "thread active count: ", threading.active_count()
	incoming_msg = send_message(MSG_REQ_STATUS, client_socket, STATUS_RETRIES)
	if incoming_msg is not None:
		tokens = incoming_msg.split()
		if tokens:
			receive_status(tokens)
	global lets_shutdown, timer	
	if not lets_shutdown:
		timer = Timer(STATUS_POLLING_INTERVAL, request_status, [client_socket,])
		timer.start()
	else:
		sys.exit(0)

def print_status(client_socket):
	if status_vars is not None:
		global status_lock
		with status_lock:
			print "\n\n", status_vars[TIMESTAMP]
			print "**************************************"
			print "Coop"
			print "{} °F\theat {}\troost {} ({})".format(status_vars[TEMP_COOP], "ON" if int(status_vars[HEATER_STATUS]) else "OFF", "ON" if int(status_vars[ROOST_STATUS]) else "OFF", status_vars[PRESSURE])
			print "\nRun"
			print "{} °F\t{} ({})".format(status_vars[TEMP_RUN], "day" if int(status_vars[DAY_NIGHT]) else "night", status_vars[LIGHT])
			print "**************************************\n"
	else:
		print "\nNo status retrieved from Arduino yet."

def print_parameters(parameters):
		print "Current tuning parameters:"
		print "1: Light [0-1000]\t{}".format(parameters[LIGHT_THRESHOLD])
		print "2: Pressure [0-1000]\t{}".format(parameters[PRESSURE_THRESHOLD])
		print "3: Heater ON [0-40]\t{}".format(parameters[TEMP_HEATER_ON])
		print "4: Heater OFF [0-70]\t{}".format(parameters[TEMP_HEATER_OFF])
		print "5: Smoothing [0-100]\t{}".format(parameters[SMOOTHING_FACTOR])
	
def tune_parameters(client_socket):
	print "\n...fetching current parameters...\n"
	incoming_msg = send_message(MSG_REQ_TUNING, client_socket, TUNING_RETRIES)
	if incoming_msg is not None:
		parameters = incoming_msg.split()
		print_parameters(parameters)
		parameterType = int(input('\nParameter to tune (1-5): '))
		parameterValue = input('Enter the new value: ')
		print "\n...transmitting...\n"	
		parameters[parameterType] = parameterValue;
		message = "%c %s %s %s %s %s " % (MSG_SET_TUNING, parameters[LIGHT_THRESHOLD], parameters[PRESSURE_THRESHOLD], parameters[TEMP_HEATER_ON], parameters[TEMP_HEATER_OFF], parameters[SMOOTHING_FACTOR])
		incoming_msg = send_message(message, client_socket, TUNING_RETRIES)
		if incoming_msg is not None:
			parameters = incoming_msg.split()
			print_parameters(parameters)
		else:
			log_error("Arduino not responding with parameters")	
	else:
		log_error("Arduino not responding with parameters")
		
def toggle_data_gathering(client_socket):
	global data_gathering
	data_gathering = not data_gathering	
	print "\n...data gathering now {action}...\n".format(action = "ON" if data_gathering else "OFF")
	
def toggle_tweeting(client_socket):
	global tweeting
	tweeting = not tweeting	
	print "\n...tweeting now {action}...\n".format(action = "ON" if tweeting else "OFF")
	
def shutdown(client_socket):
	print "\n\nShutting down..."
	global timer, lets_shutdown
	lets_shutdown = True
	timer.cancel()
	timer.join()
	client_socket.close()
	sys.exit(0)
	
menu_options = {1: print_status,
					 			2: tune_parameters,
					 			3: toggle_data_gathering,
					 			4: toggle_tweeting,
					 			5: shutdown}
	
def main_menu():
	global data_gathering, tweeting
	print "\nMain Menu:"
	print "1: Print status"
	print "2: Tune parameters"
	print "3: Turn {action} data gathering".format(action = "off" if data_gathering else "on")
	print "4: Turn {action} tweeting".format(action = "off" if tweeting else "on")
	print "5: Quit\n"
	
if __name__ == "__main__":

	client_socket = None
	
	log_error("*** Hey! Chicken! Utility! ***")
	
	signal.signal(signal.SIGINT, signal_handler)
	
	udp_lock = Lock()
	status_lock = Lock()
	
	# setup the socket
	client_socket = socket.socket(AF_INET, SOCK_DGRAM)
	client_socket.settimeout(UDP_MSG_TIMEOUT_SEC)
		
	# start a timer to begin request status loop
	if data_gathering:
		timer = Timer(3, request_status, [client_socket,])
		timer.start()
		
	while True:
		main_menu()
		option = int(input('Please make a selection: ') )
		menu_options.get(option, lambda: None)(client_socket)
	
		
		
		
		
		
		
		
		
		
		
