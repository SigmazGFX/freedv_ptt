import socket
import socketio
import json
import logging
import configparser

# Enable logging
logging.basicConfig(level=logging.DEBUG)

# Define the Socket.IO client
sio = socketio.Client(logger=False, engineio_logger=False)

# Global variables to store current mode and other settings
current_mode = "700D"  # Initial mode
config_data = {
    "fdvmode": "700D",
    "grid_square": "AA00aa",
    "callsign": "N0CALL",
    "version": "1.0.0",
    "message": "--"
}

# Function to load configuration values from config.ini
def load_config(filename="/home/pi/freedv_ptt/config.ini"):
    global current_mode, config_data
    config = configparser.ConfigParser(allow_no_value=True)
    config.optionxform = str  # Ensure case sensitivity

    with open(filename, "r") as f:
        for line in f:
            if "=" in line:
                key, value = line.strip().split("=", 1)
                key = key.strip()
                value = value.strip()
                config_data[key] = value

    # Read fdvmode from config file
    if "fdvmode" in config_data:
        current_mode = config_data["fdvmode"].strip()
    else:
        current_mode = "700X"  # Default value if fdvmode is not specified


# Load fdvmode from config.ini
load_config()

# Connect to the Socket.IO server
@sio.event
def connect():
    print("Connected to server")


@sio.event
def connect_error(data):
    print(f"Connection failed: {data}")


@sio.event
def disconnect():
    print("Disconnected from server")


@sio.event
def message(data):
    print(f"Message received: {data}")


@sio.event
def freq_change(data):
    print(f"Frequency changed to: {data['freq']} Hz")


@sio.event
def mode_change(data):
    global current_mode
    current_mode = data["mode"]
    print(f"Mode changed to: {current_mode}")


@sio.event
def tx_report(data):
    print(f"TX Report: Mode={data['mode']}, Transmitting={data['transmitting']}")


# Function to send a JSON message to the server
def send_message(message):
    sio.emit("message", message)
    print(f"Sent message to server: {message}")


def handle_ipc_commands():
    global current_mode  # Ensure we can modify the global current_mode
    # Set up the server socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(("localhost", 50007))
    server_socket.listen(1)
    print("IPC server listening on port 50007")

    while True:
        conn, addr = server_socket.accept()
        with conn:
            print("Connected by", addr)
            data = conn.recv(1024)
            if not data:
                break
            command = data.decode("utf-8").strip()

            if command.startswith("FREQ_CHANGE"):
                parts = command.split()
                if len(parts) == 2:
                    freq_khz = int(float(parts[1]) * 1e3)  # Convert kHz
                    sio.emit("freq_change", {"freq": freq_khz})
                    print(f"Emitted freq_change with freq: {freq_khz} kHz")
                else:
                    print("Invalid FREQ_CHANGE command format")

            elif command.startswith("MODE_CHANGE"):
                parts = command.split()
                if len(parts) == 2:
                    current_mode = parts[1]  # Update current_mode
                    sio.emit("tx_report", {"mode": current_mode, "transmitting": False})
                    print(f"Emitted tx_report with mode: {current_mode}")
                else:
                    print("Invalid tx_report command format")

            elif command == "TX_ON":
                sio.emit("tx_report", {"mode": current_mode, "transmitting": True})
                print(f"TX_ON command received and emitted with mode: {current_mode}")
            elif command == "TX_OFF":
                sio.emit("tx_report", {"mode": current_mode, "transmitting": False})
                print(f"TX_OFF command received and emitted with mode: {current_mode}")


# Start the client
if __name__ == "__main__":
    auth = {
        "callsign": f"{config_data['callsign']}",
        "grid_square": f"{config_data['grid_square']}",
        "version": f"{config_data['version']}",
        "role": "report_wo",
        "os": "linux",
    }
    try:
        # Attempt to connect
        sio.connect("ws://qso.freedv.org/", auth=auth)

        # Emit initial events once connected
        sio.emit("tx_report", {"mode": current_mode, "transmitting": False})
        sio.emit(
            "freq_change",
            {
                "freq": 14236000,  # 14.236 MHz converted to kHz
            },
        )
        sio.emit(
            "message_update",
            {
                "message": f"{config_data['message']}",
            },
        )

        # Handle IPC commands
        handle_ipc_commands()

        # Wait for events
        sio.wait()
    except socketio.exceptions.ConnectionError as e:
        print(f"ConnectionError: {e}")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # Disconnect from server
        sio.disconnect()
