sudo route del default gw 10.0.0.2 metric 5
sudo route del default gw 172.16.16.1 metric 6
sudo route add default gw 172.16.16.1
sudo ip tuntap delete dev tun0 mode tun
