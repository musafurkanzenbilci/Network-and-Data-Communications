import socket
import select
import sys
import threading
import json
import time
class Node:
    def __init__(self, port):
        self.port = int(port)
        self.distance_vector_list = {}
        self.immediate_neighbours = []
        self.totalNodeNumber = 0

    def readInitialVectors(self, input_file_name):
        f = open(input_file_name, "r")
        Lines = f.readlines()

        nodeNumber = int(Lines[0])
        self.totalNodeNumber = nodeNumber
        counter = 3000

        for i in range(1, len(Lines)):
            lis = Lines[i].split(" ")
            for l in range(len(lis)):
                lis[l] = lis[l].strip()
            portNo = int(lis[0])
            dist = int(lis[1])
            while portNo != counter and counter - 3000 < nodeNumber:
                if counter == self.port:
                    counter += 1
                else:
                    self.distance_vector_list[counter] = float("inf")
                    counter += 1

            self.distance_vector_list[portNo] = dist  # immediate neighbours
            self.immediate_neighbours.append(int(portNo))
            counter += 1



    def printVectors(self):
        res = ""
        #print(str(self.port) + " - "+str(self.port) +" | 0")
        res += str(self.port) + " - "+str(self.port) +" | 0\n"
        for key in self.distance_vector_list:
            res += str(self.port) + " - "+str(key) +" | "+ str(self.distance_vector_list[key]) + "\n"
            #print(str(self.port) + " - "+str(key) +" | "+ str(self.distance_vector_list[key]))
        time.sleep((self.port - 3000)/5)
        print(res)


    def closeNode(self):
        for neighbour in self.immediate_neighbours:
            if neighbour is tuple:
                neighbour[1].close()

    def advertise(self):
        # Create a socket object

        for neighbour in self.immediate_neighbours:
            time.sleep(0.05)
            # Connect to a server at a given address and port
            if neighbour is not tuple:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                neighbour = (neighbour, s)
            else:
                s = neighbour[1]
            host = socket.gethostbyname("localhost")

            neighbourPort = neighbour[0]
            s.settimeout(1)
            try:
                s.connect(("", int(neighbourPort)))
            except socket.error as err:
                continue
            message = self.distance_vector_list
            data_string = json.dumps(message)
            # Send a packet to the server
            try:
                s.sendall(data_string)
            except socket.error as serr:
                pass

    def check_inactivity(self):
        
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        
        host = socket.gethostbyname("localhost")

        s.bind(("", self.port))
        
        # Set the timeout for the select call to 5 seconds
        timeout = 5
        while True:       
            s.listen(self.totalNodeNumber)
            # Use the select module to monitor the socket for activity
            ready = select.select([s], [], [], timeout)
                
            
            if ready[0]:
                connection, address = ready[0][0].accept()
                data = connection.recv(1024)
                if data:
                    data = json.loads(data)
                    isUpdated = self.update_distance_vector_list(data)
                    if(isUpdated):
                        self.advertise()
            else:
                self.printVectors()
                break



    def update_distance_vector_list(self, neighbor_distance_vector_list):
        selfdistance = 0
        for udestination, distance in neighbor_distance_vector_list.items():
            if int(udestination) == self.port:
                selfdistance = distance


        # Compare the local distance vector list to the distance vector list of the neighbor node
        isUpdated = False
        for udestination, distance in neighbor_distance_vector_list.items():
            destination = int(udestination)
            if destination == self.port:
                continue
            if destination not in self.distance_vector_list or selfdistance+distance < self.distance_vector_list[destination]:
                self.distance_vector_list[int(destination)] = distance+selfdistance
                isUpdated = True
        return isUpdated


node = Node(sys.argv[1])
node.readInitialVectors("{}.costs".format(str(node.port)))

#Listening Thread
listen_thread = threading.Thread(target=node.check_inactivity)
listen_thread.start()

#First advertise after initialization
node.advertise()
