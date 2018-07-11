sudo killall dnsmasq

sudo ip netns add dnode1
sudo ip tuntap add dev tun1 mode tun user stas
sudo ip link set tun1 netns dnode1
sudo ip netns exec dnode1 ifconfig tun1 10.0.0.1 netmask 255.255.255.0
sudo ip netns exec dnode1 route add default gw 10.0.0.2
sudo ip netns exec dnode1 ip link set dev lo up

sudo ip link add veth1a type veth peer name veth1b
sudo ip link set veth1a netns dnode1
sudo ip netns exec dnode1 ifconfig veth1a 13.0.0.1 netmask 255.255.255.0
sudo ifconfig veth1b 13.0.0.2 netmask 255.255.255.0
sudo route add 10.0.0.1 dev veth1b
sudo route del -net 13.0.0.0 netmask 255.255.255.0

sudo ip netns add dnode2
sudo ip tuntap add dev tun2 mode tun user stas
sudo ip link set tun2 netns dnode2
sudo ip netns exec dnode2 ifconfig tun2 11.0.0.1 netmask 255.255.255.0
sudo ip netns exec dnode2 route add default gw 11.0.0.2
sudo ip netns exec dnode2 ip link set dev lo up

sudo ip link add veth2a type veth peer name veth2b
sudo ip link set veth2a netns dnode2
sudo ip netns exec dnode2 ifconfig veth2a 14.0.0.1 netmask 255.255.255.0
sudo ifconfig veth2b 14.0.0.2 netmask 255.255.255.0
sudo route add 11.0.0.1 dev veth2b
sudo route del -net 14.0.0.0 netmask 255.255.255.0
