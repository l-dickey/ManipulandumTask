import os
import csv
import time
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, simpledialog, messagebox, font
from PIL import Image, ImageTk
import serial
import serial.tools.list_ports

# === Serial Setup ===
# def find_serial_port():
#     ports = serial.tools.list_ports.comports()
#     for p in ports:
#         if 'USB' in p.description or 'Serial' in p.description:
#             return p.device
#     return None

# serial_port = find_serial_port()
# if serial_port is None:
#     raise RuntimeError("No serial port found")
serial_port = 'COM7'

ser = serial.Serial(serial_port, 115200, timeout=1)
print(f"Connected to {serial_port}")

SUBJECT_FILE = "subjects.txt"

def load_subjects():
    if not os.path.exists(SUBJECT_FILE):
        with open(SUBJECT_FILE, "w"): pass
    with open(SUBJECT_FILE, "r") as f:
        return [line.strip() for line in f if line.strip()]

def add_subject():
    new_id = simpledialog.askstring("New Subject", "Enter new subject ID:")
    if new_id and new_id.strip():
        new_id = new_id.strip()
        if new_id not in subjects:
            with open(SUBJECT_FILE, "a") as f:
                f.write(f"{new_id}\n")
            os.makedirs(os.path.join("data", new_id), exist_ok=True)
            subjects.append(new_id)
            subject_box['values'] = subjects
            subject_var.set(new_id)
        else:
            messagebox.showinfo("Info", f"Subject '{new_id}' already exists.")

subjects = load_subjects()

# === GUI Setup ===
root = tk.Tk()
root.title("ESP32 Trial Configuration")
root.geometry("950x950")  # Larger window for more content
root.configure(bg="#f0f6fc")  # Light blue background for a fresh look

# Try to load the icon
try:
    root.iconbitmap("logo1.ico")
except:
    print("Could not load logo1.ico")

# Custom colors
PRIMARY_COLOR = "#2563eb"  # Vibrant blue
SECONDARY_COLOR = "#10b981"  # Emerald green
BG_COLOR = "#f0f6fc"  # Light blue background
CARD_BG = "#ffffff"  # White card background
TEXT_COLOR = "#1e293b"  # Dark slate for text
LIGHT_TEXT = "#64748b"  # Lighter text for secondary info
BORDER_COLOR = "#cbd5e1"  # Light border color
ACCENT_COLOR = "#8b5cf6"  # Purple accent

# Custom fonts
heading_font = font.Font(family="Segoe UI", size=18, weight="bold")
subheading_font = font.Font(family="Segoe UI", size=14, weight="bold")
label_font = font.Font(family="Segoe UI", size=11)
button_font = font.Font(family="Segoe UI", size=11, weight="bold")
info_font = font.Font(family="Segoe UI", size=9)

# Create a frame for the header with the logo and title
header_frame = tk.Frame(root, bg=BG_COLOR, pady=15)
header_frame.pack(fill=tk.X)

# Load and display logo in the header
try:
    img = Image.open("logo1.ico")
    img = img.resize((60, 60))
    logo = ImageTk.PhotoImage(img)
    logo_label = tk.Label(header_frame, image=logo, bg=BG_COLOR)
    logo_label.image = logo
    logo_label.pack(side=tk.LEFT, padx=20)
except Exception as e:
    print(f"Could not load logo1.ico as image: {e}")
    try:
        # Create a placeholder if icon cannot be loaded as image
        placeholder = tk.Label(header_frame, text="LOGO", font=heading_font, bg=BG_COLOR, fg=PRIMARY_COLOR)
        placeholder.pack(side=tk.LEFT, padx=20)
    except:
        print("Failed to create logo placeholder")

# Add title next to the logo
title_label = tk.Label(header_frame, text="ESP32 Trial Configuration", 
                      font=heading_font, bg=BG_COLOR, fg=PRIMARY_COLOR)
title_label.pack(side=tk.LEFT, padx=10)

# Configure ttk styles
style = ttk.Style()
style.theme_use('clam')  # Using clam theme for a more modern look

# Configure button styles
style.configure("Primary.TButton", 
               font=button_font,
               background=PRIMARY_COLOR,
               foreground="white")
