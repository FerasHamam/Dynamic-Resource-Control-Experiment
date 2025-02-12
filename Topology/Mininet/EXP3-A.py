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

    print("*** Copying C programs to hosts")

    # Copy zmqServer directory to h1 and zmqReceiver to h3
    h1.cmd('cp -r /home/cc/zmqServer /tmp/')
    h2.cmd('cp -r /home/cc/zmqReceiver /tmp/')


    print("*** Setting up zmqServer and zmqReceiver")
    # Run setup.sh for zmqServer on h1 and zmqReceiver on h3
    h1.cmd('/tmp/zmqServer/scripts/setup.sh &')
    h2.cmd('/tmp/zmqReceiver/scripts/setup.sh &')

    print("*** Building zmqServer and zmqReceiver")
    # build zmqServer and zmqReceiver
    h1.cmd('cd /tmp/zmqServer/build & cmake .. & make &')
    h2.cmd('cd /tmp/zmqReceiver/build & cmake .. & make &')

    print("*** Starting zmqServer and zmqReceiver")
    # Run zmqServer and zmqReceiver
    h1.cmd('/tmp/zmqServer/build/sender &')
    h2.cmd('/tmp/zmqReceiver/build/receiver &')

    print("*** Running CLI")
    CLI(net)

    print("*** Stopping network")
    net.stop()

if __name__ == '__main__':
    setLogLevel('info')
    os.system('sudo mn -c')
    os.system('sudo rm -rf /tmp/zmq*')
    customTopology()
