net stop GigaLock
ping 192.168.254.254 -n 2 -w 1000 > nul 
net start GigaLock
echo off
ping 192.168.254.254 -n 5 -w 1000 > nul 
