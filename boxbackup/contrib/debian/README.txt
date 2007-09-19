These start scripts are for Debian GNU/Linux.  If installed manually they should
be placed in /etc/init.d.  To create the symbolic links for the appropriate run
levels execute the following commands:

update-rc.d bbackupd defaults 90
update-rc.d bbstored defaults 80

James Stark
<jstark@ieee.org>
