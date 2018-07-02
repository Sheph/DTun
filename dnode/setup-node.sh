sudo ip tuntap add dev tun0 mode tun user stas
sudo ifconfig tun0 10.0.0.1 netmask 255.255.255.0
sudo route del default gw 172.16.16.1
sudo route add default gw 172.16.16.1 metric 6
sudo route add default gw 10.0.0.2 metric 5
