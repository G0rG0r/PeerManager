# Abstract 
this tool is used to improve connectivity over non-reachable devices that are constrained by isps , 
the idea is to make a brdging server that would listen for both the client and the server simultanously and bridge the packets betwen them .

# Diagram 
<img src=/images/Diagram.png>

# How to use 
First you need any free public hosting server to run the tool on .
Then you need to configure the Peer.conf file and add the IP , server port and the client port .
Finally you need to run the tool and connect the server to the agent . 

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





