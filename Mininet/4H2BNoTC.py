#!/usr/bin/env python

"""
Create a network (with OpenFlow 1.3 switches) connected to a remote Ryu controller on port 6653.
"""

import os
from mininet.net import Mininet
from mininet.node import Node, OVSSwitch, RemoteController
from mininet.cli import CLI
from mininet.log import lg, info
from mininet.util import waitListening
from mininet.topo import Topo

USERNAME = os.getlogin()
IP = '10.123.123.1/32'
CMD = '/usr/sbin/sshd'
OPTS = f'-D -o AuthorizedKeysFile=/home/{USERNAME}/.ssh/authorized_keys -o UseDNS=no -u0'

class ProtoTapo(Topo):
    "Single switch connected to n hosts."
    def build(self, n=2, sw=2):
        switches = []
        for s in range(sw):
            # Create OpenFlow 1.3 switches
            switch = self.addSwitch(f's{s + 1}', cls=OVSSwitch, protocols='OpenFlow13')
            switches.append(switch)
            for h in range(n):
                host = self.addHost(f'h{((s*n) + h + 1)}')
                self.addLink(host, switch)
        for l in range(sw-1):
            self.addLink(switches[l], switches[l+1])

def connectToRootNS(network, switch, ip, routes):
    """Connect hosts to root namespace via switch."""
    root = Node('root', inNamespace=False)
    intf = network.addLink(root, switch).intf1
    root.setIP(ip, intf=intf)
    network.start()
    for route in routes:
        root.cmd('route add -net ' + route + ' dev ' + str(intf))

def sshd(network, routes=None, switch=None):
    if not switch:
        switch = network['s1']
    if not routes:
        routes = ['10.0.0.0/24']
    connectToRootNS(network, switch, IP, routes)
    # Start SSH daemons
    for host in network.hosts:
        host.cmd(CMD + ' ' + OPTS + '&')
    info("*** Waiting for SSH daemons to start\n")
    for server in network.hosts:
        waitListening(server=server, port=22, timeout=5)
    info("\n*** Hosts are running SSH at:\n")
    for host in network.hosts:
        info(f"{host.name} – {host.IP()}\n")
    info("\n*** Type 'exit' to stop\n")
    CLI(network)
    # Cleanup
    for host in network.hosts:
        host.cmd('kill %' + CMD)
    network.stop()

if __name__ == '__main__':
    import argparse
    os.system('sudo mn -c')  # Cleanup previous mininet sessions
    parser = argparse.ArgumentParser(description="Mininet with Remote Ryu Controller")
    parser.add_argument('--hosts', type=int, default=2, help="Hosts per switch")
    parser.add_argument('--switches', type=int, default=2, help="Number of switches")
    args = parser.parse_args()

    lg.setLogLevel('info')
    tapo = ProtoTapo(n=args.hosts, sw=args.switches)
    # Connect to remote Ryu controller at 127.0.0.1:6653
    net = Mininet(
        topo=tapo,
        controller=RemoteController('ryu', ip='127.0.0.1', port=6653),
        waitConnected=True
    )
    sshd(net, routes=['10.0.0.0/24'], switch=net['s1'])