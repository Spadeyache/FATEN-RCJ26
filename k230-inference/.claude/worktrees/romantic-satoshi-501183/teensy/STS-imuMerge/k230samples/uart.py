from machine import UART

# Initialize UART1 with a baud rate of 115200, 8 data bits, no parity, and 1 stop bit
u1 = UART(UART.UART1, baudrate=115200, bits=UART.EIGHTBITS, parity=UART.PARITY_NONE, stop=UART.STOPBITS_ONE)

# Send data via UART1
u1.write("UART1 test")

# Read data from UART1
r = u1.read()

# Read a line of data from UART1
r = u1.readline()

# Read data into a specified byte array
b = bytearray(8)
r = u1.readinto(b)

# Release UART resources
u1.deinit()