style.map("Primary.TButton", 
         background=[("active", "#1d4ed8"), ("pressed", "#1e40af")],
         foreground=[("active", "white")])

style.configure("Success.TButton", 
               font=button_font,
               background=SECONDARY_COLOR,
               foreground="white")
style.map("Success.TButton", 
         background=[("active", "#059669"), ("pressed", "#047857")],
         foreground=[("active", "white")])

style.configure("Accent.TButton", 
               font=button_font,
               background=ACCENT_COLOR,
               foreground="white")
style.map("Accent.TButton", 
         background=[("active", "#7c3aed"), ("pressed", "#6d28d9")],
         foreground=[("active", "white")])

# Configure combobox and entry styles
style.configure("TCombobox", padding=8, font=label_font)
style.configure("TEntry", padding=8, font=label_font)

# Configure progressbar style
style.configure("TProgressbar", 
               thickness=20,
               background=SECONDARY_COLOR)

# Create a container frame for all content
container_frame = tk.Frame(root, bg=BG_COLOR, padx=25, pady=20)
container_frame.pack(fill=tk.BOTH, expand=True)

# Create a card-like frame for subject selection
subject_frame = tk.Frame(container_frame, bg=CARD_BG, padx=20, pady=20,
                        highlightbackground=BORDER_COLOR, highlightthickness=1)
subject_frame.pack(fill=tk.X, padx=10, pady=10)

# Subject selection heading
subject_heading = tk.Label(subject_frame, text="Subject Selection", font=subheading_font, bg=CARD_BG, fg=PRIMARY_COLOR)
subject_heading.pack(anchor="w", pady=(0, 15))

# Subject selection fields
subject_field_frame = tk.Frame(subject_frame, bg=CARD_BG)
subject_field_frame.pack(fill=tk.X)

tk.Label(subject_field_frame, text="Subject ID:", font=label_font, bg=CARD_BG).grid(row=0, column=0, sticky="e", padx=10, pady=12)
subject_var = tk.StringVar()
subject_box = ttk.Combobox(subject_field_frame, textvariable=subject_var, values=subjects, state="readonly", width=20)
subject_box.grid(row=0, column=1, padx=10, pady=12, sticky="w")

add_subject_btn = ttk.Button(
    subject_field_frame, 
    text="Add New Subject", 
    command=add_subject, 
    style="Primary.TButton"
)
add_subject_btn.grid(row=0, column=2, padx=10, pady=12, sticky="w")

# Create a card-like frame for trial configuration
config_frame = tk.Frame(container_frame, bg=CARD_BG, padx=20, pady=20,
                       highlightbackground=BORDER_COLOR, highlightthickness=1)
config_frame.pack(fill=tk.X, padx=10, pady=10)

# Trial configuration heading
config_heading = tk.Label(config_frame, text="Trial Configuration", font=subheading_font, bg=CARD_BG, fg=PRIMARY_COLOR)
config_heading.pack(anchor="w", pady=(0, 15))

# Create a 2-column layout for configuration options
config_options_frame = tk.Frame(config_frame, bg=CARD_BG)
config_options_frame.pack(fill=tk.X)

# Left column
left_column = tk.Frame(config_options_frame, bg=CARD_BG)
left_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

tk.Label(left_column, text="Number of Trials:", font=label_font, bg=CARD_BG).grid(row=0, column=0, sticky="e", padx=10, pady=12)
trial_var = tk.StringVar(value="5")
trial_entry = ttk.Entry(left_column, textvariable=trial_var, width=15)
trial_entry.grid(row=0, column=1, padx=10, pady=12, sticky="w")

tk.Label(left_column, text="Reward Color:", font=label_font, bg=CARD_BG).grid(row=1, column=0, sticky="e", padx=10, pady=12)
color_var = tk.StringVar(value="Green")
color_box = ttk.Combobox(left_column, textvariable=color_var, values=["Green", "Purple"], state="readonly", width=15)
color_box.grid(row=1, column=1, padx=10, pady=12, sticky="w")

# Right column
right_column = tk.Frame(config_options_frame, bg=CARD_BG)
right_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

