from machine import UART

# Initialize UART2 with a baud rate of 115200, 8 data bits, no parity, and 1 stop bit
u2 = UART(UART.UART2, baudrate=115200, bits=UART.EIGHTBITS, parity=UART.PARITY_NONE, stop=UART.STOPBITS_ONE)

# Send data via UART2
u2.write("UART1 test")

# Read data from UART2
r = u2.read()

# Read a line of data from UART2
r = u2.readline()

# Read data into a specified byte array
b = bytearray(8)
r = u2.readinto(b)

# Release UART resources
u2.deinit()
