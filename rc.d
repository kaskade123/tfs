ipcom_ipd_start("ipftps")
ipcom_ipd_start("iptelnets")
sysDebugModeSet(1)
systemSecurityIsEnabled=0
ld < /tffs/TestFramework.out
test_start(30)

