# this will put our dssl eth into separate namespace
# yota will remain in default namespace

sudo ip netns add dssl
sudo ip link set eth3 netns dssl
sudo ip netns exec dssl ip link set dev lo up
sudo ip netns exec dssl ifconfig eth3 172.16.16.x netmask 255.255.254.0
sudo ip netns exec dssl route add default gw 172.16.16.1

# now run:
# sudo ip netns exec dssl sudo ./dnode ...
# this will run dnode for dssl eth, run another dnode as usual, it'll
# go through yota. login into yota shell and do stuff. note that DNS will only
# work from yota shell, since only yota is in default namespace.
