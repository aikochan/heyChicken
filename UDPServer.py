#!/usr/bin/python

import SocketServer
import time

dataGatheringMode = True

SERVER_IPADDRESS = "10.0.1.10"
UDP_PORT = 9999
	
def receiveAlive(socket, clientAddress, tokens):
	print "Requesting status message\n"
	if (dataGatheringMode):
		socket.sendto("R", clientAddress)   # request status upon arduino starting up 

def receiveStatus(socket, clientAddress, tokens):
	print "Receiving status message\n"
	print "Here's the status:"
	print tokens 
	fileHandle = open('coopData', 'a')
	fileHandle.write("{} {}\n".format(int(time.time()), " ".join(tokens[1:-1])))
	fileHandle.close()	
	
def handleError(socket, clientAddress, tokens):
	print "Error:"
	print tokens
	
available_actions = {"A": receiveAlive,
                     "S": receiveStatus,
                     "E": handleError}
		
# def operateDoor():

# def operateHeat():

def processMessage(socket, clientAddress, data):
	tokens = data.split()
	if tokens:
		action = tokens[0]
		available_actions.get(action, lambda: None)(socket, clientAddress, tokens)
	else:
		print "Empty message"
	
class MyUDPHandler(SocketServer.BaseRequestHandler):
    def handle(self):
        data = self.request[0].strip()
        socket = self.request[1]
        print "{} wrote:".format(self.client_address[0])
        # socket.sendto(data.upper(), self.client_address)	# echo in caps
        if data:
        	processMessage(socket, self.client_address, data)

if __name__ == "__main__":
    HOST, PORT = SERVER_IPADDRESS, UDP_PORT
    server = SocketServer.UDPServer((HOST, PORT), MyUDPHandler)
    print "Starting UDP server"    
    server.serve_forever()