tk.Label(right_column, text="Position Mode:", font=label_font, bg=CARD_BG).grid(row=0, column=0, sticky="e", padx=10, pady=12)
mode_var = tk.StringVar(value="Fixed")
mode_box = ttk.Combobox(right_column, textvariable=mode_var, values=["Fixed", "Random"], state="readonly", width=15)
mode_box.grid(row=0, column=1, padx=10, pady=12, sticky="w")

tk.Label(right_column, text="Fixed Side:", font=label_font, bg=CARD_BG).grid(row=1, column=0, sticky="e", padx=10, pady=12)
side_var = tk.StringVar(value="Push")
side_box = ttk.Combobox(right_column, textvariable=side_var, values=["Push", "Pull"], state="readonly", width=15)
side_box.grid(row=1, column=1, padx=10, pady=12, sticky="w")

def update_side_state(*args):
    if mode_var.get().lower().startswith("random"):
        side_box.configure(state="disabled")
    else:
        side_box.configure(state="readonly")

mode_var.trace_add("write", update_side_state)
update_side_state()

# Create a card-like frame for progress monitoring
progress_frame = tk.Frame(container_frame, bg=CARD_BG, padx=20, pady=20,
                         highlightbackground=BORDER_COLOR, highlightthickness=1)
progress_frame.pack(fill=tk.X, padx=10, pady=10)

# Progress monitoring heading
progress_heading = tk.Label(progress_frame, text="Trial Progress", font=subheading_font, bg=CARD_BG, fg=PRIMARY_COLOR)
progress_heading.pack(anchor="w", pady=(0, 15))

# Progress bar
progress_var = tk.DoubleVar(value=0.0)
progress_bar = ttk.Progressbar(progress_frame, variable=progress_var, style="TProgressbar", length=650)
progress_bar.pack(fill=tk.X, pady=10)

# Progress statistics
stats_frame = tk.Frame(progress_frame, bg=CARD_BG)
stats_frame.pack(fill=tk.X, pady=10)

# Trial counter
trial_counter_frame = tk.Frame(stats_frame, bg=CARD_BG)
trial_counter_frame.pack(side=tk.LEFT, padx=20)

tk.Label(trial_counter_frame, text="Current Trial:", font=label_font, bg=CARD_BG).pack(side=tk.LEFT)
current_trial_var = tk.StringVar(value="0")
tk.Label(trial_counter_frame, textvariable=current_trial_var, font=label_font, bg=CARD_BG, fg=SECONDARY_COLOR, width=3).pack(side=tk.LEFT, padx=5)

tk.Label(trial_counter_frame, text="of", font=label_font, bg=CARD_BG).pack(side=tk.LEFT)
total_trials_var = tk.StringVar(value="0")
tk.Label(trial_counter_frame, textvariable=total_trials_var, font=label_font, bg=CARD_BG, fg=SECONDARY_COLOR, width=3).pack(side=tk.LEFT, padx=5)

# Timer
timer_frame = tk.Frame(stats_frame, bg=CARD_BG)
timer_frame.pack(side=tk.RIGHT, padx=20)

tk.Label(timer_frame, text="Elapsed Time:", font=label_font, bg=CARD_BG).pack(side=tk.LEFT)
timer_var = tk.StringVar(value="00:00")
tk.Label(timer_frame, textvariable=timer_var, font=label_font, bg=CARD_BG, fg=PRIMARY_COLOR).pack(side=tk.LEFT, padx=5)

# Create a frame for action buttons and status
button_frame = tk.Frame(container_frame, bg=BG_COLOR, pady=10)
button_frame.pack(fill=tk.X, pady=10)

# Action buttons
action_buttons_frame = tk.Frame(button_frame, bg=BG_COLOR)
action_buttons_frame.pack()

# Flag to track if trials are running
is_running = False
timer_thread = None
stop_event = threading.Event()

def reset_progress():
    """Reset all progress indicators"""
    progress_var.set(0)
    current_trial_var.set("0")
    timer_var.set("00:00")
    status_label.config(text="Ready to configure ESP32", fg=LIGHT_TEXT)

def update_timer():
    """Update the timer every second"""
    start_time = time.time()
    while not stop_event.is_set():
        if is_running:
            elapsed = int(time.time() - start_time)
            minutes = elapsed // 60
            seconds = elapsed % 60
            timer_var.set(f"{minutes:02d}:{seconds:02d}")
        time.sleep(1)

