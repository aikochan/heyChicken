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
HEATER_STATUS_INDEX = 5

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

STATUS_POLLING_INTERVAL = 15.0
UDP_MSG_TIMEOUT_SEC = 20

# wish these were an enum that were forced to be sequential
# then receiveTuning() would be less ugly
# indices for thresholds
LIGHT_THRESHOLD = 1
PRESSURE_THRESHOLD = 2
TEMP_HEATER_ON = 3
TEMP_HEATER_OFF = 4
SMOOTHING_FACTOR = 5

# globals
timer = None
lets_shutdown = False
udp_lock = None
status_lock = None

# indices for status vars
TEMP_COOP = 0
TEMP_RUN = 1
LIGHT = 2
PRESSURE = 3
HEATER_STATUS = 4

# shared global status variables
status_vars = None

# for reading and setting tuning values
# index 0 is doing nothing at the moment, makes accessing tokens easier since the message type is sitting in that spot
# packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
tuning_parameters = [0, 0, 0, 0, 0, 0]

def receive_status(tokens):
	global status_lock, status_vars
	with status_lock:
		status_vars = tokens[1:-1]
	if data_gathering:
		fileHandle = open('coopData', 'a')
		fileHandle.write("{} {}\n".format(int(time.time()), " ".join(tokens[1:-1])))
		fileHandle.close()
	
def receive_tuning(tokens):
	global tuning_parameters
	# this should be re-written as the indexes are sequential
	tuning_parameters[LIGHT_THRESHOLD] = int(tokens[LIGHT_THRESHOLD])
	tuning_parameters[PRESSURE_THRESHOLD] = int(tokens[PRESSURE_THRESHOLD])
	tuning_parameters[TEMP_HEATER_ON] = int(tokens[TEMP_HEATER_ON])
	tuning_parameters[TEMP_HEATER_OFF] = int(tokens[TEMP_HEATER_OFF])
	tuning_parameters[SMOOTHING_FACTOR] = int(tokens[SMOOTHING_FACTOR])
		
def handle_error(tokens):
	print "Error:"
	print tokens
	
available_actions = {MSG_STATUS: receive_status,
					 MSG_TUNING: receive_tuning,
					 MSG_ERROR: handle_error}
		
# def operate_door():

# def operate_heat():

#def check_heater(heaterStatus):

def process_message(data):
	tokens = data.split()
	if tokens:
		action = tokens[0]
		available_actions.get(action, lambda: None)(tokens)
	else:
		print "Empty message"
	
def send_udp_message(msg, client_socket):
	global udp_lock
	reply = None
	with udp_lock:
		bytes_sent = client_socket.sendto(msg, (ARDUINO_IPADDRESS, UDP_PORT))
		if bytes_sent:
			try:
				reply, server_address_info = client_socket.recvfrom(1024)
			except socket.timeout:
				print "Error: UDP receive timeout in send_udp_message"
		else:
			print "Error: UDP sendto timeout in send_udp_message"
	return reply		 

# does not acquire lock	
def request_status(client_socket):
	# print "current thread: ", threading.current_thread()
	# print "thread active count: ", threading.active_count()
	incoming_msg = send_udp_message(MSG_REQ_STATUS, client_socket)
	if incoming_msg is not None:
		process_message(incoming_msg)
	global lets_shutdown, timer	
	if not lets_shutdown:
		timer = Timer(STATUS_POLLING_INTERVAL, request_status, [client_socket,])
		timer.start()
	else:
		sys.exit(0)

def signal_handler(signal, frame):
	print "\n******** Received SIGINT *************"
	sys.exit(0)

def print_status(client_socket):
	if status_vars is not None:
		global status_lock
		with status_lock:
			print "\n\n**************************************"
			print "Coop"
			print "{} °F\theat {}\troost ({})".format(status_vars[TEMP_COOP], status_vars[HEATER_STATUS], status_vars[PRESSURE])
			print "\nRun"
			print "{} °F\tlight ({})".format(status_vars[TEMP_RUN], status_vars[LIGHT])
			print "**************************************\n"
	else:
		print "No status retrieved from Arduino yet."

def tune_parameters(client_socket):
	print "tune_parameters"
	
def toggle_data_gathering(client_socket):
	print "toggle_data_gathering"
	
def toggle_tweeting(client_socket):
	print "toggle_tweeting"

def shutdown(client_socket):
	print "\n\nShutting down..."
	global timer, lets_shutdown
	lets_shutdown = True
	timer.cancel()
	timer.join()
	sys.exit(0)
	
menu_options = {1: print_status,
					 			2: tune_parameters,
					 			3: toggle_data_gathering,
					 			4: toggle_tweeting,
					 			5: shutdown}
	
def main_menu():
	print "\nMain Menu:"
	print "1: Print status"
	print "2: Tune parameters"
	print "3: Turn {action} data gathering".format(action = "off" if data_gathering else "on")
	print "4: Turn {action} tweeting".format(action = "off" if tweeting else "on")
	print "5: Quit\n"
	
if __name__ == "__main__":

	client_socket = None
	data_gathering = True
	tweeting = False
	
	
	print "\n*** Hey! Chicken! Utility! ***"

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
	
		
		
		
		
		
		
		
		
		
		
