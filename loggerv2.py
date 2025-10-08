"""
Behavioral Task Data Logger - Nature Journal Style
Requirements: pip install pyserial matplotlib
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, font
import serial
import serial.tools.list_ports
import threading
import queue
import csv
import os
from datetime import datetime
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

class BehavioralDataLogger:
    def __init__(self, root):
        self.root = root
        self.root.title("Behavioral Task Data Acquisition System")
        self.root.geometry("1600x1000")
        
        # Nature journal professional color scheme
        self.colors = {
            'bg': '#FFFFFF',            # Pure white background
            'bg_recording': '#FFE5E5',  # Light red background when recording
            'panel': '#FAFAFA',         # Light grey panels
            'panel_recording': '#FFF0F0', # Light pink panels when recording
            'primary': '#0D47A1',       # Nature blue (deep professional blue)
            'secondary': '#1565C0',     # Lighter blue
            'accent': '#D32F2F',        # Nature red (for recording/important)
            'success': '#2E7D32',       # Professional green
            'text': '#212121',          # Almost black
            'text_secondary': '#757575', # Grey text
            'border': '#E0E0E0',        # Light border
            'grid': '#EEEEEE',          # Very light grid
            'chart_line': '#0D47A1',    # Blue line
            'chart_fill': '#E3F2FD'     # Light blue fill
        }
        
        self.root.configure(bg=self.colors['bg'])
        
        # Professional typography (Nature uses Harding, we'll use similar)
        self.title_font = font.Font(family="Helvetica", size=24, weight="bold")
        self.subtitle_font = font.Font(family="Helvetica", size=11)
        self.heading_font = font.Font(family="Helvetica", size=12, weight="bold")
        self.subheading_font = font.Font(family="Helvetica", size=10, weight="bold")
        self.body_font = font.Font(family="Helvetica", size=10)
        self.mono_font = font.Font(family="Courier New", size=9)
        self.stat_font = font.Font(family="Helvetica", size=36, weight="bold")
        
        # Configure custom styles
        self.configure_styles()
        
        # Data storage
        self.subjects = []
        self.trials = []
        self.session_stats = {'total': 0, 'correct': 0, 'success_rate': 0.0}
        self.performance_data = []
        self.timestamped_notes = []
        self.current_session_date = ""
        self.data_dir = Path("behavioral_data")
        self.data_dir.mkdir(exist_ok=True)
        
        # Serial communication
        self.serial_port = None
        self.is_recording = False
        self.serial_thread = None
        self.data_queue = queue.Queue()
        
        # Store references to widgets that need color updates
        self.bg_widgets = []  # Widgets with bg color
        self.panel_widgets = []  # Widgets with panel color
        
        # Load existing subjects
        self.load_subjects()
        
        # Create GUI
        self.create_widgets()
        
        # Start queue checker
        self.check_queue()
    
    def configure_styles(self):
        """Configure ttk styles for Nature journal aesthetic"""
        style = ttk.Style()
        style.theme_use('clam')
        
        # Minimal, clean frame style
        style.configure('Card.TFrame', 
                       background=self.colors['panel'],
                       relief='flat')
        
        style.configure('Title.TLabel',
                       background=self.colors['bg'],
                       foreground=self.colors['primary'],
                       font=self.title_font)
        
        style.configure('Body.TLabel',
                       background=self.colors['panel'],
                       foreground=self.colors['text'],
                       font=self.body_font)
    
    def load_subjects(self):
        """Load existing subject folders"""
        if self.data_dir.exists():
            self.subjects = [d.name for d in self.data_dir.iterdir() if d.is_dir()]
            self.subjects.sort()
    
    def create_section(self, parent, title, subtitle=None):
        """Create a clean section with title"""
        frame = tk.Frame(parent, bg=self.colors['panel'])
        self.panel_widgets.append(frame)
        
        header_frame = tk.Frame(frame, bg=self.colors['panel'])
        header_frame.pack(fill='x', pady=(0, 15))
        self.panel_widgets.append(header_frame)
        
        title_label = tk.Label(header_frame, text=title,
                              font=self.heading_font,
                              fg=self.colors['text'],
                              bg=self.colors['panel'],
                              anchor='w')
        title_label.pack(side='left')
        self.panel_widgets.append(title_label)
        
        if subtitle:
            sub_label = tk.Label(header_frame, text=subtitle,
                                font=self.body_font,
                                fg=self.colors['text_secondary'],
                                bg=self.colors['panel'])
            sub_label.pack(side='left', padx=(15, 0))
            self.panel_widgets.append(sub_label)
        
        # Thin separator line (Nature style)
        separator = tk.Frame(frame, height=1, bg=self.colors['border'])
        separator.pack(fill='x', pady=(0, 20))
        
        content = tk.Frame(frame, bg=self.colors['panel'])
        content.pack(fill='both', expand=True)
        self.panel_widgets.append(content)
        
        return frame, content
    
    def create_widgets(self):
        # Main container
        main = tk.Frame(self.root, bg=self.colors['bg'])
        main.pack(fill=tk.BOTH, expand=True)
        self.bg_widgets.append(main)
        
        # Header bar with branding
        header = tk.Frame(main, bg=self.colors['bg'], height=100)
        header.pack(fill='x', padx=40, pady=(30, 20))
        self.bg_widgets.append(header)
        
        # Logo/Title area
        logo_frame = tk.Frame(header, bg=self.colors['bg'])
        logo_frame.pack(side='left')
        self.bg_widgets.append(logo_frame)
        
        title = tk.Label(logo_frame, 
                        text="Behavioral Data Acquisition",
                        font=self.title_font,
                        fg=self.colors['primary'],
                        bg=self.colors['bg'])
        title.pack(anchor='w')
        self.bg_widgets.append(title)
        
        subtitle = tk.Label(logo_frame,
                           text="Neural Interface Research System  •  Real-time Performance Monitoring",
                           font=self.subtitle_font,
                           fg=self.colors['text_secondary'],
                           bg=self.colors['bg'])
        subtitle.pack(anchor='w', pady=(5, 0))
        self.bg_widgets.append(subtitle)
        
        # Status indicator in header
        status_frame = tk.Frame(header, bg=self.colors['bg'])
        status_frame.pack(side='right', padx=20)
        self.bg_widgets.append(status_frame)
        
        self.header_status = tk.Label(status_frame,
                                      text="STANDBY",
                                      font=self.subheading_font,
                                      fg=self.colors['text_secondary'],
                                      bg=self.colors['bg'])
        self.header_status.pack()
        self.bg_widgets.append(self.header_status)
        
        # Thin top border
        tk.Frame(main, height=1, bg=self.colors['border']).pack(fill='x', padx=40)
        
        # Main content area - THREE COLUMN LAYOUT
        content = tk.Frame(main, bg=self.colors['bg'])
        content.pack(fill=tk.BOTH, expand=True, padx=40, pady=30)
        self.bg_widgets.append(content)
        
        # Left sidebar - Subject & Connection (25% width)
        left_sidebar = tk.Frame(content, bg=self.colors['panel'], width=350)
        left_sidebar.pack(side='left', fill='both', padx=(0, 15))
        left_sidebar.pack_propagate(False)
        self.panel_widgets.append(left_sidebar)
        
        # Middle area - Main data display (50% width)
        middle_area = tk.Frame(content, bg=self.colors['bg'])
        middle_area.pack(side='left', fill='both', expand=True, padx=(0, 15))
        self.bg_widgets.append(middle_area)
        
        # Right sidebar - Notes (25% width)
        right_sidebar = tk.Frame(content, bg=self.colors['panel'], width=350)
        right_sidebar.pack(side='right', fill='both')
        right_sidebar.pack_propagate(False)
        self.panel_widgets.append(right_sidebar)
        
        # === LEFT SIDEBAR - CONTROLS ===
        
        # Subject Management
        subj_frame, subj_content = self.create_section(left_sidebar, "Subject", "Experimental subject identification")
        subj_frame.pack(fill='x', padx=20, pady=(20, 0))
        
        tk.Label(subj_content, text="Subject ID",
                font=self.body_font, fg=self.colors['text'],
                bg=self.colors['panel']).pack(anchor='w', pady=(0, 8))
        
        entry_row = tk.Frame(subj_content, bg=self.colors['panel'])
        entry_row.pack(fill='x', pady=(0, 15))
        
        self.new_subject_entry = tk.Entry(entry_row, font=self.body_font,
                                          relief='solid', borderwidth=1,
                                          highlightbackground=self.colors['border'],
                                          highlightcolor=self.colors['primary'],
                                          highlightthickness=1)
        self.new_subject_entry.pack(side='left', fill='x', expand=True, ipady=8)
        self.new_subject_entry.bind('<Return>', lambda e: self.add_subject())
        
        add_btn = tk.Button(entry_row, text="ADD",
                           command=self.add_subject,
                           bg=self.colors['primary'], fg='white',
                           font=self.subheading_font, relief='flat',
                           padx=20, cursor='hand2',
                           activebackground=self.colors['secondary'])
        add_btn.pack(side='left', padx=(10, 0), ipady=8)
        
        tk.Label(subj_content, text="Active Subject",
                font=self.body_font, fg=self.colors['text'],
                bg=self.colors['panel']).pack(anchor='w', pady=(0, 8))
        
        self.subject_var = tk.StringVar()
        self.subject_combo = ttk.Combobox(subj_content, textvariable=self.subject_var,
                                         values=self.subjects, font=self.body_font,
                                         state='readonly', height=8)
        self.subject_combo.pack(fill='x', ipady=8)
        
        # Weight entry
        tk.Label(subj_content, text="Subject Weight (g)",
                font=self.body_font, fg=self.colors['text'],
                bg=self.colors['panel']).pack(anchor='w', pady=(15, 8))
        
        self.weight_entry = tk.Entry(subj_content, font=self.body_font,
                                     relief='solid', borderwidth=1,
                                     highlightbackground=self.colors['border'],
                                     highlightcolor=self.colors['primary'],
                                     highlightthickness=1)
        self.weight_entry.pack(fill='x', ipady=8)
        
        # Connection
        conn_frame, conn_content = self.create_section(left_sidebar, "Connection", "Serial interface configuration")
        conn_frame.pack(fill='x', padx=20, pady=(30, 0))
        
        tk.Label(conn_content, text="Serial Port",
                font=self.body_font, fg=self.colors['text'],
                bg=self.colors['panel']).pack(anchor='w', pady=(0, 8))
        
        port_row = tk.Frame(conn_content, bg=self.colors['panel'])
        port_row.pack(fill='x', pady=(0, 15))
        
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var,
                                      font=self.body_font, state='readonly')
        self.port_combo.pack(side='left', fill='x', expand=True, ipady=8)
        self.refresh_ports()
        
        refresh_btn = tk.Button(port_row, text="⟳",
                               command=self.refresh_ports,
                               bg=self.colors['panel'], fg=self.colors['primary'],
                               font=('Helvetica', 14), relief='solid',
                               borderwidth=1, width=3, cursor='hand2',
                               activebackground=self.colors['grid'])
        refresh_btn.pack(side='left', padx=(10, 0))
        
        connect_btn = tk.Button(conn_content, text="CONNECT",
                               command=self.connect_serial,
                               bg=self.colors['primary'], fg='white',
                               font=self.subheading_font, relief='flat',
                               cursor='hand2', height=2,
                               activebackground=self.colors['secondary'])
        connect_btn.pack(fill='x', ipady=5)
        
        self.connection_label = tk.Label(conn_content, text="Disconnected",
                                        font=self.body_font,
                                        fg=self.colors['text_secondary'],
                                        bg=self.colors['panel'])
        self.connection_label.pack(pady=(10, 0))
        
        # Session Control
        ctrl_frame, ctrl_content = self.create_section(left_sidebar, "Session Control", "Acquisition management")
        ctrl_frame.pack(fill='x', padx=20, pady=(30, 0))
        
        self.start_btn = tk.Button(ctrl_content, text="START ACQUISITION",
                                   command=self.start_session,
                                   bg=self.colors['success'], fg='white',
                                   font=self.heading_font, relief='flat',
                                   cursor='hand2', state='disabled',
                                   disabledforeground='#BDBDBD',
                                   activebackground='#388E3C', height=2)
        self.start_btn.pack(fill='x', pady=(0, 10), ipady=8)
        
        self.stop_btn = tk.Button(ctrl_content, text="STOP ACQUISITION",
                                  command=self.stop_session,
                                  bg=self.colors['accent'], fg='white',
                                  font=self.heading_font, relief='flat',
                                  cursor='hand2', state='disabled',
                                  disabledforeground='#BDBDBD',
                                  activebackground='#E53935', height=2)
        self.stop_btn.pack(fill='x', pady=(0, 10), ipady=8)
        
        self.save_btn = tk.Button(ctrl_content, text="EXPORT DATA",
                                  command=self.save_session,
                                  bg=self.colors['text'], fg='white',
                                  font=self.heading_font, relief='flat',
                                  cursor='hand2', state='disabled',
                                  disabledforeground='#BDBDBD',
                                  activebackground='#424242', height=2)
        self.save_btn.pack(fill='x', ipady=8)
        
        self.recording_label = tk.Label(ctrl_content, text="",
                                       font=self.heading_font,
                                       bg=self.colors['panel'])
        self.recording_label.pack(pady=(20, 0))
        
        # === RIGHT SIDEBAR - NOTES ===
        
        # Session Notes
        notes_frame, notes_content = self.create_section(right_sidebar, "Session Notes", "General observations")
        notes_frame.pack(fill='x', padx=20, pady=(20, 0))
        
        self.notes_text = scrolledtext.ScrolledText(notes_content,
                                                    font=self.body_font,
                                                    relief='solid', borderwidth=1,
                                                    highlightbackground=self.colors['border'],
                                                    highlightcolor=self.colors['primary'],
                                                    highlightthickness=1,
                                                    wrap='word', height=8)
        self.notes_text.pack(fill='x')
        
        # Timestamped Notes
        tstamp_frame, tstamp_content = self.create_section(right_sidebar, "Event Log", "Timestamped observations")
        tstamp_frame.pack(fill='both', expand=True, padx=20, pady=(30, 20))
        
        # Note entry area
        note_input_frame = tk.Frame(tstamp_content, bg=self.colors['panel'])
        note_input_frame.pack(fill='x', pady=(0, 10))
        
        self.timestamped_note_entry = tk.Entry(note_input_frame, font=self.body_font,
                                               relief='solid', borderwidth=1,
                                               highlightbackground=self.colors['border'],
                                               highlightcolor=self.colors['primary'],
                                               highlightthickness=1)
        self.timestamped_note_entry.pack(side='left', fill='x', expand=True, ipady=8)
        self.timestamped_note_entry.bind('<Return>', lambda e: self.add_timestamped_note())
        
        add_note_btn = tk.Button(note_input_frame, text="LOG",
                                command=self.add_timestamped_note,
                                bg=self.colors['primary'], fg='white',
                                font=self.subheading_font, relief='flat',
                                padx=15, cursor='hand2',
                                activebackground=self.colors['secondary'])
        add_note_btn.pack(side='left', padx=(10, 0), ipady=8)
        
        # Notes display area
        self.timestamped_notes_display = scrolledtext.ScrolledText(tstamp_content,
                                                                   font=self.body_font,
                                                                   relief='solid', borderwidth=1,
                                                                   highlightbackground=self.colors['border'],
                                                                   highlightthickness=1,
                                                                   wrap='word', state='disabled')
        self.timestamped_notes_display.pack(fill='both', expand=True)
        
        # === MIDDLE AREA - DATA DISPLAY ===
        
        # Statistics Cards
        stats_container = tk.Frame(middle_area, bg=self.colors['bg'])
        stats_container.pack(fill='x', pady=(0, 25))
        
        stat_cards = []
        stat_data = [
            ("Total Trials", "n", self.colors['primary']),
            ("Correct Responses", "n", self.colors['success']),
            ("Success Rate", "%", self.colors['accent'])
        ]
        
        for i, (label, unit, color) in enumerate(stat_data):
            card = tk.Frame(stats_container, bg='white',
                           relief='solid', borderwidth=1,
                           highlightbackground=self.colors['border'],
                           highlightthickness=1)
            card.pack(side='left', fill='both', expand=True, padx=(0 if i == 0 else 10, 0))
            
            # Color indicator bar at top
            tk.Frame(card, height=4, bg=color).pack(fill='x')
            
            # Content
            content_frame = tk.Frame(card, bg='white')
            content_frame.pack(fill='both', expand=True, padx=30, pady=25)
            
            value_label = tk.Label(content_frame, text="0",
                                  font=self.stat_font,
                                  fg=color, bg='white')
            value_label.pack()
            
            unit_label = tk.Label(content_frame, text=unit,
                                 font=self.body_font,
                                 fg=self.colors['text_secondary'], bg='white')
            unit_label.pack()
            
            text_label = tk.Label(content_frame, text=label,
                                 font=self.subheading_font,
                                 fg=self.colors['text'], bg='white')
            text_label.pack(pady=(10, 0))
            
            stat_cards.append(value_label)
        
        self.total_label, self.correct_label, self.success_label = stat_cards
        
        # Performance Chart
        chart_frame = tk.Frame(middle_area, bg='white',
                              relief='solid', borderwidth=1,
                              highlightbackground=self.colors['border'],
                              highlightthickness=1)
        chart_frame.pack(fill='both', expand=True, pady=(0, 25))
        
        # Chart header
        chart_header = tk.Frame(chart_frame, bg='white')
        chart_header.pack(fill='x', padx=30, pady=(25, 15))
        
        tk.Label(chart_header, text="Performance Dynamics",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Label(chart_header, text="Real-time success rate monitoring",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left', padx=(15, 0))
        
        # Separator
        tk.Frame(chart_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=30)
        
        # Chart area
        chart_content = tk.Frame(chart_frame, bg='white')
        chart_content.pack(fill='both', expand=True, padx=20, pady=20)
        
        self.fig = Figure(figsize=(10, 4), dpi=100, facecolor='white')
        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('white')
        self.ax.set_xlabel('Trial Number', fontfamily='Helvetica', fontsize=11, color=self.colors['text'])
        self.ax.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=11, color=self.colors['text'])
        self.ax.set_ylim(0, 105)
        self.ax.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5, alpha=1)
        self.ax.set_axisbelow(True)
        
        # Clean spines
        for spine in ['top', 'right']:
            self.ax.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax.spines[spine].set_color(self.colors['border'])
            self.ax.spines[spine].set_linewidth(1)
        
        self.ax.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas = FigureCanvasTkAgg(self.fig, master=chart_content)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Trial Data Table
        table_frame = tk.Frame(middle_area, bg='white',
                              relief='solid', borderwidth=1,
                              highlightbackground=self.colors['border'],
                              highlightthickness=1)
        table_frame.pack(fill='both', expand=True)
        
        # Table header
        table_header = tk.Frame(table_frame, bg='white')
        table_header.pack(fill='x', padx=30, pady=(25, 15))
        
        tk.Label(table_header, text="Trial Log",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Label(table_header, text="Most recent experimental trials",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left', padx=(15, 0))
        
        # Separator
        tk.Frame(table_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=30)
        
        # Table content
        table_content = tk.Frame(table_frame, bg='white')
        table_content.pack(fill='both', expand=True, padx=20, pady=20)
        
        # Custom treeview
        style = ttk.Style()
        style.configure("Nature.Treeview",
                       background="white",
                       foreground=self.colors['text'],
                       fieldbackground="white",
                       font=self.mono_font,
                       rowheight=30,
                       borderwidth=0)
        style.configure("Nature.Treeview.Heading",
                       background=self.colors['panel'],
                       foreground=self.colors['text'],
                       font=self.subheading_font,
                       borderwidth=1,
                       relief='flat')
        style.map('Nature.Treeview',
                 background=[('selected', self.colors['chart_fill'])],
                 foreground=[('selected', self.colors['text'])])
        
        columns = ('Trial', 'Outcome', 'RT (ms)', 'Position', 'Timestamp')
        self.tree = ttk.Treeview(table_content, columns=columns, show='headings',
                                height=10, style="Nature.Treeview")
        
        col_widths = {'Trial': 80, 'Outcome': 120, 'RT (ms)': 100, 'Position': 100, 'Timestamp': 120}
        for col in columns:
            self.tree.heading(col, text=col)
            self.tree.column(col, width=col_widths.get(col, 100), anchor='center')
        
        scrollbar = ttk.Scrollbar(table_content, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscroll=scrollbar.set)
        
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
    
    def set_recording_background(self, recording):
        """Change background colors based on recording state"""
        if recording:
            bg_color = self.colors['bg_recording']
            panel_color = self.colors['panel_recording']
        else:
            bg_color = self.colors['bg']
            panel_color = self.colors['panel']
        
        # Update main background
        self.root.config(bg=bg_color)
        
        # Update all bg widgets
        for widget in self.bg_widgets:
            try:
                widget.config(bg=bg_color)
            except:
                pass
        
        # Update all panel widgets
        for widget in self.panel_widgets:
            try:
                widget.config(bg=panel_color)
            except:
                pass
    
    def add_timestamped_note(self):
        """Add a timestamped note to the log"""
        note_text = self.timestamped_note_entry.get().strip()
        if not note_text:
            return
        
        timestamp = datetime.now().strftime('%H:%M:%S')
        trial_num = len(self.trials) if self.is_recording else "—"
        
        note_entry = {
            'timestamp': datetime.now().isoformat(),
            'time_display': timestamp,
            'trial': trial_num,
            'note': note_text
        }
        
        self.timestamped_notes.append(note_entry)
        
        # Update display
        self.timestamped_notes_display.config(state='normal')
        display_text = f"[{timestamp}] Trial {trial_num}: {note_text}\n"
        self.timestamped_notes_display.insert('1.0', display_text)
        self.timestamped_notes_display.config(state='disabled')
        
        # Clear entry
        self.timestamped_note_entry.delete(0, tk.END)
    
    def refresh_ports(self):
        """Refresh available COM ports"""
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)
    
    def add_subject(self):
        """Add a new subject"""
        subject_name = self.new_subject_entry.get().strip()
        if not subject_name:
            return
        
        if subject_name in self.subjects:
            messagebox.showwarning("Duplicate Subject", "This subject ID already exists in the database.")
            return
        
        subject_dir = self.data_dir / subject_name
        subject_dir.mkdir(exist_ok=True)
        
        self.subjects.append(subject_name)
        self.subjects.sort()
        self.subject_combo['values'] = self.subjects
        self.subject_combo.set(subject_name)
        self.new_subject_entry.delete(0, tk.END)
    
    def connect_serial(self):
        """Connect to serial port"""
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Connection Error", "Please select a serial port.")
            return
        
        try:
            self.serial_port = serial.Serial(port, 115200, timeout=1)
            self.connection_label.config(text=f"Connected • {port}",
                                        foreground=self.colors['success'])
            self.header_status.config(text="CONNECTED", fg=self.colors['success'])
            self.start_btn.config(state='normal', bg=self.colors['success'])
            
            self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.serial_thread.start()
            
        except Exception as e:
            messagebox.showerror("Connection Error", f"Failed to establish serial connection:\n\n{str(e)}")
    
    def read_serial(self):
        """Read data from serial port"""
        buffer = ""
        while self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting:
                    data = self.serial_port.read(self.serial_port.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line.startswith('TRIAL,'):
                            self.data_queue.put(line)
            except Exception as e:
                print(f"Serial read error: {e}")
                break
    
    def check_queue(self):
        """Check for new data in queue"""
        try:
            while True:
                line = self.data_queue.get_nowait()
                if self.is_recording:
                    self.process_trial_data(line)
        except queue.Empty:
            pass
        
        self.root.after(100, self.check_queue)
    
    def process_trial_data(self, line):
        """Process incoming trial data"""
        parts = line.split(',')
        if len(parts) == 4 and parts[0] == 'TRIAL':
            trial = {
                'timestamp': datetime.now().isoformat(),
                'trial_number': len(self.trials) + 1,
                'outcome': parts[1],
                'reaction_time': int(parts[2]),
                'encoder_position': int(parts[3])
            }
            
            self.trials.append(trial)
            self.update_stats(trial)
            self.update_table(trial)
    
    def update_stats(self, trial):
        """Update session statistics"""
        self.session_stats['total'] += 1
        if trial['outcome'] == 'CORRECT':
            self.session_stats['correct'] += 1
        
        success_rate = (self.session_stats['correct'] / self.session_stats['total']) * 100
        self.session_stats['success_rate'] = success_rate
        
        self.total_label.config(text=str(self.session_stats['total']))
        self.correct_label.config(text=str(self.session_stats['correct']))
        self.success_label.config(text=f"{success_rate:.1f}")
        
        self.performance_data.append({
            'trial': self.session_stats['total'],
            'success_rate': success_rate
        })
        self.update_plot()
    
    def update_plot(self):
        """Update performance plot with Nature journal style"""
        if not self.performance_data:
            return
        
        trials = [d['trial'] for d in self.performance_data[-50:]]
        rates = [d['success_rate'] for d in self.performance_data[-50:]]
        
        self.ax.clear()
        
        # Fill area under curve (Nature style)
        self.ax.fill_between(trials, rates, alpha=0.2, color=self.colors['chart_line'])
        
        # Main line
        self.ax.plot(trials, rates, color=self.colors['chart_line'],
                    linewidth=2, marker='o', markersize=5,
                    markerfacecolor='white', markeredgewidth=2,
                    markeredgecolor=self.colors['chart_line'], zorder=3)
        
        self.ax.set_xlabel('Trial Number', fontfamily='Helvetica', fontsize=11, color=self.colors['text'])
        self.ax.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=11, color=self.colors['text'])
        self.ax.set_ylim(0, 105)
        self.ax.set_facecolor('white')
        self.ax.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5, alpha=1, zorder=0)
        self.ax.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax.spines[spine].set_color(self.colors['border'])
            self.ax.spines[spine].set_linewidth(1)
        
        self.ax.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas.draw()
    
    def update_table(self, trial):
        """Update recent trials table"""
        time_str = datetime.fromisoformat(trial['timestamp']).strftime('%H:%M:%S')
        
        tag = 'correct' if trial['outcome'] == 'CORRECT' else 'timeout'
        self.tree.insert('', 0, values=(
            trial['trial_number'],
            trial['outcome'],
            trial['reaction_time'],
            trial['encoder_position'],
            time_str
        ), tags=(tag,))
        
        # Subtle color coding
        self.tree.tag_configure('correct', background='#F1F8F4')
        self.tree.tag_configure('timeout', background='#FFF4F4')
        
        children = self.tree.get_children()
        if len(children) > 50:
            self.tree.delete(children[-1])
    
    def start_session(self):
        """Start recording session"""
        if not self.subject_var.get():
            messagebox.showerror("Configuration Error", "Please select an experimental subject.")
            return
        
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showerror("Connection Error", "Serial connection not established.")
            return
        
        self.trials = []
        self.timestamped_notes = []
        self.session_stats = {'total': 0, 'correct': 0, 'success_rate': 0.0}
        self.performance_data = []
        
        for item in self.tree.get_children():
            self.tree.delete(item)
        
        # Clear timestamped notes display
        self.timestamped_notes_display.config(state='normal')
        self.timestamped_notes_display.delete('1.0', tk.END)
        self.timestamped_notes_display.config(state='disabled')
        
        self.ax.clear()
        self.ax.set_facecolor('white')
        self.ax.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5)
        self.ax.set_axisbelow(True)
        for spine in ['top', 'right']:
            self.ax.spines[spine].set_visible(False)
        self.canvas.draw()
        
        self.total_label.config(text="0")
        self.correct_label.config(text="0")
        self.success_label.config(text="0.0")
        
        self.current_session_date = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
        
        self.is_recording = True
        self.set_recording_background(True)  # Turn background red
        self.recording_label.config(text=f"● RECORDING\n{self.subject_var.get()}",
                                   foreground=self.colors['accent'])
        self.header_status.config(text="ACQUIRING DATA", fg=self.colors['accent'])
        
        self.start_btn.config(state='disabled', bg='#BDBDBD')
        self.stop_btn.config(state='normal', bg=self.colors['accent'])
    
    def stop_session(self):
        """Stop recording session"""
        self.is_recording = False
        self.set_recording_background(False)  # Turn background white
        self.recording_label.config(text="")
        self.header_status.config(text="CONNECTED", fg=self.colors['success'])
        self.start_btn.config(state='normal', bg=self.colors['success'])
        self.stop_btn.config(state='disabled', bg='#BDBDBD')
        self.save_btn.config(state='normal', bg=self.colors['text'])
    
    def save_session(self):
        """Save session data to CSV"""
        if not self.trials:
            messagebox.showwarning("Export Warning", "No trial data available for export.")
            return
        
        subject = self.subject_var.get()
        weight = self.weight_entry.get().strip()
        subject_dir = self.data_dir / subject
        filename = subject_dir / f"{subject}_{self.current_session_date}.csv"
        
        try:
            with open(filename, 'w', newline='') as f:
                writer = csv.writer(f)
                
                # Trial data header
                writer.writerow(['Trial', 'Timestamp', 'Outcome', 'ReactionTime_ms', 'EncoderPosition'])
                
                # Trial data
                for trial in self.trials:
                    writer.writerow([
                        trial['trial_number'],
                        trial['timestamp'],
                        trial['outcome'],
                        trial['reaction_time'],
                        trial['encoder_position']
                    ])
                
                # Metadata section
                writer.writerow([])
                writer.writerow(['# Experimental Metadata'])
                writer.writerow([f'# Subject: {subject}'])
                writer.writerow([f'# Subject Weight: {weight if weight else "Not recorded"} g'])
                writer.writerow([f'# Session Date: {self.current_session_date}'])
                writer.writerow([f'# Total Trials: {self.session_stats["total"]}'])
                writer.writerow([f'# Correct Responses: {self.session_stats["correct"]}'])
                writer.writerow([f'# Success Rate: {self.session_stats["success_rate"]:.1f}%'])
                
                # Session notes
                writer.writerow([])
                writer.writerow(['# Session Notes'])
                session_notes = self.notes_text.get("1.0", tk.END).strip()
                if session_notes:
                    for line in session_notes.split('\n'):
                        writer.writerow([f'# {line}'])
                else:
                    writer.writerow(['# No session notes recorded'])
                
                # Timestamped notes
                if self.timestamped_notes:
                    writer.writerow([])
                    writer.writerow(['# Timestamped Event Log'])
                    writer.writerow(['Timestamp', 'Trial', 'Note'])
                    for note in self.timestamped_notes:
                        writer.writerow([
                            note['timestamp'],
                            note['trial'],
                            note['note']
                        ])
            
            messagebox.showinfo("Export Complete", 
                              f"Data successfully exported to:\n\n{filename}\n\n"
                              f"Total trials: {self.session_stats['total']}\n"
                              f"Success rate: {self.session_stats['success_rate']:.1f}%")
            self.save_btn.config(state='disabled', bg='#BDBDBD')
            
        except Exception as e:
            messagebox.showerror("Export Error", f"Failed to export data:\n\n{str(e)}")
    
    def __del__(self):
        """Cleanup on exit"""
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()

if __name__ == "__main__":
    root = tk.Tk()
    app = BehavioralDataLogger(root)
    root.mainloop()