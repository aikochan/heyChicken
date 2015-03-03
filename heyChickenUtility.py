#!/usr/bin/python
# -*- coding: utf-8 -*-

import time
import signal
import sys
import threading
from threading import Thread, Timer, Lock
import socket
from socket import AF_INET, SOCK_DGRAM
import tweepy
import twitterKeys	# defines CONSUMER_KEY, CONSUMER_SECRET, ACCESS_KEY, and ACCESS_SECRET

# "constants"
ARDUINO_IPADDRESS = "10.0.1.11"
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
STATUS_POLLING_INTERVAL = 180.0		# 3 minutes
STATUS_RETRIES = 3
TUNING_RETRIES = 3
RETRY_DELAY_SEC = 30
MAX_UDP_FAILURES = 5

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

# messages
ARDUINO_DOWN_MSG = "Better check my coop. Something's up with the electronics."
HEATER_ON_MSG = "Turning on the heat. My comb's getting frosty."
HEATER_OFF_MSG = "We are all toasty in here. Cut the heat please."
SUNS_UP_MSG = "Rise and shine! Time to get up."
NIGHT_MSG = "Goodnight! Time to hit the sack, er, roost bar."

# globals
timer = None
lets_shutdown = False
udp_lock = None
status_lock = None
data_gathering = True
tweeting = False
tweepy_api = None
udp_failures = 0

def signal_handler(signal, frame):
	print "\n******** Received SIGINT *************"
	shutdown(None)
	sys.exit(0)

def log_error(msg):
	if msg is not None:
		fileHandle = open('coopErrors', 'a')
		fileHandle.write("{}\t{}\n".format(time.ctime(), msg))
		fileHandle.close()
		# print "\n{}".format(msg)

def send_notification(msg):
	global tweepy_api
	if tweepy_api is  None:
		auth = tweepy.OAuthHandler(twitterKeys.CONSUMER_KEY, twitterKeys.CONSUMER_SECRET)
		auth.set_access_token(twitterKeys.ACCESS_KEY, twitterKeys.ACCESS_SECRET)
		tweepy_api = tweepy.API(auth)
	tweepy_api.update_status(status=msg)

# returns true on success, otherwise false		
def send_datagram(msg, client_socket):
	success = True
	bytes_sent = 0
	try:
		bytes_sent = client_socket.sendto(msg, (ARDUINO_IPADDRESS, UDP_PORT))
	except socket.error, err:
		log_error("UDP sendto failed in send_datagram, I/O error({})".format(err))
		success = False	
	else:
		if bytes_sent < len(msg):
			log_error("UDP sendto failed in send_datagram, bytes_sent: {}".format(bytes_sent))
			success = False	
	return success

# returns reply on success, None on failure
def receive_datagram(client_socket):
	reply = None
	try:
		reply, server_address_info = client_socket.recvfrom(UDP_PACKET_SIZE)
	except socket.error, err:
		log_error("UDP recvfrom failed in receive_datagram, I/O error({})".format(err))
		reply = None		# redundant just in case
	return reply
	
def send_message(msg, client_socket, retries):
	global udp_lock
	reply = None
	send_result = False
	with udp_lock:
		send_result = send_datagram(msg, client_socket)
		if  send_result:
			reply = receive_datagram(client_socket)
		while retries and (not send_result or reply is None):
			print "\n...send_message retry countdown {}...".format(retries)
			time.sleep(RETRY_DELAY_SEC)
			send_result = send_datagram(msg, client_socket)
			if  send_result:
				reply = receive_datagram(client_socket)
			retries -= 1
	if reply is None:
		log_error("send_message failed after retries!")
		udp_failures += 1
		if udp_failures > MAX_UDP_FAILURES:
			send_notification(ARDUINO_DOWN_MSG)
			udp_failures = 0
	return reply	
	
def checkChange(previousStatus, index, on_msg, off_msg):
	global status_lock, status_vars
	notification = None
	with status_lock:
		if previousStatus[index] is not status_vars[index]:
			notification = (on_msg if int(status_vars[index]) else off_msg)
			if index is HEATER_STATUS:
				notification = notification + " It's {}° in here.".format(status_vars[TEMP_COOP])
	if notification is not None:
		send_notification(notification)

def receive_status(tokens):
	global status_lock, status_vars
	notification = None
	previousStatus = None
	with status_lock:
		previousStatus = status_vars
		status_vars = tokens[1:-1]
		status_vars.append(time.ctime())
	# check if notifications needed
	if previousStatus is not None:
		checkChange(previousStatus, HEATER_STATUS, HEATER_ON_MSG, HEATER_OFF_MSG)
		checkChange(previousStatus, DAY_NIGHT, SUNS_UP_MSG, NIGHT_MSG)
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
	
	#send_notification("Testing!")
		
	# start a timer to begin request status loop
	if data_gathering:
		timer = Timer(3, request_status, [client_socket,])
		timer.start()
		
	while True:
		main_menu()
		option = int(input('Please make a selection: ') )
		menu_options.get(option, lambda: None)(client_socket)
	
		
		
		
		
		
		
		
		
		
		
