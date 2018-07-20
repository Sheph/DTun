echo 'source ~/.bashrc
export PS1="\e[1;33m\u@\h \w> \e[m"' > /tmp/my_bash.rc
sudo ip netns exec dnode1 sudo -u root bash --rcfile /tmp/my_bash.rc -i
