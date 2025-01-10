#!/usr/bin/env python

"""
Create a network and start sshd(8) on each host.

"""
import os

from mininet.net import Mininet
from mininet.node import Node
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.log import lg, info
from mininet.util import waitListening
from mininet.topo import Topo


USERNAME = os.getlogin()
IP = '10.123.123.1/32'
CMD ='/usr/sbin/sshd'
OPTS = f'-D -o AuthorizedKeysFile=/home/{USERNAME}/.ssh/authorized_keys -o UseDNS=no -u0'

class ProtoTapo(Topo):
    "Single switch connected to n hosts."
    def build(self, n=2, sw=2,h_bw=25,s_bw=25,h_delay='5ms',s_delay='5ms',h_loss=0,s_loss=0):
        switches = []
        for s in range(sw):
            switch = self.addSwitch(f's{s + 1}')
            switches.append(switch)
            for h in range(n):
                host = self.addHost(f'h{((s*n)+ h + 1)}')
                self.addLink(host, switch, bw=h_bw, delay=h_delay, loss=h_loss, use_htb=True)
        for l in range(sw-1):
            self.addLink(switches[l], switches[l+1], bw=s_bw, delay=s_delay, loss=s_loss, use_htb=True)

def connectToRootNS( network, switch, ip, routes ):
    """Connect hosts to root namespace via switch. Starts network.
      network: Mininet() network object
      switch: switch to connect to root namespace
      ip: IP address for root namespace node
      routes: host networks to route to"""
    # Create a node in root namespace and link to switch 0
    root = Node( 'root', inNamespace=False )
    intf = network.addLink( root, switch ).intf1
    root.setIP( ip, intf=intf )
    # Start network that now includes link to root namespace
    network.start()
    # Add routes from root ns to hosts
    for route in routes:
        root.cmd( 'route add -net ' + route + ' dev ' + str( intf ) )

# pylint: disable=too-many-arguments
def sshd( network, routes=None, switch=None ):
    """Start a network, connect it to root ns, and run sshd on all hosts.
       ip: root-eth0 IP address in root namespace (10.123.123.1/32)
       routes: Mininet host networks to route to (10.0/24)
       switch: Mininet switch to connect to root namespace (s1)"""
    if not switch:
        switch = network[ 's1' ]  # switch to use
    if not routes:
        routes = [ '10.0.0.0/24' ]
    connectToRootNS( network, switch, IP, routes )
    for host in network.hosts:
        host.cmd( CMD + ' ' + OPTS + '&' )
    info( "*** Waiting for ssh daemons to start\n" )
    for server in network.hosts:
        waitListening( server=server, port=22, timeout=5 )

    info( "\n*** Hosts are running sshd at the following addresses:\n" )
    for host in network.hosts:
        info( host.name, host.IP(), '\n' )
    info( "\n*** Type 'exit' or control-D to shut down network\n" )
    CLI( network )
    for host in network.hosts:
        host.cmd( 'kill %' + CMD )
    network.stop()

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description="Mininet SSH Daemon Setup")
    parser.add_argument('--hosts', type=int, default=2, help="Number of hosts per switch")
    parser.add_argument('--switches', type=int, default=2, help="Number of switches")
    parser.add_argument('--hbw', type=int, default=25, help="Bandwidth for Host links (Mbps)")
    parser.add_argument('--sbw', type=int, default=25, help="Bandwidth for Switch links (Mbps)")
    parser.add_argument('--hdelay', type=str, default='5ms', help="Host Link delay")
    parser.add_argument('--sdelay', type=str, default='5ms', help="Switch Link delay")
    parser.add_argument('--hloss', type=float, default=0.0, help="Packet loss percentage -- Host Link")
    parser.add_argument('--sloss', type=float, default=0.0, help="Packet loss percentage -- Switch Link")
    parser.add_argument('--ip', type=str, default='10.123.123.1/32', help="Root IP")
    args = parser.parse_args()

    lg.setLogLevel('info')
    tapo = ProtoTapo(n=args.hosts, sw=args.switches,h_bw=args.hbw,s_bw=args.sbw,
                     h_delay=args.hdelay,s_delay=args.sdelay,h_loss=args.hloss,s_loss=args.sloss)
    net = Mininet(topo=tapo, link=TCLink, waitConnected=True)
    sshd(net, routes=['10.0.0.0/24'], switch=net['s1'])