def send_config():
    global is_running, timer_thread, stop_event
    
    # Check if already running
    if is_running:
        messagebox.showinfo("Info", "A trial is already in progress")
        return
        
    trials = trial_var.get()
    subject_id = subject_var.get().strip() or "unknown"
    color_val = color_var.get().strip().lower()
    mode_val = mode_var.get().strip().lower()
    side_val = side_var.get().strip().lower()

    # Input validation
    if not trials.isdigit() or int(trials) <= 0:
        messagebox.showerror("Error", "Number of trials must be a positive number")
        return
        
    if not subject_id:
        messagebox.showerror("Error", "Please select a subject ID")
        return

    # Reset any previous progress
    reset_progress()
    stop_event.clear()  # Clear any previous stop event
    
    total_trials = int(trials)
    total_trials_var.set(str(total_trials))

    color = 'G' if color_val.startswith('g') else 'P'
    mode = 'X' if mode_val.startswith('r') else 'F'
    side = side_val[0].upper() if mode == 'F' else None

    config_str = f"TRIALS={trials};COLOR={color};MODE={mode}"
    if side:
        config_str += f";SIDE={side}"
    config_str += "\n"

    # Show sending feedback with animation
    status_label.config(text="Sending configuration...", fg=PRIMARY_COLOR)
    root.update()
    
    print(f"Sending: {config_str.strip()}")
    ser.write(config_str.encode())
    time.sleep(0.5)  # Simulate sending delay
    
    status_label.config(text=f"Configuration sent for subject: {subject_id}", fg=SECONDARY_COLOR)

    # CSV Logging
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    subject_folder = os.path.join("data", subject_id)
    os.makedirs(subject_folder, exist_ok=True)
    csv_filename = os.path.join(subject_folder, f"log_{subject_id}_{timestamp}.csv")
    
    # Start a new thread for the timer
    is_running = True
    if timer_thread is None or not timer_thread.is_alive():
        timer_thread = threading.Thread(target=update_timer, daemon=True)
        timer_thread.start()
    
    # Start a new thread for data logging to avoid blocking the GUI
    logging_thread = threading.Thread(
        target=log_data,
        args=(subject_id, csv_filename, total_trials),
        daemon=True
    )
    logging_thread.start()

def log_data(subject_id, csv_filename, total_trials):
    global is_running, stop_event
    
    try:
        csv_file = open(csv_filename, mode='w', newline='')

        csv_file.write(f"# Subject: {subject_id}\n")
        csv_file.write(f"# Trials: {total_trials}\n")
        csv_file.write(f"# Color: {color_var.get()}\n")
        csv_file.write(f"# Mode: {mode_var.get()}\n")
        csv_file.write(f"# Side: {side_var.get() if mode_var.get().lower().startswith('f') else 'N/A'}\n")
        csv_file.write(f"# Timestamp: {datetime.now().isoformat()}\n")
        csv_file.write("# ----------------------------------------\n")

        csv_writer = csv.DictWriter(csv_file, fieldnames=[
            'subject', 'trial', 'event_type', 'event_time_us', 'color', 'mode', 'side'
        ])
        csv_writer.writeheader()

        current_trial_config = {
            "subject": subject_id,
            "color": color_var.get(),
            "mode": mode_var.get(),
            "side": side_var.get() if mode_var.get().lower().startswith("f") else "N/A"
        }

        print(f"Logging to {csv_filename}")
        print("Waiting for ESP32 event stream...")
        
        # Update status to show logging is in progress
        status_label.config(text=f"Trial in progress - logging data...", fg=PRIMARY_COLOR)
        
        start_time = time.time()
        timeout = 60
        current_trial = 0
        last_trial = 0

        while not stop_event.is_set():
            line = ser.readline()
            if line:
                line = line.decode(errors='ignore').strip()
                print("ESP32:", line)
                if line.startswith("EVENT"):
                    parts = line.split(",")
                    if len(parts) >= 4:
                        try:
                            # Extract trial number
                            current_trial = int(parts[2])
                            
                            # Only update UI if trial number changed
                            if current_trial != last_trial:
                                last_trial = current_trial
                                
                                # Update trial counter and progress bar
                                progress_percentage = min(100, (current_trial / total_trials) * 100)
                                
                                # Use root.after to update UI from the main thread
                                root.after(0, lambda: current_trial_var.set(str(current_trial)))
                                root.after(0, lambda: progress_var.set(progress_percentage))
                            
                            event = {
                                "subject": current_trial_config["subject"],
                                "trial": parts[2],
                                "event_type": parts[1],
                                "event_type": parts[1],
                                "event_time_us": parts[-1],
                                "color": current_trial_config["color"],
                                "mode": current_trial_config["mode"],
                                "side": current_trial_config["side"]
                            }
                            csv_writer.writerow(event)
                            csv_file.flush()
                            
                            # Check if we've completed all trials
                            if current_trial >= total_trials:
                                root.after(0, lambda: status_label.config(
                                    text="Trial complete! All data logged successfully.", 
                                    fg=SECONDARY_COLOR
                                ))
                                print("All trials completed.")
                                break
                                
                        except (ValueError, IndexError) as e:
                            print(f"Error parsing trial data: {e}")
                            
                start_time = time.time()
            else:
                if time.time() - start_time > timeout:
                    print("No new data received — closing log.")
                    root.after(0, lambda: status_label.config(
                        text="Logging complete - no new data received", 
                        fg=SECONDARY_COLOR
                    ))
                    break
                time.sleep(0.1)
                
    except Exception as e:
        root.after(0, lambda: status_label.config(text=f"Error: {str(e)}", fg="red"))
        print(f"Error in data logging: {e}")
    finally:
        if 'csv_file' in locals():
            csv_file.close()
        is_running = False

