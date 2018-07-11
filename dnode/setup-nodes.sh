sudo killall dnsmasq

sudo ip netns add dnode1
sudo ip tuntap add dev tun1 mode tun user stas
sudo ip link set tun1 netns dnode1
sudo ip netns exec dnode1 ifconfig tun1 10.0.0.1 netmask 255.255.255.0
sudo ip netns exec dnode1 route add default gw 10.0.0.2
sudo ip netns exec dnode1 ip link set dev lo up

sudo ip netns add dnode2
sudo ip tuntap add dev tun2 mode tun user stas
sudo ip link set tun2 netns dnode2
sudo ip netns exec dnode2 ifconfig tun2 11.0.0.1 netmask 255.255.255.0
sudo ip netns exec dnode2 route add default gw 11.0.0.2
sudo ip netns exec dnode2 ip link set dev lo up
