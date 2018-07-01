sudo ip tuntap add dev tun0 mode tun user stas
sudo ifconfig tun0 10.0.0.1 netmask 255.255.255.0
sudo route del default gw csp1.zte.com.cn
sudo route add default gw csp1.zte.com.cn metric 6
sudo route add default gw 10.0.0.2 metric 5
