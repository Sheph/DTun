sudo ip netns add dnode1
sudo ip tuntap add dev tun0 mode tun user stas
sudo ip link set tun0 netns dnode1
sudo ip netns exec dnode1 ifconfig tun0 10.0.0.1 netmask 255.255.255.0
sudo ip netns exec dnode1 route add default gw 10.0.0.2
