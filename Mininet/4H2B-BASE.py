from mininet.net import Mininet
from mininet.node import DefaultController, OVSKernelSwitch
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.log import setLogLevel
import os

def customTopology():
    net = Mininet(controller=DefaultController, switch=OVSKernelSwitch, link=TCLink)

    print("*** Adding controller")
    net.addController('c0', controller=DefaultController, port=6633)

    print("*** Adding hosts")
    h1 = net.addHost('h1', ip='10.0.0.1/24')
    h2 = net.addHost('h2', ip='10.0.0.2/24')
    h3 = net.addHost('h3', ip='10.0.0.3/24')
    h4 = net.addHost('h4', ip='10.0.0.4/24')

    print("*** Adding switches")
    s1 = net.addSwitch('s1')
    s2 = net.addSwitch('s2')

    print("*** Creating links")
    net.addLink(h1, s1)
    net.addLink(h3, s1)
    net.addLink(s1, s2)
    net.addLink(s2, h2)
    net.addLink(s2, h4)

    print("*** Starting network")
    net.start()

    print("*** Adding default switch flows")
    s1.cmd('ovs-ofctl add-flow s1 "priority=1,actions=normal"')
    s2.cmd('ovs-ofctl add-flow s2 "priority=1,actions=normal"')
    
    # TC
    #s1.cmd("sudo tc qdisc del dev s1-eth0 root")

    print("*** Running CLI")
    CLI(net)

    print("*** Stopping network")
    net.stop()

if __name__ == '__main__':
    setLogLevel('info')
    os.system('sudo mn -c')
    os.system('sudo rm -rf /tmp/zmq*')
    customTopology()