def stop_trials():
    """Stop the current trial"""
    global is_running, stop_event
    if is_running:
        stop_event.set()
        is_running = False
        status_label.config(text="Trial stopped by user", fg="#e11d48")  # Red text
        
        # Send stop command to ESP32
        ser.write(b"STOP\n")
        print("Stop command sent to ESP32")

# Send button with improved styling
send_button = ttk.Button(
    action_buttons_frame, 
    text="Start Trials", 
    command=send_config, 
    style="Success.TButton"
)
send_button.pack(side=tk.LEFT, padx=10)

# Stop button
stop_button = ttk.Button(
    action_buttons_frame, 
    text="Stop Trials", 
    command=stop_trials, 
    style="Accent.TButton"
)
stop_button.pack(side=tk.LEFT, padx=10)

# Status label for feedback
status_frame = tk.Frame(container_frame, bg=CARD_BG, padx=20, pady=15,
                       highlightbackground=BORDER_COLOR, highlightthickness=1)
status_frame.pack(fill=tk.X, padx=10, pady=10)

status_label = tk.Label(
    status_frame, 
    text="Ready to configure ESP32", 
    font=label_font, 
    bg=CARD_BG, 
    fg=LIGHT_TEXT,
    wraplength=650
)
status_label.pack(pady=5)

# Footer with connection status and version info
footer_frame = tk.Frame(root, bg=BG_COLOR, pady=5)
footer_frame.pack(fill=tk.X, side=tk.BOTTOM)

# Connection indicator
connection_frame = tk.Frame(footer_frame, bg=BG_COLOR)
connection_frame.pack(side=tk.LEFT, padx=15)

connection_indicator = tk.Canvas(connection_frame, width=10, height=10, bg=BG_COLOR, highlightthickness=0)
connection_indicator.create_oval(2, 2, 10, 10, fill=SECONDARY_COLOR, outline="")
connection_indicator.pack(side=tk.LEFT, padx=(0, 5))

connection_text = tk.Label(
    connection_frame, 
    text=f"Connected to {serial_port}", 
    font=info_font, 
    bg=BG_COLOR, 
    fg=LIGHT_TEXT
)
connection_text.pack(side=tk.LEFT)

# Version info
version_label = tk.Label(footer_frame, text="v1.1.0", font=info_font, bg=BG_COLOR, fg=LIGHT_TEXT)
version_label.pack(side=tk.RIGHT, padx=15)

# Set initial UI state
reset_progress()

root.mainloop()