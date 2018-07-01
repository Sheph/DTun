sudo route del default gw 10.0.0.2 metric 5
sudo route del default gw csp1.zte.com.cn metric 6
sudo route add default gw csp1.zte.com.cn
sudo ip tuntap delete dev tun0 mode tun
