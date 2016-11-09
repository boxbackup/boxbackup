dptest write casket 50000 5000
if errorlevel 1 goto error
dptest read casket
if errorlevel 1 goto error
dptest read -wb casket
if errorlevel 1 goto error
dptest rcat casket 50000 50 500 32 32
if errorlevel 1 goto error
dptest combo casket
if errorlevel 1 goto error
dptest wicked casket 5000
if errorlevel 1 goto error
del /Q casket

crtest write casket 50000 500 10
if errorlevel 1 goto error
crtest read casket
if errorlevel 1 goto error
crtest read -wb casket
if errorlevel 1 goto error
crtest rcat casket 50000 5 10 500 32 32
if errorlevel 1 goto error
crtest combo casket
if errorlevel 1 goto error
crtest wicked casket 5000
if errorlevel 1 goto error
rd /S /Q casket

crtest write -lob casket 1000 50 10
if errorlevel 1 goto error
crtest read -lob casket
if errorlevel 1 goto error
rd /S /Q casket

rltest write casket 50000
if errorlevel 1 goto error
rltest read casket 50000
if errorlevel 1 goto error
del /Q casket*

hvtest write casket 50000
if errorlevel 1 goto error
hvtest read casket 50000
if errorlevel 1 goto error
del /Q casket

hvtest write -qdbm casket 50000
if errorlevel 1 goto error
hvtest read -qdbm casket 50000
if errorlevel 1 goto error
rd /S /Q casket

cbtest sort 5000
if errorlevel 1 goto error
cbtest strstr 500
if errorlevel 1 goto error
cbtest list 50000
if errorlevel 1 goto error
cbtest map 50000
if errorlevel 1 goto error
cbtest wicked 5000
if errorlevel 1 goto error
cbtest misc
if errorlevel 1 goto error

vltest write -tune 25 64 32 32 casket 50000
if errorlevel 1 goto error
vltest read casket
if errorlevel 1 goto error
vltest rdup -tune 25 64 256 256 casket 50000 50000
if errorlevel 1 goto error
vltest combo casket
if errorlevel 1 goto error
vltest wicked casket 5000
if errorlevel 1 goto error
del /Q casket

vltest write -int -cz -tune 25 64 32 32 casket 50000
if errorlevel 1 goto error
vltest read -int casket
if errorlevel 1 goto error
vltest rdup -int -cz -tune 25 64 256 256 casket 50000 50000
if errorlevel 1 goto error
vltest combo -cz casket
if errorlevel 1 goto error
vltest wicked -cz casket 5000
if errorlevel 1 goto error
del /Q casket

odtest write casket 500 50 5000
if errorlevel 1 goto error
odtest read casket
if errorlevel 1 goto error
odtest combo casket
if errorlevel 1 goto error
odtest wicked casket 500
if errorlevel 1 goto error
rd /S /Q casket

@echo off
echo #================================
echo # SUCCESS
echo #================================
goto :EOF

:error
@echo off
echo #================================
echo # ERROR
echo #================================
goto :EOF
