import SocketServer

SERVER_IPADDRESS = "10.0.1.15"
UDP_PORT = 9999

class MyUDPHandler(SocketServer.BaseRequestHandler):
    def handle(self):
        data = self.request[0].strip()
        socket = self.request[1]
        print "{} wrote:".format(self.client_address[0])
        print data
        socket.sendto(data.upper(), self.client_address)	# echo in caps

if __name__ == "__main__":
    HOST, PORT = SERVER_IPADDRESS, UDP_PORT
    server = SocketServer.UDPServer((HOST, PORT), MyUDPHandler)
    print "Starting UDP server"
    server.serve_forever()