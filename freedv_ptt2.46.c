/*
 * freedv_ptt2.4.6.c
 *
 * Description:
 * This program implements a graphical user interface (GUI) using GTK for controlling
 * an sBitx radio with Codec2 FreeDV digital voice encoder/decoder. The GUI allows the
 * user to select a frequency band from predefined options and change the operating
 * frequency and mode accordingly using telnet commands directly to the radio subsystem.
 *
 * Features:
 * - GUI with dropdown menus for frequency band selection
 * - Buttons for TX (transmit) and RX (receive)
 * - Header bar with Settings button for codec settings
 * - Automatically connects to a telnet server to send frequency and mode commands
 * - Squelch control and audio input level adjustment
 * - Integration with FreeDV Reporter website via Socket.io 
 *
 * Usage:
 * 1. Compile the program using:
 *    gcc -o freedv_ptt2.46 freedv_ptt2.46.c `pkg-config --cflags --libs gtk+-3.0`
 *
 * 2. Run the program:
 *    ./freedv_ptt2.4.6
 *
 * Requirements:
 *
 * - As the code is written the directory must be called /freedv_ptt this of course can be changed but all references to the location in the code will need adjustment to reflect new.
 *
 * - Codec2 freedv_tx, freedv_rx in freedv_ptt directory with this comiled code 
 * - Codec2 library /usr/lib/libcodec2.so.1.2 (https://github.com/drowe67/codec2)
 *
 * - GTK+ 3 library
 * - Telnet server will be running on localhost (127.0.0.1) at port 8081
 * - Hamlib Net Server eill be running on localhost (127.0.0.1) at port 4532
 *
 *
 * - To communicate with the qso.freedv.org reporter website the host machine will need to have the socket.io library installed
 * - The sioclient.py file is the client required to make this connection.
 *
 *
 * Author:
 * Jon Bruno
 *
 * Callsign:
 * W2JON
 *
 * Date:
 * 6/9/24
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdbool.h>
 
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4532
#define TELNET_PORT 8081
#define BUFFER_SIZE 1024
#define CONFIG_FILE "config.ini"
const char * RELEASE_VERSION = "2.4.6a";
int sockfd_telnet, sockfd_server;
int rxtx_mode = -1; // -1 indicates no mode selected, 0 for TX, 1 for RX
pid_t tx_pid, rx_pid;//Global variable to store the PID of the TX and RX process
pid_t python_pid;//Global variable to store the PID of the Python script process
GtkWidget * value_label = NULL; // Declare value_label globally
GtkWidget * selected_menu_item = NULL; // Used to track selected freq dropdown


// Used to print the current environment variables. This was used for diagnostics and is not required
//extern char **environ;
//
//void print_environment_variables() {
//    int i = 0;
//    while (environ[i] != NULL) {
//        printf("%s\n", environ[i++]);
//    }
//}


// Function to start the Python script 
void start_python_script() {
    if ((python_pid = fork()) == 0) {
        // Child process: Execute the Python script
        execl("/usr/bin/python3", "python3", "/home/pi/freedv_ptt/sioclient.py", NULL);
        perror("Failed to start Python script");
        exit(EXIT_FAILURE);
    } else if (python_pid < 0) {
        perror("Failed to fork");
        exit(EXIT_FAILURE);
    } else {
        printf("Started Python script with PID: %d\n", python_pid);
    }
}


//// Function to start the -=(Compiled)=- Python script 
//void start_python_script() {
//    if ((python_pid = fork()) == 0) {
//        // Child process: Create a new process group
//        setpgid(0, 0);
//        char *argv[] = {"/home/pi/freedv_ptt/assets/sioclient", NULL};
//        char *envp[] = { "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", NULL };

//        // Redirect output to a log file
//        freopen("/tmp/sioclient_stdout.log", "w", stdout);
//        freopen("/tmp/sioclient_stderr.log", "w", stderr);

//        execve(argv[0], argv, envp);
//        perror("Failed to start Python executable");
//        exit(EXIT_FAILURE);
//    } else if (python_pid < 0) {
//        perror("Failed to fork");
//        exit(EXIT_FAILURE);
//    } else {
//        printf("Started Python executable with PID: %d\n", python_pid);
//        // Set the parent process to be the leader of the process group
//        setpgid(python_pid, python_pid);
//    }
//}

// Function to send IPC command
void send_ipc_command(const char *command) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[1024] = {0};

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(50007);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return;
    }

    // Send command
    send(sock, command, strlen(command), 0);
    printf("IPC command sent: %s\n", command);

    close(sock);
}

// Function to send commands to the Hamlib Net server
void send_command(const char * command) {
  if (send(sockfd_server, command, strlen(command), 0) < 0) {
    perror("Send failed");
    exit(EXIT_FAILURE);
  }
}

void handle_termination(int signum) {
    // Terminate the Python script process if it's running
    if (python_pid > 0) {
        // Kill the entire process group
        kill(-python_pid, SIGTERM);
        printf("Terminated Python script with PID: %d\n", python_pid);
        // Wait for the child process to terminate
        waitpid(python_pid, NULL, 0);
    }

    // Close the socket opened to the Hamlib server
    close(sockfd_server);  
    exit(0);
}

// Function to check if the configuration file exists
int config_file_exists() {
  FILE * file = fopen(CONFIG_FILE, "r");
  if (file) {
    fclose(file);
    return 1; // File exists
  }
  return 0; // File doesn't exist
}
int check_audio_device(const char *device) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/asound/%s", device);
    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1; // Device found
    }
    return 0; // Device not found
}

int check_program_running(const char *program) {
    char command[256];
    snprintf(command, sizeof(command), "pgrep %s > /dev/null", program);
    return system(command) == 0;
}

void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    gtk_widget_destroy(GTK_WIDGET(dialog));
    if (response_id == GTK_RESPONSE_OK) {
        gtk_main_quit(); // Quit GTK main loop after dialog is destroyed
    }
}

void show_message_dialog(const char *message) {
    GtkWidget *dialog, *content_area, *label, *button;
    GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;

    gtk_init(NULL, NULL);

    dialog = gtk_dialog_new_with_buttons("System Error", NULL, flags, "OK", GTK_RESPONSE_OK, NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new(message);
    gtk_container_add(GTK_CONTAINER(content_area), label);

    gtk_widget_show_all(dialog);

    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL); // Close the dialog on any response
    gtk_dialog_run(GTK_DIALOG(dialog));
}


// Function to create the configuration file with default values
void create_default_config() {
  FILE * file = fopen(CONFIG_FILE, "w");
  if (file != NULL) {
    fprintf(file, "fdvmode=700D\n");
    fprintf(file, "callsign=N0CALL\n");
    fprintf(file, "grid_square=AA00ab\n");
    fprintf(file, "squelch_level=-5\n");
    fprintf(file, "input_level=1\n");
    fprintf(file, "start_mode=-1\n");
    fprintf(file, "version=sBitx fdv_ptt %s\n",RELEASE_VERSION);
    fprintf(file, "message=--\n");
    fclose(file);
  } else {
    perror("Failed to create configuration file");
  }
}

// Generic function to save a key-value pair to a configuration file
void save_config(const char * key,
  const char * value) {
  FILE * file = fopen(CONFIG_FILE, "r+");
  if (file != NULL) {
    char line[256];
    int found = 0;
    FILE * tempFile = tmpfile();
    if (tempFile == NULL) {
      perror("Failed to create temporary file");
      fclose(file);
      return;
    }

    // Read each line from the configuration file and update key-value pair
    while (fgets(line, sizeof(line), file) != NULL) {
      if (strstr(line, key) == line) {
        fprintf(tempFile, "%s=%s\n", key, value);
        found = 1;
      } else {
        fputs(line, tempFile);
      }
    }

    // If key was not found, append it to the end of the temporary file
    if (!found) {
      fprintf(tempFile, "%s=%s\n", key, value);
    }

    // Copy the updated content back to the original file
    rewind(file);
    rewind(tempFile);
    while (fgets(line, sizeof(line), tempFile) != NULL) {
      fputs(line, file);
    }

    // Truncate the original file to the new length
    ftruncate(fileno(file), ftell(file));

    fclose(tempFile);
    fclose(file);
  } else {
    perror("Failed to open configuration file");
  }
}

// Generic function to load a value by key from a configuration file
void load_config(const char * key, char * value,
  const char * default_value) {
  strcpy(value, default_value);

  FILE * file = fopen(CONFIG_FILE, "r");
  if (file != NULL) {
    char file_key[50];
    char file_value[256];
    while (fscanf(file, "%49[^=]=%255s\n", file_key, file_value) == 2) {
      if (strcmp(file_key, key) == 0) {
        strcpy(value, file_value);
        break;
      }
    }
    fclose(file);
  } else {
    perror("Failed to open configuration file");
  }
}


// Specific save/load functions for each configuration parameter

void save_squelch_level(int squelch_level) {
  char value[50];
  snprintf(value, sizeof(value), "%d", squelch_level);
  save_config("squelch_level", value);
}

int load_squelch_level() {
  char value[50];
  load_config("squelch_level", value, "-5");
  return atoi(value);
}

void save_input_level(int input_level) {
  char value[50];
  snprintf(value, sizeof(value), "%d", input_level);
  save_config("input_level", value);
}

int load_input_level() {
  char value[50];
  load_config("input_level", value, "1");
  return atoi(value);
}

void save_fdvmode(const char * fdvmode) {
  save_config("fdvmode", fdvmode);
 
// Send IPC command for mode change
  char command[30]; // Assuming a sufficient size for the command
  sprintf(command, "MODE_CHANGE %s", fdvmode);
  send_ipc_command(command);
  
}
void save_release_version(const char *release_version) {
  // Allocate enough memory to hold the concatenated string
  char full_version[256]; // Adjust size as needed

  // Concatenate the prefix and the release version in one line
  snprintf(full_version, sizeof(full_version), "sBitx fdv_ptt %s", release_version);

  // Save the concatenated string
  save_config("version", full_version);
}

char * load_fdvmode() {
  static char fdvmode[256];
  load_config("fdvmode", fdvmode, "700D");
  printf("Mode from config file: %s\n", fdvmode);
  return fdvmode;
}

void save_callsign(const char * callsign) {
  save_config("callsign", callsign);
}

char * load_callsign() {
  static char callsign[256];
  load_config("callsign", callsign, "N0CALL");
  printf("Callsign from config file: %s\n", callsign);
  return callsign;
}

void save_grid_square(const char * grid_square) {
  save_config("grid_square", grid_square);
}

char * load_grid_square() {
  static char grid_square[256];
  load_config("grid_square", grid_square, "AA00ab");
  printf("Grid_square from config file: %s\n", grid_square);
  return grid_square;
}

// Function to update the value label when the slider is adjusted
void on_adjustment_value_changed(GtkAdjustment * adjustment, gpointer data) {
  // Get the value of the adjustment
  gdouble value = gtk_adjustment_get_value(adjustment);

  // Update the label with the new value
  gchar * label_text = g_strdup_printf("<b>%.0f</b>", value);
  gtk_label_set_markup(GTK_LABEL(data), label_text);
  g_free(label_text);
}

// Function to apply codec settings
void apply_codec_settings(int squelch_level, int input_level, const char * fdvmode, const char * callsign, const char * grid_square) {
  // Implement the logic to apply codec settings here
  printf("Saved squelch level: %d\n", squelch_level);
  printf("Saved input level: %d\n", input_level);
  printf("Saved mode: %s\n", fdvmode);
  printf("Saved Callsign: %s\n", callsign);
  printf("Saved Grid Square: %s\n", grid_square);
  save_squelch_level(squelch_level);
  save_input_level(input_level);
  save_fdvmode(fdvmode);
  save_callsign(callsign);
  save_grid_square(grid_square);
  save_release_version(RELEASE_VERSION);
 
}

// Function to handle apply button click
void on_apply_button_clicked(GtkButton * button, gpointer data) {
  GtkAdjustment * squelch_adjustment = GTK_ADJUSTMENT(data);
  GtkAdjustment * input_adjustment = GTK_ADJUSTMENT(g_object_get_data(G_OBJECT(button), "input_adjustment"));

  // Ensure the mode is retrieved correctly
  GtkWidget * mode_700c_button = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "mode_700c_button"));
  GtkWidget * mode_700d_button = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "mode_700d_button"));
  GtkWidget * mode_700e_button = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "mode_700e_button"));

  const char * fdvmode;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mode_700c_button))) {
    fdvmode = "700C";
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mode_700d_button))) {
    fdvmode = "700D";
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mode_700e_button))) {
    fdvmode = "700E";
  }

  int squelch_level = (int) gtk_adjustment_get_value(squelch_adjustment);
  int input_level = (int) gtk_adjustment_get_value(input_adjustment);

  // Retrieve the callsign from the text entry
  GtkWidget * callsign_entry = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "callsign_entry"));
  const char * callsign = gtk_entry_get_text(GTK_ENTRY(callsign_entry));
  
   // Retrieve the grid_square from the text entry
  GtkWidget * grid_square_entry = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "grid_square_entry"));
  const char * grid_square = gtk_entry_get_text(GTK_ENTRY(grid_square_entry));

  // Apply codec settings and save the callsign
  apply_codec_settings(squelch_level, input_level, fdvmode, callsign, grid_square);
}

// Function to handle TX button click
void on_tx_button_clicked(GtkButton * button, gpointer data) {
  if (rxtx_mode != 0) {
    // If not already in TX mode, terminate RX process (if running) and launch TX process
    if (rx_pid > 0) {
      if (killpg(rx_pid, SIGTERM) == -1 && errno != ESRCH) {
        perror("Failed to kill RX process group");
        exit(EXIT_FAILURE);
      }
      rx_pid = 0;
    }
    // Load the input level from the configuration file
    int input_level = load_input_level();
    char * mode = load_fdvmode();
    char * callsign = load_callsign();

    if ((tx_pid = fork()) == 0) {
      if (setpgid(0, 0) == -1) {
        perror("Failed to set TX process group");
        exit(EXIT_FAILURE);
      }
      char tx_command[256];

      // Buffer included to reduce underruns ( This buffer could induce approx 1 second of latency to the audio stream depending on system load.)
      sprintf(tx_command, "arecord -f S16_LE -c 1 -r 8000 -D plughw:CARD=5,DEV=0 | sox -t raw -r 8000 -e signed -b 16 -c 1 - -t raw - vol %ddB | ./freedv_tx %s --reliabletext %s - - | aplay -f S16_LE -D plughw:CARD=2,DEV=0 --buffer-size=8192", input_level, mode, callsign);

      printf("Executing TX command: %s\n", tx_command);
      execl("/bin/sh", "sh", "-c", tx_command, NULL);
      perror("Failed to execute TX process");
      exit(EXIT_FAILURE);
    }

    rxtx_mode = 0;
    send_command("T 1\n"); // Send TX command to radio
    printf("Switched to TX mode.\n");
    // Send IPC command to Python script
    send_ipc_command("TX_ON");
  }
}

// Function to handle RX button click
void on_rx_button_clicked(GtkButton * button, gpointer data) {
  if (rxtx_mode != 1) {
    // If not already in RX mode, terminate TX process (if running) and launch RX process
    if (tx_pid > 0) {
      // Insert delay of 1.5 seconds before killing the TX process. This will ensure the aplay buffer has played out.
      usleep(1500000); // 1.5 seconds = 1,500,000 microseconds

      if (killpg(tx_pid, SIGTERM) == -1 && errno != ESRCH) {
        perror("Failed to kill TX process group");
        exit(EXIT_FAILURE);
      }
      tx_pid = 0;
    }

    // Load the squelch level from the configuration file
    int squelch_level = load_squelch_level();
    char * mode = load_fdvmode();

    if ((rx_pid = fork()) == 0) {
      if (setpgid(0, 0) == -1) {
        perror("Failed to set RX process group");
        exit(EXIT_FAILURE);
      }
      char rx_command[256];
      // Modify the rx_command string to include the squelch level argument
      sprintf(rx_command, "arecord -f S16_LE -c 1 -r 8000 -D plughw:CARD=1,DEV=1 |./freedv_rx %s --squelch %d - - -| aplay -f S16_LE -D plughw:CARD=5,DEV=0", mode, squelch_level);
      //sprintf(rx_command, "arecord -f S16_LE -c 1 -r 8000 -D plughw:CARD=1,DEV=1 |./freedv_rx %s --squelch %d --reliabletext --txtrx txtrx.tmp -v - - | aplay -f S16_LE -D plughw:CARD=5,DEV=0", mode, squelch_level);

      printf("Executing RX command: %s\n", rx_command);
      execl("/bin/sh", "sh", "-c", rx_command, NULL);
      perror("Failed to execute RX process");
      exit(EXIT_FAILURE);
    }
    rxtx_mode = 1;
    send_command("T 0\n"); // Send RX command to radio
    printf("Switched to RX mode.\n");
    // Send IPC command to Python script
    send_ipc_command("TX_OFF");
  }
}

// Function to handle closing of the GTK window
void on_window_closed(GtkWidget * widget, gpointer data) {
  close(sockfd_server);
  if (tx_pid > 0) {
    if (killpg(tx_pid, SIGTERM) == -1 && errno != ESRCH) {
      perror("Failed to kill TX process group");
      exit(EXIT_FAILURE);
    }
    tx_pid = 0;
  }
  if (rx_pid > 0) {
    if (killpg(rx_pid, SIGTERM) == -1 && errno != ESRCH) {
      perror("Failed to kill RX process group");
      exit(EXIT_FAILURE);
    }
    rx_pid = 0;
  }
  gtk_main_quit();
}

// Function to open the codec settings window
void open_codec_settings_window(GtkWidget * widget, gpointer data) {
  // Create a new window
  GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Codec Settings");
  gtk_window_set_default_size(GTK_WINDOW(window), 300, 150);

  // Create a vertical box layout
  GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_container_add(GTK_CONTAINER(window), vbox);

  // Create a label for the squelch level
  GtkWidget * squelch_label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(squelch_label), "<b>Squelch Level</b>");
  gtk_box_pack_start(GTK_BOX(vbox), squelch_label, FALSE, FALSE, 0);

  // Create an adjustment for the squelch level slider
  GtkAdjustment * squelch_adjustment = gtk_adjustment_new(load_squelch_level(), -5, 15, 1, 1, 0);
  GtkWidget * squelch_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, squelch_adjustment);
  gtk_widget_set_hexpand(squelch_slider, TRUE);
  gtk_box_pack_start(GTK_BOX(vbox), squelch_slider, TRUE, TRUE, 0);

  // Create a label to display the current value of the squelch level slider
  gchar * squelch_label_text = g_strdup_printf("<b>%d</b>", (int) gtk_adjustment_get_value(squelch_adjustment));
  GtkWidget * squelch_value_label = gtk_label_new(squelch_label_text);
  gtk_label_set_use_markup(GTK_LABEL(squelch_value_label), TRUE);
  g_free(squelch_label_text);
  gtk_box_pack_start(GTK_BOX(vbox), squelch_value_label, FALSE, FALSE, 0);

  // Connect the squelch level adjustment's "value_changed" signal to the callback function
  g_signal_connect(squelch_adjustment, "value_changed", G_CALLBACK(on_adjustment_value_changed), squelch_value_label);

  // Create a horizontal separator line
  GtkWidget * separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start(GTK_BOX(vbox), separator, FALSE, FALSE, 5); // Add 5 pixels of space above the separator

  // Create a label for the input level
  GtkWidget * input_label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(input_label), "<b>Input Level dB</b>");
  gtk_box_pack_start(GTK_BOX(vbox), input_label, FALSE, FALSE, 0);

  // Create an adjustment for the input level slider
  GtkAdjustment * input_adjustment = gtk_adjustment_new(load_input_level(), -10, +10, 1, 1, 0);
  GtkWidget * input_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, input_adjustment);
  gtk_widget_set_hexpand(input_slider, TRUE);
  gtk_box_pack_start(GTK_BOX(vbox), input_slider, TRUE, TRUE, 0);

  // Create a label to display the current value of the input level slider
  gchar * input_label_text = g_strdup_printf("<b>%ddB</b>", (int) gtk_adjustment_get_value(input_adjustment));
  GtkWidget * input_value_label = gtk_label_new(input_label_text);
  gtk_label_set_use_markup(GTK_LABEL(input_value_label), TRUE);
  g_free(input_label_text);
  gtk_box_pack_start(GTK_BOX(vbox), input_value_label, FALSE, FALSE, 0);

  // Connect the input level adjustment's "value_changed" signal to the callback function
  g_signal_connect(input_adjustment, "value_changed", G_CALLBACK(on_adjustment_value_changed), input_value_label);

  // Create a horizontal box for the mode selection
  GtkWidget * mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_pack_start(GTK_BOX(vbox), mode_box, FALSE, FALSE, 0);

  // Create a label for the mode selection
  GtkWidget * mode_label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(mode_label), "<b> Select Mode</b>");
  gtk_box_pack_start(GTK_BOX(mode_box), mode_label, FALSE, FALSE, 0);

  // Create a radio button for 700C
  GtkWidget * mode_700c_button = gtk_radio_button_new_with_label_from_widget(NULL, "700C");
  gtk_box_pack_start(GTK_BOX(mode_box), mode_700c_button, FALSE, FALSE, 0);

  // Create a radio button for 700D
  GtkWidget * mode_700d_button = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mode_700c_button), "700D");
  gtk_box_pack_start(GTK_BOX(mode_box), mode_700d_button, FALSE, FALSE, 0);

  // Create a radio button for 700E
  GtkWidget * mode_700e_button = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mode_700c_button), "700E");
  gtk_box_pack_start(GTK_BOX(mode_box), mode_700e_button, FALSE, FALSE, 0);

  // Load the current fdvmode to set the active button
  const char * current_mode = load_fdvmode();
  if (strcmp(current_mode, "700C") == 0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode_700c_button), TRUE);
  } else if (strcmp(current_mode, "700D") == 0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode_700d_button), TRUE);
  } else if (strcmp(current_mode, "700E") == 0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode_700e_button), TRUE);
  }
  // Create a horizontal separator1 line
  GtkWidget * separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start(GTK_BOX(vbox), separator1, FALSE, FALSE, 5); // Add 5 pixels of space above the separator

  // Create a horizontal box for the callsign label and entry
  GtkWidget * hbox_callsign = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); // 5 pixels of space between elements
  gtk_box_pack_start(GTK_BOX(vbox), hbox_callsign, FALSE, FALSE, 0);

  // Create a label for the callsign
  GtkWidget * callsign_label = gtk_label_new(" Callsign:      ");
  gtk_box_pack_start(GTK_BOX(hbox_callsign), callsign_label, FALSE, FALSE, 0);

  // Create a text entry for the callsign and make it more narrow
  GtkWidget * callsign_entry = gtk_entry_new();
  gtk_widget_set_size_request(callsign_entry, 5, -1); // Set the width to 25 pixels
  gtk_box_pack_start(GTK_BOX(hbox_callsign), callsign_entry, FALSE, FALSE, 0);

  // Load the current callsign and set it in the text entry
  const char * current_callsign = load_callsign();
  gtk_entry_set_text(GTK_ENTRY(callsign_entry), current_callsign);

  // Create a horizontal box for grid_square label and entry
  GtkWidget * hbox_grid_square = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); // 5 pixels of space between elements
  gtk_box_pack_start(GTK_BOX(vbox), hbox_grid_square, FALSE, FALSE, 0);

  // Create a label for the grid_square
  GtkWidget * grid_square_label = gtk_label_new(" Grid square:");
  gtk_box_pack_start(GTK_BOX(hbox_grid_square), grid_square_label, FALSE, FALSE, 0);

  // Create a text entry for the grid_square and make it more narrow
  GtkWidget * grid_square_entry = gtk_entry_new();
  gtk_widget_set_size_request(grid_square_entry, 5, -1); // Set the width to 25 pixels
  gtk_box_pack_start(GTK_BOX(hbox_grid_square), grid_square_entry, FALSE, FALSE, 0);

  // Load the current grid_square and set it in the text entry
  const char * current_grid_square = load_grid_square();
  gtk_entry_set_text(GTK_ENTRY(grid_square_entry), current_grid_square);


  // Create an apply button
  GtkWidget * apply_button = gtk_button_new_with_label("Apply");
  gtk_box_pack_start(GTK_BOX(vbox), apply_button, FALSE, FALSE, 0);

  // Connect the apply button's "clicked" signal to the callback function
  g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_button_clicked), squelch_adjustment);

  // Save input adjustment and mode buttons for later retrieval
  g_object_set_data(G_OBJECT(apply_button), "input_adjustment", input_adjustment);
  g_object_set_data(G_OBJECT(apply_button), "mode_700c_button", mode_700c_button);
  g_object_set_data(G_OBJECT(apply_button), "mode_700d_button", mode_700d_button);
  g_object_set_data(G_OBJECT(apply_button), "mode_700e_button", mode_700e_button);
  g_object_set_data(G_OBJECT(apply_button), "callsign_entry", callsign_entry);
  g_object_set_data(G_OBJECT(apply_button), "grid_square_entry", grid_square_entry);


// Create a label with a hyperlink and formatted text for the GitHub link
char *github_markup = g_strdup_printf(" <a href=\"https://qso.freedv.org\"><small>FreeDV Reporter</small></a>                              <a href=\"https://github.com/SigmazGFX/FreeDV_PTT\"><small>%s W2JON  </small></a>", RELEASE_VERSION);
GtkWidget *github_label = gtk_label_new(NULL);
gtk_label_set_markup(GTK_LABEL(github_label), github_markup);
g_free(github_markup); // Free the allocated string

gtk_widget_set_halign(github_label, GTK_ALIGN_END); // Align to the bottom right corner
gtk_widget_set_valign(github_label, GTK_ALIGN_END);
gtk_box_pack_start(GTK_BOX(vbox), github_label, FALSE, FALSE, 0);

//------------


  // Show all widgets
  gtk_widget_show_all(window);
}

void send_telnet_commands(int sockfd_telnet) {
  char * commands[] = {
    "m DIGITAL",
    "LOW 900",
    "HIGH 2100",
    "PITCH 1500",
    "f 14236"
  };

  // Define delay in milliseconds (200 milliseconds = 0.2 seconds)
  struct timespec delay = {
    0,
    200 * 1000 * 1000
  }; // 200 milliseconds

  for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (send(sockfd_telnet, commands[i], strlen(commands[i]), 0) < 0) {
      perror("Send command failed");
      exit(EXIT_FAILURE);
    }
    printf("Sent: %s\n", commands[i]);
    // Sleep for 200 milliseconds between commands
    nanosleep( & delay, NULL);
  }

  // Close the socket after sending commands
  //close(sockfd_telnet);
}
void change_mode(const char * frequency) {
  // Determine mode command based on frequency band
  char * mode_command;

  if (strstr(frequency, "1997") || strstr(frequency, "3625") || strstr(frequency, "3643") || strstr(frequency, "3693") || strstr(frequency, "3697") || strstr(frequency, "3850") || strstr(frequency, "7177") || strstr(frequency, "7197")) {
    mode_command = "m LSB";
  } else {
    mode_command = "m DIGITAL";
  }

  // Format the mode command
  char command[20]; 
  sprintf(command, "%s", mode_command); 

  // Send the command
  if (send(sockfd_telnet, command, strlen(command), 0) < 0) {
    perror("Send command failed");
    exit(EXIT_FAILURE);
  }
  printf("Changing mode to: %s\n", mode_command); //  Report changing the radio mode to console

  // Define delay in milliseconds (200 milliseconds = 0.2 seconds)
  struct timespec delay = {
    0,
    200 * 1000 * 1000
  }; // 200 milliseconds

  // Sleep for 200 milliseconds between commands
  nanosleep( & delay, NULL);
}

void change_frequency(const char * frequency) {
  // Telnet command to change frequency
  // Format the command
  char command[20];
  sprintf(command, "f %s", frequency);

  // Define delay in milliseconds (200 milliseconds = 0.2 seconds)
  struct timespec delay = {
    0,
    200 * 1000 * 1000
  }; // 200 milliseconds

  // Send the command
  if (send(sockfd_telnet, command, strlen(command), 0) < 0) {
    perror("Send command failed");
    exit(EXIT_FAILURE);
  }

  printf("Changing frequency to: %s MHz\n", frequency); 

 
  // Sleep for 200 milliseconds between commands
  nanosleep( & delay, NULL);

  // Change mode based on frequency band
  change_mode(frequency);

  // Center to 1500
  char pitch_command[] = "PITCH 1500";
  if (send(sockfd_telnet, pitch_command, strlen(pitch_command), 0) < 0) {
    perror("Send PITCH command failed");
    exit(EXIT_FAILURE);
  }

  printf("Setting PITCH to 1500\n");

  // Sleep for 200 milliseconds between commands
  nanosleep( & delay, NULL);

  // Set LOW bandpass shoulder
  char low_command[] = "LOW 900";
  if (send(sockfd_telnet, low_command, strlen(low_command), 0) < 0) {
    perror("Send LOW command failed");
    exit(EXIT_FAILURE);
  }

  printf("Setting LOW to 900\n");

  // Sleep for 200 milliseconds between commands
  nanosleep( & delay, NULL);

  // Set HIGH bandpass shoulder
  char high_command[] = "HIGH 2100";
  if (send(sockfd_telnet, high_command, strlen(high_command), 0) < 0) {
    perror("Send HIGH command failed");
    exit(EXIT_FAILURE);
  }

  printf("Setting HIGH to 2100\n");

  // Sleep for 200 milliseconds between commands
  nanosleep( & delay, NULL);

}

void menu_item_selected(GtkWidget * widget, gpointer data) {
  // Get label from selected menu item
  GtkWidget * label = gtk_bin_get_child(GTK_BIN(widget));
  const gchar * full_text = gtk_label_get_text(GTK_LABEL(label));

  // Extract frequency from label text (remove " MHz" and decimal point)
  gchar frequency[10]; // Assuming max 9 characters for frequency (e.g., "14.236 MHz")
  int i = 0;
  while ( * full_text && !g_ascii_isdigit( * full_text)) {
    full_text++;
  }
  while ( * full_text && (g_ascii_isdigit( * full_text) || * full_text == '.')) {
    if ( * full_text != '.') {
      frequency[i++] = * full_text;
    }
    full_text++;
  }
  frequency[i] = '\0';

  // Change frequency
  change_frequency(frequency);

 // Send IPC command
    char command[50]; // Adjust size as needed
    int freq_khz = atoi(frequency); // Convert frequency string to integer (kHz)
    sprintf(command, "FREQ_CHANGE %d", freq_khz);
    send_ipc_command(command);

}

int main(int argc, char * argv[]) {
  GtkWidget * window;
  GtkWidget * hbox;
  GtkWidget * tx_button;
  GtkWidget * rx_button;
  
  save_release_version(RELEASE_VERSION);  
  
    const char *audio_device = "card5"; // Simplified device name for path checking
    const char *sbitx_program = "sbitx";  // Replace with your actual program name
    //const char *device = "card5";  // Replace with your actual audio device name
	
    // Check if sBitx program is running
    if (!check_program_running(sbitx_program)) {
        show_message_dialog("ERROR:\n\n                    sBitx is not running.\n\nPlease exit and start the sBitx application");
        return 1; // Exit program if sBitx is not running
    }

    // Check if audio device is present
    if (!check_audio_device(audio_device)) {
        show_message_dialog("ERROR:\n\n     plughw:CARD=5,DEV=0 not found\n\nConnect USB audio device and try again.\n");
        return 1; // Exit program if audio device is not present
    }


  // Check if the configuration file exists
  if (!config_file_exists()) {
    // Create the configuration file with default values
    create_default_config();
  }

  struct sockaddr_in servaddr_telnet, servaddr_server;

  // Create socket for telnet
  if ((sockfd_telnet = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Telnet socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Set telnet server address and port
  memset( & servaddr_telnet, 0, sizeof(servaddr_telnet));
  servaddr_telnet.sin_family = AF_INET;
  servaddr_telnet.sin_addr.s_addr = inet_addr(SERVER_IP);
  servaddr_telnet.sin_port = htons(TELNET_PORT);

  // Connect to telnet server
  if (connect(sockfd_telnet, (struct sockaddr * ) & servaddr_telnet, sizeof(servaddr_telnet)) < 0) {
    perror("Telnet connection failed");
    exit(EXIT_FAILURE);
  }

  // Send telnet commands
  send_telnet_commands(sockfd_telnet);

  // Create socket for Hamlib net server
  if ((sockfd_server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Server socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Set Hamlib net server address and port
  memset( & servaddr_server, 0, sizeof(servaddr_server));
  servaddr_server.sin_family = AF_INET;
  servaddr_server.sin_addr.s_addr = inet_addr(SERVER_IP);
  servaddr_server.sin_port = htons(SERVER_PORT);

  // Connect to Hamlib net server
  if (connect(sockfd_server, (struct sockaddr * ) & servaddr_server, sizeof(servaddr_server)) < 0) {
    perror("Server connection failed");
    exit(EXIT_FAILURE);
  }
  
  // Print_environment_variables();// Was only used as diagnostic tool
  
  // Set up signal handling to clean up child process on exit
  signal(SIGINT, handle_termination);
  signal(SIGTERM, handle_termination);
  // Start the Python script to handle socket.io communications
  start_python_script();
  
  // Initialize GTK
  gtk_init( & argc, & argv);

  // Create the main window
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(window, "destroy", G_CALLBACK(on_window_closed), NULL);

  // Set window title
  gtk_window_set_title(GTK_WINDOW(window), "FreeDV 700D PTT");

  // Create a horizontal box layout
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_container_add(GTK_CONTAINER(window), hbox);

  // Create a header bar
  GtkWidget * header_bar = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "sBitx FreeDV_PTT");

  // Create a Settings button in the header bar
  GtkWidget * settings_button = gtk_button_new_with_label("Settings");
  g_signal_connect(settings_button, "clicked", G_CALLBACK(open_codec_settings_window), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), settings_button);

  // Create a dropdown menu with the specified options
  GtkWidget * menu_button = gtk_menu_button_new();
  GtkWidget * menu = gtk_menu_new();

  // Function to add a group of menu items
  void add_menu_group(GtkWidget * menu,
    const char * group_name,
      const char * options[], int num_options) {
    GtkWidget * group_label = gtk_menu_item_new_with_label(group_name);
    gtk_widget_set_sensitive(group_label, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), group_label);
    gtk_widget_show(group_label);

    for (int i = 0; i < num_options; i++) {
      GtkWidget * menu_item = gtk_menu_item_new_with_label(options[i]);
      g_signal_connect(menu_item, "activate", G_CALLBACK(menu_item_selected), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
      gtk_widget_show(menu_item);
    }
  }

  // Add menu items (sBitx doesnt really support 160 or 60 meters)
  // const char *options_160_meters[] = {"1.997 MHz"};
  // add_menu_group(menu, "160 Meters", options_160_meters, 1);

  const char * options_80_meters[] = {
    "3.625 MHz",
    "3.643 MHz",
    "3.693 MHz",
    "3.697 MHz",
    "3.850 MHz"
  };
  add_menu_group(menu, "80 Meters", options_80_meters, 5);

  // const char *options_60_meters[] = {"5.4035 MHz", "5.3665 MHz", "5.3685 MHz"};
  // add_menu_group(menu, "60 Meters", options_60_meters, 3);

  const char * options_40_meters[] = {
    "7.177 MHz",
    "7.197 MHz"
  };
  add_menu_group(menu, "40 Meters", options_40_meters, 2);

  const char * options_20_meters[] = {
    "14.236 MHz",
    "14.240 MHz"
  };
  add_menu_group(menu, "20 Meters", options_20_meters, 2);

  const char * options_17_meters[] = {
    "18.118 MHz"
  };
  add_menu_group(menu, "17 Meters", options_17_meters, 1);

  const char * options_15_meters[] = {
    "21.313 MHz"
  };
  add_menu_group(menu, "15 Meters", options_15_meters, 1);

  const char * options_12_meters[] = {
    "24.933 MHz"
  };
  add_menu_group(menu, "12 Meters", options_12_meters, 1);

  const char * options_10_meters[] = {
    "28.330 MHz",
    "28.720 MHz"
  };
  add_menu_group(menu, "10 Meters", options_10_meters, 2);

  gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), menu_button);

  // Set the header bar as titlebar
  gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

  // Create TX button
  tx_button = gtk_button_new_with_label("TX");
  gtk_widget_set_size_request(tx_button, 150, 50); // Set button size to 150x50
  g_signal_connect(tx_button, "clicked", G_CALLBACK(on_tx_button_clicked), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), tx_button, TRUE, TRUE, 5);

  // Create RX button
  rx_button = gtk_button_new_with_label("RX");
  gtk_widget_set_size_request(rx_button, 150, 50); // Set button size to 150x50
  g_signal_connect(rx_button, "clicked", G_CALLBACK(on_rx_button_clicked), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), rx_button, TRUE, TRUE, 5);

  // Show all widgets
  gtk_widget_show_all(window);
    		
  // Start GTK main loop
  gtk_main();
  
  // Clean up before exiting (kill python instance)
  handle_termination(0);
	
  return 0;
}
