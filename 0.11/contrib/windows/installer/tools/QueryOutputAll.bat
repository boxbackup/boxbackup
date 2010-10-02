@ECHO OFF
:    o=old, d=deleted, s=size info, t=timestamp, r=recursive
set Queryopts=-odstr
::set Queryopts=-str
query.exe "list %Queryopts%" quit > QueryOutputAllResults.txt
