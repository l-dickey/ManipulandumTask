import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import csv
import json
import time
from datetime import datetime
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import pandas as pd
import os

class BehavioralTaskLogger:
    def __init__(self, root):
        self.root = root
        self.root.title("Behavioral Task Data Logger")
        self.root.geometry("1200x800")
        
        # Serial connection
        self.serial_connection = None
        self.is_connected = False
        self.is_logging = False
        
        # Data storage
        self.trial_data = []
        self.current_subject = "SUBJ001"
        self.session_start_time = None
        self.trial_counter = 0
        self.correct_trials = 0
        
        # Threading
        self.data_queue = queue.Queue()
        self.serial_thread = None
        
        # File paths
        self.data_directory = "behavioral_data"
        if not os.path.exists(self.data_directory):
            os.makedirs(self.data_directory)
            
        self.setup_ui()
        self.start_data_processor()
        
    def setup_ui(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(3, weight=1)
        
        # Connection Frame
        conn_frame = ttk.LabelFrame(main_frame, text="Connection", padding="10")
        conn_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        
        ttk.Label(conn_frame, text="COM Port:").grid(row=0, column=0, sticky=tk.W)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=15)
        self.port_combo.grid(row=0, column=1, padx=(5, 10))
        
        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=(0, 10))
        
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=3)
        
        self.status_label = ttk.Label(conn_frame, text="Disconnected", foreground="red")
        self.status_label.grid(row=0, column=4, padx=(10, 0))
        
        # Subject Info Frame
        subject_frame = ttk.LabelFrame(main_frame, text="Subject Information", padding="10")
        subject_frame.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        
        ttk.Label(subject_frame, text="Subject ID:").grid(row=0, column=0, sticky=tk.W)
        self.subject_var = tk.StringVar(value=self.current_subject)
        subject_entry = ttk.Entry(subject_frame, textvariable=self.subject_var, width=15)
        subject_entry.grid(row=0, column=1, padx=(5, 10))
        
        ttk.Button(subject_frame, text="New Session", command=self.start_new_session).grid(row=0, column=2, padx=(0, 10))
        
        self.logging_btn = ttk.Button(subject_frame, text="Start Logging", command=self.toggle_logging, state="disabled")
        self.logging_btn.grid(row=0, column=3)
        
        # Session Stats Frame
        stats_frame = ttk.LabelFrame(main_frame, text="Session Statistics", padding="10")
        stats_frame.grid(row=2, column=0, sticky=(tk.W, tk.E, tk.N), padx=(0, 5))
        
        self.trial_count_label = ttk.Label(stats_frame, text="Trials: 0")
        self.trial_count_label.grid(row=0, column=0, sticky=tk.W)
        
        self.correct_count_label = ttk.Label(stats_frame, text="Correct: 0")
        self.correct_count_label.grid(row=1, column=0, sticky=tk.W)
        
        self.success_rate_label = ttk.Label(stats_frame, text="Success Rate: 0%")
        self.success_rate_label.grid(row=2, column=0, sticky=tk.W)
        
        self.session_time_label = ttk.Label(stats_frame, text="Session Time: 00:00:00")
        self.session_time_label.grid(row=3, column=0, sticky=tk.W)
        
        # Control buttons
        ttk.Button(stats_frame, text="Save Data", command=self.save_data).grid(row=4, column=0, pady=(10, 0), sticky=tk.W)
        ttk.Button(stats_frame, text="Load Data", command=self.load_data).grid(row=5, column=0, pady=(5, 0), sticky=tk.W)
        ttk.Button(stats_frame, text="Clear Session", command=self.clear_session).grid(row=6, column=0, pady=(5, 0), sticky=tk.W)
        
        # Data display frame
        data_frame = ttk.LabelFrame(main_frame, text="Trial Data", padding="10")
        data_frame.grid(row=2, column=1, rowspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), padx=(5, 0))
        data_frame.columnconfigure(0, weight=1)
        data_frame.rowconfigure(0, weight=1)
        
        # Create treeview for data display
        columns = ("Trial", "Outcome", "Reaction Time", "Encoder Pos", "Timestamp")
        self.tree = ttk.Treeview(data_frame, columns=columns, show="headings", height=15)
        
        for col in columns:
            self.tree.heading(col, text=col)
            self.tree.column(col, width=100)
        
        scrollbar = ttk.Scrollbar(data_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        
        self.tree.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        scrollbar.grid(row=0, column=1, sticky=(tk.N, tk.S))
        
        # Performance plot frame
        plot_frame = ttk.LabelFrame(main_frame, text="Performance Plot", padding="10")
        plot_frame.grid(row=3, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), padx=(0, 5))
        
        self.fig, self.ax = plt.subplots(figsize=(6, 3))
        self.canvas = FigureCanvasTkAgg(self.fig, plot_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        self.refresh_ports()
        
    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.set(ports[0])
    
    def toggle_connection(self):
        if not self.is_connected:
            self.connect_serial()
        else:
            self.disconnect_serial()
    
    def connect_serial(self):
        try:
            port = self.port_var.get()
            if not port:
                messagebox.showerror("Error", "Please select a COM port")
                return
                
            self.serial_connection = serial.Serial(port, 115200, timeout=1)
            self.is_connected = True
            
            self.connect_btn.config(text="Disconnect")
            self.status_label.config(text="Connected", foreground="green")
            self.logging_btn.config(state="normal")
            
            # Start serial reading thread
            self.serial_thread = threading.Thread(target=self.read_serial_data, daemon=True)
            self.serial_thread.start()
            
        except Exception as e:
            messagebox.showerror("Connection Error", f"Failed to connect: {str(e)}")
    
    def disconnect_serial(self):
        self.is_connected = False
        self.is_logging = False
        
        if self.serial_connection:
            self.serial_connection.close()
            self.serial_connection = None
        
        self.connect_btn.config(text="Connect")
        self.status_label.config(text="Disconnected", foreground="red")
        self.logging_btn.config(text="Start Logging", state="disabled")
    
    def toggle_logging(self):
        if not self.is_logging:
            self.start_logging()
        else:
            self.stop_logging()
    
    def start_logging(self):
        self.is_logging = True
        self.logging_btn.config(text="Stop Logging")
        if not self.session_start_time:
            self.session_start_time = time.time()
    
    def stop_logging(self):
        self.is_logging = False
        self.logging_btn.config(text="Start Logging")
    
    def start_new_session(self):
        if self.trial_data and messagebox.askyesno("New Session", "Current session data will be lost. Continue?"):
            self.clear_session()
        
        self.current_subject = self.subject_var.get()
        self.session_start_time = time.time()
        self.trial_counter = 0
        self.correct_trials = 0
        self.trial_data = []
        self.update_display()
    
    def clear_session(self):
        self.trial_data = []
        self.trial_counter = 0
        self.correct_trials = 0
        self.session_start_time = None
        self.is_logging = False
        self.logging_btn.config(text="Start Logging")
        self.update_display()
        
        # Clear treeview
        for item in self.tree.get_children():
            self.tree.delete(item)
    
    def read_serial_data(self):
        while self.is_connected:
            try:
                if self.serial_connection and self.serial_connection.in_waiting:
                    line = self.serial_connection.readline().decode('utf-8').strip()
                    if line and self.is_logging:
                        self.data_queue.put(line)
            except Exception as e:
                print(f"Serial read error: {e}")
                break
    
    def process_serial_data(self, data_line):
        # Parse the incoming data from ESP32
        # Expected format: "TRIAL,outcome,reaction_time,encoder_pos"
        try:
            parts = data_line.split(',')
            if len(parts) >= 4 and parts[0] == "TRIAL":
                outcome = parts[1]
                reaction_time = int(parts[2])
                encoder_pos = int(parts[3])
                
                self.trial_counter += 1
                if outcome == "CORRECT":
                    self.correct_trials += 1
                
                trial_data = {
                    'trial_number': self.trial_counter,
                    'subject_id': self.current_subject,
                    'outcome': outcome,
                    'reaction_time': reaction_time,
                    'encoder_position': encoder_pos,
                    'timestamp': datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                }
                
                self.trial_data.append(trial_data)
                self.update_display()
                self.add_trial_to_tree(trial_data)
                self.update_plot()
                
        except Exception as e:
            print(f"Data parsing error: {e}")
    
    def add_trial_to_tree(self, trial_data):
        self.tree.insert("", tk.END, values=(
            trial_data['trial_number'],
            trial_data['outcome'],
            f"{trial_data['reaction_time']}ms",
            trial_data['encoder_position'],
            trial_data['timestamp']
        ))
        # Scroll to bottom
        self.tree.see(self.tree.get_children()[-1])
    
    def update_display(self):
        self.trial_count_label.config(text=f"Trials: {self.trial_counter}")
        self.correct_count_label.config(text=f"Correct: {self.correct_trials}")
        
        if self.trial_counter > 0:
            success_rate = (self.correct_trials / self.trial_counter) * 100
            self.success_rate_label.config(text=f"Success Rate: {success_rate:.1f}%")
        else:
            self.success_rate_label.config(text="Success Rate: 0%")
        
        if self.session_start_time:
            elapsed = time.time() - self.session_start_time
            hours, remainder = divmod(elapsed, 3600)
            minutes, seconds = divmod(remainder, 60)
            self.session_time_label.config(text=f"Session Time: {int(hours):02d}:{int(minutes):02d}:{int(seconds):02d}")
    
    def update_plot(self):
        if len(self.trial_data) < 2:
            return
        
        # Calculate rolling success rate
        window_size = min(10, len(self.trial_data))
        success_rates = []
        trial_numbers = []
        
        for i in range(window_size - 1, len(self.trial_data)):
            window_data = self.trial_data[i - window_size + 1:i + 1]
            correct_in_window = sum(1 for trial in window_data if trial['outcome'] == 'CORRECT')
            success_rate = (correct_in_window / window_size) * 100
            success_rates.append(success_rate)
            trial_numbers.append(i + 1)
        
        self.ax.clear()
        self.ax.plot(trial_numbers, success_rates, 'b-', linewidth=2)
        self.ax.set_xlabel('Trial Number')
        self.ax.set_ylabel('Success Rate (%)')
        self.ax.set_title(f'Rolling Success Rate (window={window_size})')
        self.ax.grid(True, alpha=0.3)
        self.ax.set_ylim(0, 100)
        
        self.canvas.draw()
    
    def save_data(self):
        if not self.trial_data:
            messagebox.showwarning("No Data", "No trial data to save")
            return
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{self.current_subject}_{timestamp}.csv"
        filepath = os.path.join(self.data_directory, filename)
        
        try:
            with open(filepath, 'w', newline='') as csvfile:
                fieldnames = ['trial_number', 'subject_id', 'outcome', 'reaction_time', 'encoder_position', 'timestamp']
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(self.trial_data)
            
            messagebox.showinfo("Data Saved", f"Data saved to {filename}")
            
        except Exception as e:
            messagebox.showerror("Save Error", f"Failed to save data: {str(e)}")
    
    def load_data(self):
        filepath = filedialog.askopenfilename(
            title="Load Trial Data",
            initialdir=self.data_directory,
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
        )
        
        if not filepath:
            return
        
        try:
            with open(filepath, 'r') as csvfile:
                reader = csv.DictReader(csvfile)
                loaded_data = list(reader)
            
            if loaded_data:
                self.trial_data = loaded_data
                self.trial_counter = len(loaded_data)
                self.correct_trials = sum(1 for trial in loaded_data if trial['outcome'] == 'CORRECT')
                self.current_subject = loaded_data[0]['subject_id']
                self.subject_var.set(self.current_subject)
                
                # Update display
                self.update_display()
                
                # Clear and repopulate treeview
                for item in self.tree.get_children():
                    self.tree.delete(item)
                
                for trial in loaded_data:
                    self.add_trial_to_tree(trial)
                
                self.update_plot()
                messagebox.showinfo("Data Loaded", f"Loaded {len(loaded_data)} trials")
            
        except Exception as e:
            messagebox.showerror("Load Error", f"Failed to load data: {str(e)}")
    
    def start_data_processor(self):
        def process_queue():
            try:
                while True:
                    data = self.data_queue.get_nowait()
                    self.process_serial_data(data)
                    self.data_queue.task_done()
            except queue.Empty:
                pass
            finally:
                self.root.after(100, process_queue)
        
        process_queue()

if __name__ == "__main__":
    root = tk.Tk()
    app = BehavioralTaskLogger(root)
    root.mainloop()