import tkinter as tk
from tkinter import ttk, simpledialog, messagebox
import threading
import serial
import csv
import os
from datetime import datetime

LOGS_DIR = "logs"
SERIAL_PORT = "COM8"
BAUD_RATE = 115200
ENCODER_TASK_PERIOD = 10  # ms

class BehaviorLoggerGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Behavior Logger")
        self.geometry("400x200")
        
        # Ensure logs directory exists
        os.makedirs(LOGS_DIR, exist_ok=True)

        # Subject selection
        tk.Label(self, text="Select Subject:").pack(pady=(10, 0))
        self.subject_var = tk.StringVar()
        self.subject_menu = ttk.Combobox(self, textvariable=self.subject_var, state="readonly")
        self.subject_menu['values'] = self.get_subject_list()
        self.subject_menu.pack(pady=5)
        
        # Add new subject
        tk.Button(self, text="Add Subject", command=self.add_subject).pack(pady=5)
        
        # Start/Stop buttons
        btn_frame = tk.Frame(self)
        btn_frame.pack(pady=10)
        self.start_btn = tk.Button(btn_frame, text="Start Logging", command=self.start_logging)
        self.start_btn.grid(row=0, column=0, padx=5)
        self.stop_btn = tk.Button(btn_frame, text="Stop Logging", command=self.stop_logging, state="disabled")
        self.stop_btn.grid(row=0, column=1, padx=5)
        
        self.status_var = tk.StringVar(value="Idle")
        tk.Label(self, textvariable=self.status_var).pack(pady=(5,0))
        
        self.logging_thread = None
        self.stop_event = threading.Event()
    
    def get_subject_list(self):
        return [d for d in os.listdir(LOGS_DIR) if os.path.isdir(os.path.join(LOGS_DIR, d))]
    
    def add_subject(self):
        name = simpledialog.askstring("New Subject", "Enter new subject ID:")
        if name:
            subj_dir = os.path.join(LOGS_DIR, name)
            try:
                os.makedirs(subj_dir, exist_ok=False)
                self.subject_menu['values'] = self.get_subject_list()
                self.subject_var.set(name)
            except FileExistsError:
                messagebox.showerror("Error", f"Subject '{name}' already exists.")
    
    def start_logging(self):
        subj = self.subject_var.get()
        if not subj:
            messagebox.showwarning("Select Subject", "Please select or add a subject before starting.")
            return
        # Prepare file
        now = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = os.path.join(LOGS_DIR, subj, f"behavior_log_{now}.csv")
        self.log_file = open(filename, 'w', newline='', buffering=1)
        self.writer = csv.writer(self.log_file)
        self.writer.writerow(['line_type','timestamp_ms','encoder_value','event_type','trial_number'])
        
        # Open serial
        self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        self.stop_event.clear()
        self.logging_thread = threading.Thread(target=self._log_loop, daemon=True)
        self.logging_thread.start()
        
        self.status_var.set(f"Logging to {filename}")
        self.start_btn.config(state="disabled")
        self.stop_btn.config(state="normal")
    
    def stop_logging(self):
        self.stop_event.set()
        if self.logging_thread is not None:
            self.logging_thread.join()
        self.ser.close()
        self.log_file.close()
        self.status_var.set("Idle")
        self.start_btn.config(state="normal")
        self.stop_btn.config(state="disabled")
    
    def _log_loop(self):
        while not self.stop_event.is_set():
            raw = self.ser.readline().decode('ascii', errors='ignore').strip()
            if not raw:
                continue
            parts = raw.split(',')
            if parts[0] == 'EVENT' and len(parts) == 4:
                _, ev_type, trial_str, t_us = parts
                t_ms = int(t_us) // 1000
                self.writer.writerow(['EVENT', t_ms, '', ev_type, trial_str])
            elif len(parts) == 2:
                t, val = parts
                self.writer.writerow(['DATA', t, val, '', ''])
        # end loop

if __name__ == "__main__":
    app = BehaviorLoggerGUI()
    app.mainloop()
