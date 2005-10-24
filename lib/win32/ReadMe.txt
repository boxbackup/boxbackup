Upgrade instructions

Version 0.09g to 0.09h

This version included patches to the server as well. The server for this 
upgrade can be found at http://home.earthlink.net/~gniemcew/ but hopefully 
will be merged into the core in the next release.

New values in the bbackupd.conf can now be added:

StoreObjectInfoFile = C:\Program Files\Box Backup\bbackupd\bbackupd.dat

This stores the state when a backup daemon is shutdown.

KeepAliveTime = 250

This is imperative if MaximumDiffingTime is larger than 300, this stops the ssl 
layer timing out when a diff is performed. It is wise to set MaximumDiffingTime 
long enough for the largest file you may have. If you do not wish to upgrade your 
server then make KeepAliveTime greater than MaximumDiffingTime.

Have fun

Nick
