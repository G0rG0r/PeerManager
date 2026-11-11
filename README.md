# Abstract 
this tool is used to improve connectivity over non-reachable devices that are constrained by isps , 
the idea is to make a bridging server that would listen for both the client and the server simultaneously and bridge the packets between them .

# Diagram 
<img src=/images/Diagram.png>

# How to use 
First you need any free public hosting server to run the tool on .
Then you need to configure the Peer.conf file and add the IP , server port and the client port .
Finally you need to run the tool and connect the server to the agent . 

# Prerequisites
GCC for compiling the source code.
Reachable Host.


# Step-by-step guide 

first we need to edit the Peer.conf file and add our service details.

<img src=/images/1.png>

then we can run our PeerManager and it will load the configuration file .

<img src=/images/2.png>

after successfully loading we can cocnnect our agent -the agent can be any service bridge or packet forwarder- 

<img src=/images/3.png>
<img src=/images/4.png>
we have successfully connected the backend.

now we can interact with PeerManager and we can we get responses from the connected backend .

<img src=/images/5.png>


# Future 
Currently the software still requires a third party that is reachable by both sides to establish the connection , which reduce the overall effectiveness of it .
the software now only allows port mappings , virtual hosts maybe added in the future for web services.
Server Authentication can be a good implementation as ports are exposed publicly .
TLS/Encryption can also be added.






