# johann.maxen@gmail.com - September 2014
#
# This is a python Nagios plugin to fetch html formatted
# network temperature data from an Arduino Uno Ethernet R3

# NOTES:
# Remember to install urllib: pip install urllib3
# Most probably argparse also

import sys, re, urllib3, argparse
from urllib3 import Timeout

# nagios plugin API return values
OK = 0
WARNING = 1
CRITICAL = 2
UNKNOWN = 3

CONNECT_TIMEOUT = 2
READ_TIMEOUT = 2
RETRIES = 1

# default thresholds
t_warn = 28.0
t_crit = 31.0

serverpage = 'http://'

def main(type):
	retval = OK
	outstring = 'Status is '
		
	# Establish a connection with the Arduino Ethernet and
	# fetch data. A timeout of 5 seconds is set.
	try:
		http = urllib3.PoolManager(timeout=Timeout(connect=CONNECT_TIMEOUT,read=READ_TIMEOUT),retries=RETRIES)
#		http = urllib3.PoolManager(timeout=Timeout(connect=5.0))
		f = http.request('GET' , serverpage)
		f.status
	except:
		# If the URL cannot be reached return '3' == UNKNOWN
	    outstring = 'CRITICAL - Device not found on the network!!!'
	    retval = UNKNOWN
	    print(outstring)
	    sys.exit(retval) 

	
	# Decode the byte stream and pick out readings using a
	# regular expression
#	result = f.read().decode('utf-8')
	result = f.data
	m = re.search('T:(\d+\.\d+)', result)
#	print(result)

	if type == 'T':
		# temperature
		temp = float(m.group(1))
		if temp <= t_warn:
			outstring += 'OK - Temperature: ' + str(temp) + ' C|temperature=' + str(temp)
		elif t_warn < temp <= t_crit:
			outstring += 'WARNING! - Temperature: ' + str(temp) + ' C|temperature=' + str(temp)
			retval = WARNING
		elif temp > t_crit:
			outstring += 'CRITICAL!!! - Temperature: ' + str(temp) + ' C|temperature=' + str(temp)
			retval = CRITICAL
		
	print(outstring)	
	sys.exit(retval)

if __name__ == "__main__":
	# Option parsing
	parser = argparse.ArgumentParser()
	parser.add_argument("-t", "--type", help="type of data", choices="THL")
	parser.add_argument("-H", "--host", help="host IP", required=True)
	parser.add_argument("-w", "--warning", type=float, help="warning threshold")
	parser.add_argument("-c", "--critical", type=float, help="critical threshold")
	args = parser.parse_args()
	serverpage += args.host
	# Set warning/critical thresholds if provided
	if args.warning != None:
		t_warn = args.warning

	if args.critical != None:
		t_crit = args.critical
	
	main(args.type)