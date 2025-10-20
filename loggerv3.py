"""
Behavioral Task Data Logger - Enhanced with Penalty State Support
Requirements: pip install pyserial matplotlib numpy
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, font, filedialog
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
import numpy as np

class BehavioralDataLogger:
    def __init__(self, root):
        self.root = root
        self.root.title("Behavioral Task Data Acquisition System")
        self.root.geometry("2000x1080")
        self.root.minsize(1700, 900)
        
        # Colors
        self.colors = {
            'bg': '#FFFFFF',
            'bg_recording': '#FFE5E5',
            'panel': '#FAFAFA',
            'panel_recording': '#FFF0F0',
            'primary': '#0D47A1',
            'secondary': '#1565C0',
            'accent': '#D32F2F',
            'success': '#2E7D32',
            'warning': '#F57C00',  # NEW: For early/penalty trials
            'text': '#212121',
            'text_secondary': '#757575',
            'border': '#E0E0E0',
            'grid': '#EEEEEE',
            'chart_line': '#0D47A1',
            'chart_fill': '#E3F2FD',
            'reward_colors': ["#FF7700", '#FFA726', '#FFD54F'],
            'comparison_color': '#9E9E9E',
            'subject_colors': ['#1976D2', '#388E3C', '#D32F2F', '#F57C00', '#7B1FA2', '#0097A7']
        }
        
        self.root.configure(bg=self.colors['bg'])
        
        # Typography
        self.title_font = font.Font(family="Helvetica", size=24, weight="bold")
        self.subtitle_font = font.Font(family="Helvetica", size=11)
        self.heading_font = font.Font(family="Helvetica", size=12, weight="bold")
        self.subheading_font = font.Font(family="Helvetica", size=10, weight="bold")
        self.body_font = font.Font(family="Helvetica", size=10)
        self.mono_font = font.Font(family="Courier New", size=9)
        self.stat_font = font.Font(family="Helvetica", size=36, weight="bold")
        self.small_stat_font = font.Font(family="Helvetica", size=20, weight="bold")
        
        # Configure styles
        self.configure_styles()
        
        # Real-time parameters
        self.params = {
            'window_size': tk.IntVar(value=20),
            'autosave_interval': tk.IntVar(value=50),
            'autosave_enabled': tk.BooleanVar(value=False)
        }
        
        # Water volumes per reward level (µL)
        self.water_volumes = {0: 4, 1: 8, 2: 12}
        
        # Data storage - UPDATED with early trials
        self.subjects = []
        self.trials = []
        self.session_stats = {
            'total': 0, 
            'correct': 0,
            'early': 0,  # NEW: Track early trials
            'timeout': 0,  # NEW: Track timeout trials
            'success_rate': 0.0,
            'by_reward': {0: {'total': 0, 'correct': 0}, 
                         1: {'total': 0, 'correct': 0},
                         2: {'total': 0, 'correct': 0}},
            'current_streak': 0,
            'streak_type': None,
            'water_consumed': 0
        }
        self.performance_data = []
        self.comparison_sessions = []
        self.timestamped_notes = []
        self.current_session_date = ""
        self.data_dir = Path("behavioral_data")
        self.data_dir.mkdir(exist_ok=True)
        
        # Multi-subject data
        self.subject_sessions = {}
        
        # Serial communication
        self.serial_port = None
        self.is_recording = False
        self.serial_thread = None
        self.data_queue = queue.Queue()
        
        # Autosave tracking
        self.last_autosave_trial = 0
        
        # Widget references
        self.bg_widgets = []
        self.panel_widgets = []
        
        # Load data
        self.load_subjects()
        
        # Create GUI
        self.create_widgets()
        
        # Start queue checker
        self.check_queue()
    
    def configure_styles(self):
        """Configure ttk styles"""
        style = ttk.Style()
        style.theme_use('clam')
        
        style.configure('Card.TFrame', 
                       background=self.colors['panel'],
                       relief='flat')
        
        # Notebook styling
        style.configure('TNotebook', background=self.colors['bg'], borderwidth=0)
        style.configure('TNotebook.Tab', padding=[20, 10], font=self.subheading_font)
        
        # PanedWindow styling
        style.configure('TPanedwindow', background=self.colors['bg'])
        style.configure('Sash', sashthickness=5, sashrelief='flat', background=self.colors['border'])
        
        # Treeview
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
        
        # Header bar
        header = tk.Frame(main, bg=self.colors['bg'], height=100)
        header.pack(fill='x', padx=40, pady=(30, 20))
        self.bg_widgets.append(header)
        
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
                           text="Neural Interface Research  •  Penalty State Tracking  •  Water Monitoring  •  Multi-Subject Analysis",
                           font=self.subtitle_font,
                           fg=self.colors['text_secondary'],
                           bg=self.colors['bg'])
        subtitle.pack(anchor='w', pady=(5, 0))
        self.bg_widgets.append(subtitle)
        
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
        
        tk.Frame(main, height=1, bg=self.colors['border']).pack(fill='x', padx=40)
        
        # Tabbed interface
        self.notebook = ttk.Notebook(main)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=40, pady=20)
        
        # Tab 1: Current Session
        self.session_tab = tk.Frame(self.notebook, bg=self.colors['bg'])
        self.notebook.add(self.session_tab, text='Current Session')
        
        # Tab 2: Multi-Subject Dashboard
        self.dashboard_tab = tk.Frame(self.notebook, bg=self.colors['bg'])
        self.notebook.add(self.dashboard_tab, text='Multi-Subject Dashboard')
        
        # Tab 3: Settings
        self.settings_tab = tk.Frame(self.notebook, bg=self.colors['bg'])
        self.notebook.add(self.settings_tab, text='Settings & Parameters')
        
        # Build tabs
        self.create_session_tab()
        self.create_dashboard_tab()
        self.create_settings_tab()
    
    def create_session_tab(self):
        """Create the current session tab"""
        content = tk.Frame(self.session_tab, bg=self.colors['bg'])
        content.pack(fill=tk.BOTH, expand=True, pady=10, padx=10)
        
        # Three-pane resizable layout
        self.main_paned = ttk.PanedWindow(content, orient=tk.HORIZONTAL)
        self.main_paned.pack(fill=tk.BOTH, expand=True)
        
        # Left sidebar
        left_sidebar = tk.Frame(self.main_paned, bg=self.colors['panel'])
        self.panel_widgets.append(left_sidebar)
        self.main_paned.add(left_sidebar, weight=0)
        
        # Middle-right paned window
        middle_right_paned = ttk.PanedWindow(self.main_paned, orient=tk.HORIZONTAL)
        self.main_paned.add(middle_right_paned, weight=1)
        
        # Middle area
        middle_area = tk.Frame(middle_right_paned, bg=self.colors['bg'])
        self.bg_widgets.append(middle_area)
        middle_right_paned.add(middle_area, weight=1)
        
        # Right sidebar
        right_sidebar = tk.Frame(middle_right_paned, bg=self.colors['panel'])
        self.panel_widgets.append(right_sidebar)
        middle_right_paned.add(right_sidebar, weight=0)
        
        self.create_left_sidebar(left_sidebar)
        self.create_middle_area(middle_area)
        self.create_right_sidebar(right_sidebar)
    
    def create_dashboard_tab(self):
        """Create multi-subject dashboard tab"""
        content = tk.Frame(self.dashboard_tab, bg=self.colors['bg'])
        content.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # Control panel
        control_panel = tk.Frame(content, bg='white',
                                relief='solid', borderwidth=1,
                                highlightbackground=self.colors['border'],
                                highlightthickness=1)
        control_panel.pack(fill='x', pady=(0, 15))
        
        control_content = tk.Frame(control_panel, bg='white')
        control_content.pack(fill='x', padx=30, pady=20)
        
        tk.Label(control_content, text="Multi-Subject Comparison",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        btn_frame = tk.Frame(control_content, bg='white')
        btn_frame.pack(side='right')
        
        tk.Button(btn_frame, text="LOAD SUBJECTS",
                 command=self.load_multi_subjects,
                 bg=self.colors['primary'], fg='white',
                 font=self.subheading_font, relief='flat',
                 cursor='hand2', padx=20, pady=8,
                 activebackground=self.colors['secondary']).pack(side='left', padx=(0, 10))
        
        tk.Button(btn_frame, text="REFRESH",
                 command=self.refresh_dashboard,
                 bg=self.colors['secondary'], fg='white',
                 font=self.subheading_font, relief='flat',
                 cursor='hand2', padx=20, pady=8,
                 activebackground=self.colors['primary']).pack(side='left', padx=(0, 10))
        
        tk.Button(btn_frame, text="CLEAR",
                 command=self.clear_dashboard,
                 bg=self.colors['text_secondary'], fg='white',
                 font=self.subheading_font, relief='flat',
                 cursor='hand2', padx=20, pady=8,
                 activebackground='#616161').pack(side='left')
        
        # Two-panel layout
        left_panel = tk.Frame(content, bg='white',
                             relief='solid', borderwidth=1,
                             highlightbackground=self.colors['border'],
                             highlightthickness=1)
        left_panel.pack(side='left', fill='both', expand=True, padx=(0, 10))
        
        right_panel = tk.Frame(content, bg='white',
                              relief='solid', borderwidth=1,
                              highlightbackground=self.colors['border'],
                              highlightthickness=1)
        right_panel.pack(side='right', fill='both', expand=True)
        
        # LEFT: Table
        table_header = tk.Frame(left_panel, bg='white')
        table_header.pack(fill='x', padx=20, pady=(20, 10))
        
        tk.Label(table_header, text="Subject Performance Summary",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Frame(left_panel, height=1, bg=self.colors['border']).pack(fill='x', padx=20)
        
        table_content = tk.Frame(left_panel, bg='white')
        table_content.pack(fill='both', expand=True, padx=20, pady=20)
        
        columns = ('Subject', 'Sessions', 'Total Trials', 'Success %', 'Avg RT', 'Best Session')
        self.subject_tree = ttk.Treeview(table_content, columns=columns, show='headings',
                                        height=15, style="Nature.Treeview")
        
        col_widths = {'Subject': 120, 'Sessions': 80, 'Total Trials': 100, 
                     'Success %': 90, 'Avg RT': 80, 'Best Session': 100}
        for col in columns:
            self.subject_tree.heading(col, text=col)
            self.subject_tree.column(col, width=col_widths.get(col, 100), anchor='center')
        
        scrollbar = ttk.Scrollbar(table_content, orient=tk.VERTICAL, command=self.subject_tree.yview)
        self.subject_tree.configure(yscroll=scrollbar.set)
        
        self.subject_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # RIGHT: Charts
        chart_header = tk.Frame(right_panel, bg='white')
        chart_header.pack(fill='x', padx=20, pady=(20, 10))
        
        tk.Label(chart_header, text="Learning Curves",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Frame(right_panel, height=1, bg=self.colors['border']).pack(fill='x', padx=20)
        
        chart_content = tk.Frame(right_panel, bg='white')
        chart_content.pack(fill='both', expand=True, padx=20, pady=20)
        
        self.fig_multi = Figure(figsize=(8, 6), dpi=100, facecolor='white')
        self.ax_multi = self.fig_multi.add_subplot(111)
        self.ax_multi.set_facecolor('white')
        self.ax_multi.set_xlabel('Trial Number', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_multi.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_multi.set_ylim(0, 105)
        self.ax_multi.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5)
        self.ax_multi.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_multi.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_multi.spines[spine].set_color(self.colors['border'])
            self.ax_multi.spines[spine].set_linewidth(1)
        
        self.ax_multi.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_multi = FigureCanvasTkAgg(self.fig_multi, master=chart_content)
        self.canvas_multi.draw()
        self.canvas_multi.get_tk_widget().pack(fill=tk.BOTH, expand=True)
    
    def create_settings_tab(self):
        """Create settings tab"""
        content = tk.Frame(self.settings_tab, bg=self.colors['bg'])
        content.pack(fill=tk.BOTH, expand=True, padx=100, pady=40)
        
        # Parameters
        params_frame = tk.Frame(content, bg='white',
                               relief='solid', borderwidth=1,
                               highlightbackground=self.colors['border'],
                               highlightthickness=1)
        params_frame.pack(fill='x', pady=(0, 20))
        
        params_header = tk.Frame(params_frame, bg='white')
        params_header.pack(fill='x', padx=30, pady=(25, 15))
        
        tk.Label(params_header, text="Real-Time Parameters",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Label(params_header, text="Adjustable during acquisition",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left', padx=(15, 0))
        
        tk.Frame(params_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=30)
        
        params_content = tk.Frame(params_frame, bg='white')
        params_content.pack(fill='both', padx=40, pady=30)
        
        # Window size
        self.create_param_row(params_content, "Sliding Window Size", 
                             self.params['window_size'], 10, 100, "trials",
                             "Number of recent trials for windowed success rate")
        
        # Autosave
        autosave_row = tk.Frame(params_content, bg='white')
        autosave_row.pack(fill='x', pady=(0, 20))
        
        left = tk.Frame(autosave_row, bg='white')
        left.pack(side='left', fill='x', expand=True)
        
        tk.Label(left, text="Auto-Save Interval",
                font=self.subheading_font, fg=self.colors['text'],
                bg='white').pack(anchor='w')
        
        tk.Label(left, text="Automatically save session every N trials",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(anchor='w', pady=(5, 10))
        
        controls = tk.Frame(left, bg='white')
        controls.pack(anchor='w')
        
        tk.Checkbutton(controls, text="Enable Auto-Save",
                      variable=self.params['autosave_enabled'],
                      font=self.body_font, bg='white',
                      activebackground='white',
                      command=self.toggle_autosave).pack(side='left')
        
        tk.Scale(controls, from_=10, to=100, orient=tk.HORIZONTAL,
                variable=self.params['autosave_interval'],
                font=self.body_font, length=200,
                bg='white', highlightthickness=0,
                troughcolor=self.colors['grid']).pack(side='left', padx=(20, 10))
        
        tk.Label(controls, textvariable=self.params['autosave_interval'],
                font=self.subheading_font, fg=self.colors['primary'],
                bg='white', width=4).pack(side='left')
        
        tk.Label(controls, text="trials",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left')
        
        # Apply button
        apply_frame = tk.Frame(params_content, bg='white')
        apply_frame.pack(fill='x', pady=(20, 0))
        
        tk.Button(apply_frame, text="APPLY CHANGES",
                 command=self.apply_parameters,
                 bg=self.colors['success'], fg='white',
                 font=self.heading_font, relief='flat',
                 cursor='hand2', pady=15,
                 activebackground='#388E3C').pack(fill='x')
        
        # Info
        info_frame = tk.Frame(content, bg='white',
                             relief='solid', borderwidth=1,
                             highlightbackground=self.colors['border'],
                             highlightthickness=1)
        info_frame.pack(fill='both', expand=True)
        
        info_header = tk.Frame(info_frame, bg='white')
        info_header.pack(fill='x', padx=30, pady=(25, 15))
        
        tk.Label(info_header, text="System Information",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Frame(info_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=30)
        
        info_content = tk.Frame(info_frame, bg='white')
        info_content.pack(fill='both', expand=True, padx=40, pady=30)
        
        info_text = scrolledtext.ScrolledText(info_content, font=self.body_font,
                                             relief='flat', wrap='word',
                                             height=15, bg=self.colors['panel'])
        info_text.pack(fill='both', expand=True)
        
        info_text.insert('1.0', f"""Data Directory: {self.data_dir.absolute()}

Total Subjects: {len(self.subjects)}

Water Volumes per Reward Level:
• Level 0 (1 drop): 4 µL
• Level 1 (2 drops): 8 µL
• Level 2 (3 drops): 12 µL

Trial Outcomes:
• CORRECT: Successful trial, water reward given
• TIMEOUT: No response within time limit
• EARLY: Premature movement (penalty state), 2000ms delay

Active Parameters:
• Sliding Window: {self.params['window_size'].get()} trials
• Auto-Save: {'Enabled' if self.params['autosave_enabled'].get() else 'Disabled'}

Features:
✓ Early movement penalty tracking
✓ Real-time water consumption tracking
✓ Statistical summaries (mean, median, SD)
✓ Current performance streak counter
✓ Recent trials table (last 50 trials)
✓ Multi-subject comparison dashboard
✓ Session-to-session learning curves
✓ Reaction time analysis by reward level
✓ Comprehensive CSV export with metadata

Tips:
• Monitor early trials to track impulsivity
• Watch for patterns in early movements by reward level
• Use sliding window to detect performance changes
• Load previous sessions to compare learning
• Multi-subject dashboard identifies cohort trends
• Adjust parameters without interrupting session
""")
        info_text.config(state='disabled')
    
    def create_param_row(self, parent, label, var, min_val, max_val, unit, description):
        """Create parameter row"""
        row = tk.Frame(parent, bg='white')
        row.pack(fill='x', pady=(0, 20))
        
        left = tk.Frame(row, bg='white')
        left.pack(side='left', fill='x', expand=True)
        
        tk.Label(left, text=label,
                font=self.subheading_font, fg=self.colors['text'],
                bg='white').pack(anchor='w')
        
        tk.Label(left, text=description,
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(anchor='w', pady=(5, 10))
        
        controls = tk.Frame(left, bg='white')
        controls.pack(anchor='w')
        
        tk.Scale(controls, from_=min_val, to=max_val, orient=tk.HORIZONTAL,
                variable=var, font=self.body_font, length=300,
                bg='white', highlightthickness=0,
                troughcolor=self.colors['grid']).pack(side='left', padx=(0, 10))
        
        tk.Label(controls, textvariable=var,
                font=self.subheading_font, fg=self.colors['primary'],
                bg='white', width=4).pack(side='left')
        
        tk.Label(controls, text=unit,
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left')
    
    def toggle_autosave(self):
        """Toggle autosave"""
        if self.params['autosave_enabled'].get():
            messagebox.showinfo("Auto-Save Enabled", 
                              f"Session will auto-save every {self.params['autosave_interval'].get()} trials.")
    
    def apply_parameters(self):
        """Apply parameters"""
        messagebox.showinfo("Parameters Applied", 
                           "Real-time parameters updated.")
        self.update_plots()
    
    def create_left_sidebar(self, parent):
        """Create left sidebar"""
        # Subject
        subj_frame, subj_content = self.create_section(parent, "Subject", "Identification")
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
        
        tk.Label(subj_content, text="Weight (g)",
                font=self.body_font, fg=self.colors['text'],
                bg=self.colors['panel']).pack(anchor='w', pady=(15, 8))
        
        self.weight_entry = tk.Entry(subj_content, font=self.body_font,
                                     relief='solid', borderwidth=1,
                                     highlightbackground=self.colors['border'],
                                     highlightcolor=self.colors['primary'],
                                     highlightthickness=1)
        self.weight_entry.pack(fill='x', ipady=8)
        
        # Connection
        conn_frame, conn_content = self.create_section(parent, "Connection", "Serial")
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
        ctrl_frame, ctrl_content = self.create_section(parent, "Session Control", "Acquisition")
        ctrl_frame.pack(fill='x', padx=20, pady=(30, 0))
        
        self.start_btn = tk.Button(ctrl_content, text="START",
                                   command=self.start_session,
                                   bg=self.colors['success'], fg='white',
                                   font=self.heading_font, relief='flat',
                                   cursor='hand2', state='disabled',
                                   disabledforeground='#BDBDBD',
                                   activebackground='#388E3C', height=2)
        self.start_btn.pack(fill='x', pady=(0, 10), ipady=8)
        
        self.stop_btn = tk.Button(ctrl_content, text="STOP",
                                  command=self.stop_session,
                                  bg=self.colors['accent'], fg='white',
                                  font=self.heading_font, relief='flat',
                                  cursor='hand2', state='disabled',
                                  disabledforeground='#BDBDBD',
                                  activebackground='#E53935', height=2)
        self.stop_btn.pack(fill='x', pady=(0, 10), ipady=8)
        
        btn_row = tk.Frame(ctrl_content, bg=self.colors['panel'])
        btn_row.pack(fill='x', pady=(0, 10))
        
        self.save_btn = tk.Button(btn_row, text="SAVE",
                                  command=self.save_session,
                                  bg=self.colors['text'], fg='white',
                                  font=self.subheading_font, relief='flat',
                                  cursor='hand2', state='disabled',
                                  disabledforeground='#BDBDBD',
                                  activebackground='#424242')
        self.save_btn.pack(side='left', fill='x', expand=True, ipady=8)
        
        self.clear_btn = tk.Button(btn_row, text="CLEAR",
                                   command=self.clear_session,
                                   bg=self.colors['text_secondary'], fg='white',
                                   font=self.subheading_font, relief='flat',
                                   cursor='hand2', state='disabled',
                                   disabledforeground='#BDBDBD',
                                   activebackground='#616161')
        self.clear_btn.pack(side='left', fill='x', expand=True, padx=(10, 0), ipady=8)
        
        self.recording_label = tk.Label(ctrl_content, text="",
                                       font=self.heading_font,
                                       bg=self.colors['panel'])
        self.recording_label.pack(pady=(10, 0))
        
        # Session Comparison
        comp_frame, comp_content = self.create_section(parent, "Session Comparison", "Load previous")
        comp_frame.pack(fill='both', expand=True, padx=20, pady=(15, 20))
        
        load_btn = tk.Button(comp_content, text="LOAD SESSIONS",
                            command=self.load_comparison_sessions,
                            bg=self.colors['secondary'], fg='white',
                            font=self.subheading_font, relief='flat',
                            cursor='hand2',
                            activebackground=self.colors['primary'])
        load_btn.pack(fill='x', ipady=8, pady=(0, 10))
        
        clear_comp_btn = tk.Button(comp_content, text="CLEAR",
                                   command=self.clear_comparison,
                                   bg=self.colors['text_secondary'], fg='white',
                                   font=self.body_font, relief='flat',
                                   cursor='hand2',
                                   activebackground='#616161')
        clear_comp_btn.pack(fill='x', ipady=6)
        
        self.comparison_list = tk.Listbox(comp_content, font=self.body_font,
                                         height=6, relief='solid', borderwidth=1,
                                         highlightbackground=self.colors['border'],
                                         highlightthickness=1)
        self.comparison_list.pack(fill='both', expand=True, pady=(10, 0))
    
    def create_middle_area(self, parent):
        """Create middle area - UPDATED with Early stat card"""
        # Stats cards - NOW WITH 5 CARDS including Early
        stats_container = tk.Frame(parent, bg=self.colors['bg'])
        stats_container.pack(fill='x', pady=(0, 20))
        
        stat_cards = []
        stat_data = [
            ("Total", "n", self.colors['primary']),
            ("Correct", "n", self.colors['success']),
            ("Early", "n", self.colors['warning']),  # NEW CARD
            ("Success", "%", self.colors['accent']),
            ("Window", "%", self.colors['secondary'])
        ]
        
        for i, (label, unit, color) in enumerate(stat_data):
            card = tk.Frame(stats_container, bg='white',
                           relief='solid', borderwidth=1,
                           highlightbackground=self.colors['border'],
                           highlightthickness=1)
            card.pack(side='left', fill='both', expand=True, padx=(0 if i == 0 else 10, 0))
            
            tk.Frame(card, height=4, bg=color).pack(fill='x')
            
            content_frame = tk.Frame(card, bg='white')
            content_frame.pack(fill='both', expand=True, padx=20, pady=20)
            
            value_label = tk.Label(content_frame, text="0",
                                  font=self.stat_font if i < 3 else self.small_stat_font,
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
        
        # Unpack all 5 stat labels
        self.total_label, self.correct_label, self.early_label, self.success_label, self.window_label = stat_cards
        
        # Charts
        charts_container = tk.Frame(parent, bg=self.colors['bg'])
        charts_container.pack(fill='x', pady=(0, 20))
        
        # Performance chart
        perf_frame = tk.Frame(charts_container, bg='white',
                             relief='solid', borderwidth=1,
                             highlightbackground=self.colors['border'],
                             highlightthickness=1)
        perf_frame.pack(side='left', fill='both', expand=True, padx=(0, 10))
        
        perf_header = tk.Frame(perf_frame, bg='white')
        perf_header.pack(fill='x', padx=20, pady=(20, 10))
        
        tk.Label(perf_header, text="Performance",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        self.window_display = tk.Label(perf_header, text=f"{self.params['window_size'].get()}-trial window",
                                       font=self.body_font, fg=self.colors['text_secondary'],
                                       bg='white')
        self.window_display.pack(side='left', padx=(15, 0))
        
        tk.Frame(perf_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=20)
        
        perf_content = tk.Frame(perf_frame, bg='white')
        perf_content.pack(fill='both', expand=True, padx=15, pady=15)
        
        self.fig_perf = Figure(figsize=(6, 4), dpi=100, facecolor='white')
        self.ax_perf = self.fig_perf.add_subplot(111)
        self.ax_perf.set_facecolor('white')
        self.ax_perf.set_xlabel('Trial', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_perf.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_perf.set_ylim(0, 105)
        self.ax_perf.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5)
        self.ax_perf.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_perf.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_perf.spines[spine].set_color(self.colors['border'])
            self.ax_perf.spines[spine].set_linewidth(1)
        
        self.ax_perf.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_perf = FigureCanvasTkAgg(self.fig_perf, master=perf_content)
        self.canvas_perf.draw()
        self.canvas_perf.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # RT chart
        rt_frame = tk.Frame(charts_container, bg='white',
                           relief='solid', borderwidth=1,
                           highlightbackground=self.colors['border'],
                           highlightthickness=1)
        rt_frame.pack(side='right', fill='both', expand=True)
        
        rt_header = tk.Frame(rt_frame, bg='white')
        rt_header.pack(fill='x', padx=20, pady=(20, 10))
        
        tk.Label(rt_header, text="Reaction Time",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Label(rt_header, text="By reward level",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left', padx=(15, 0))
        
        tk.Frame(rt_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=20)
        
        rt_content = tk.Frame(rt_frame, bg='white')
        rt_content.pack(fill='both', expand=True, padx=15, pady=15)
        
        self.fig_rt = Figure(figsize=(6, 4), dpi=100, facecolor='white')
        self.ax_rt = self.fig_rt.add_subplot(111)
        self.ax_rt.set_facecolor('white')
        self.ax_rt.set_xlabel('Trial', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_rt.set_ylabel('RT (ms)', fontfamily='Helvetica', fontsize=10, color=self.colors['text'])
        self.ax_rt.grid(True, color=self.colors['grid'], linestyle='-', linewidth=0.5)
        self.ax_rt.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_rt.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_rt.spines[spine].set_color(self.colors['border'])
            self.ax_rt.spines[spine].set_linewidth(1)
        
        self.ax_rt.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_rt = FigureCanvasTkAgg(self.fig_rt, master=rt_content)
        self.canvas_rt.draw()
        self.canvas_rt.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Recent Trials Table
        table_frame = tk.Frame(parent, bg='white',
                              relief='solid', borderwidth=1,
                              highlightbackground=self.colors['border'],
                              highlightthickness=1)
        table_frame.pack(fill='both', expand=True)
        
        table_header = tk.Frame(table_frame, bg='white')
        table_header.pack(fill='x', padx=20, pady=(20, 10))
        
        tk.Label(table_header, text="Recent Trials",
                font=self.heading_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        tk.Label(table_header, text="Last 50 trials",
                font=self.body_font, fg=self.colors['text_secondary'],
                bg='white').pack(side='left', padx=(15, 0))
        
        tk.Frame(table_frame, height=1, bg=self.colors['border']).pack(fill='x', padx=20)
        
        table_content = tk.Frame(table_frame, bg='white')
        table_content.pack(fill='both', expand=True, padx=20, pady=20)
        
        columns = ('Trial', 'Level', 'Outcome', 'RT (ms)', 'Position', 'Time')
        self.trial_tree = ttk.Treeview(table_content, columns=columns, show='headings',
                                      height=8, style="Nature.Treeview")
        
        col_widths = {'Trial': 70, 'Level': 60, 'Outcome': 100, 'RT (ms)': 90, 'Position': 90, 'Time': 100}
        for col in columns:
            self.trial_tree.heading(col, text=col)
            self.trial_tree.column(col, width=col_widths.get(col, 100), anchor='center')
        
        scrollbar = ttk.Scrollbar(table_content, orient=tk.VERTICAL, command=self.trial_tree.yview)
        self.trial_tree.configure(yscroll=scrollbar.set)
        
        self.trial_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
    
    def create_right_sidebar(self, parent):
        """Create right sidebar with scrollable canvas"""
        # Create canvas with scrollbar for right sidebar
        canvas = tk.Canvas(parent, bg=self.colors['panel'], highlightthickness=0)
        scrollbar = tk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg=self.colors['panel'])
        
        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        # Pack canvas and scrollbar
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Mouse wheel scrolling
        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1*(event.delta/120)), "units")
        canvas.bind_all("<MouseWheel>", _on_mousewheel)
        
        # Water Consumption (compact)
        water_frame, water_content = self.create_section(scrollable_frame, "Water", "µL consumed")
        water_frame.pack(fill='x', padx=15, pady=(15, 0))
        
        # Compact water card
        total_water_card = tk.Frame(water_content, bg='white',
                                   relief='solid', borderwidth=1,
                                   highlightbackground=self.colors['border'],
                                   highlightthickness=1)
        total_water_card.pack(fill='x', pady=(0, 5))
        
        tk.Frame(total_water_card, height=3, bg='#2196F3').pack(fill='x')
        
        water_card_content = tk.Frame(total_water_card, bg='white')
        water_card_content.pack(fill='both', padx=10, pady=8)
        
        water_row = tk.Frame(water_card_content, bg='white')
        water_row.pack(fill='x')
        
        tk.Label(water_row, text="Total:",
                font=self.body_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        self.water_total_label = tk.Label(water_row, text="0 µL",
                                          font=self.subheading_font,
                                          fg='#2196F3', bg='white')
        self.water_total_label.pack(side='right')
        
        # Compact breakdown
        breakdown_frame = tk.Frame(water_content, bg=self.colors['panel'])
        breakdown_frame.pack(fill='x', pady=(5, 0))
        
        self.water_breakdown_labels = {}
        for i in range(3):
            row = tk.Frame(breakdown_frame, bg=self.colors['panel'])
            row.pack(fill='x', pady=1)
            
            tk.Label(row, text=f"L{i}:",
                    font=self.body_font, fg=self.colors['text'],
                    bg=self.colors['panel'], width=3).pack(side='left')
            
            label = tk.Label(row, text="0 µL",
                           font=self.body_font, fg=self.colors['text_secondary'],
                           bg=self.colors['panel'])
            label.pack(side='right')
            
            self.water_breakdown_labels[i] = label
        
        # Streak (compact)
        streak_frame, streak_content = self.create_section(scrollable_frame, "Streak", "Current")
        streak_frame.pack(fill='x', padx=15, pady=(15, 0))
        
        streak_card = tk.Frame(streak_content, bg='white',
                              relief='solid', borderwidth=1,
                              highlightbackground=self.colors['border'],
                              highlightthickness=1)
        streak_card.pack(fill='x')
        
        tk.Frame(streak_card, height=2, bg=self.colors['success']).pack(fill='x')
        
        streak_inner = tk.Frame(streak_card, bg='white')
        streak_inner.pack(fill='both', padx=10, pady=6)
        
        streak_row = tk.Frame(streak_inner, bg='white')
        streak_row.pack(fill='x')
        
        tk.Label(streak_row, text="Streak:",
                font=self.body_font, fg=self.colors['text'],
                bg='white').pack(side='left')
        
        self.streak_label = tk.Label(streak_row, text="0",
                                     font=self.subheading_font,
                                     fg=self.colors['success'],
                                     bg='white')
        self.streak_label.pack(side='right')
        
        # RT Statistics (compact - single card with all 3 levels)
        rt_frame, rt_content = self.create_section(scrollable_frame, "RT Stats", "Mean±SD (ms)")
        rt_frame.pack(fill='x', padx=15, pady=(15, 0))
        
        self.rt_stat_labels = {}
        for i in range(3):
            card = tk.Frame(rt_content, bg='white',
                           relief='solid', borderwidth=1,
                           highlightbackground=self.colors['border'],
                           highlightthickness=1)
            card.pack(fill='x', pady=(0 if i == 0 else 3, 0))
            
            tk.Frame(card, height=2, bg=self.colors['reward_colors'][i]).pack(fill='x')
            
            content = tk.Frame(card, bg='white')
            content.pack(fill='both', padx=8, pady=4)
            
            # Single row with level and stats
            header = tk.Frame(content, bg='white')
            header.pack(fill='x')
            
            tk.Label(header, text=f"L{i}:",
                    font=self.body_font, fg=self.colors['text'],
                    bg='white', width=3).pack(side='left')
            
            mean_label = tk.Label(header, text="—",
                                 font=self.body_font, fg=self.colors['text'],
                                 bg='white')
            mean_label.pack(side='right')
            
            self.rt_stat_labels[i] = {
                'mean': mean_label
            }
        
        # Reward Analysis (compact)
        reward_frame, reward_content = self.create_section(scrollable_frame, "Reward", "Success by level")
        reward_frame.pack(fill='x', padx=15, pady=(15, 0))
        
        self.reward_stats = {}
        for i in range(3):
            card = tk.Frame(reward_content, bg='white',
                           relief='solid', borderwidth=1,
                           highlightbackground=self.colors['border'],
                           highlightthickness=1)
            card.pack(fill='x', pady=(0 if i == 0 else 3, 0))
            
            tk.Frame(card, height=2, bg=self.colors['reward_colors'][i]).pack(fill='x')
            
            content = tk.Frame(card, bg='white')
            content.pack(fill='both', padx=8, pady=4)
            
            row = tk.Frame(content, bg='white')
            row.pack(fill='x')
            
            tk.Label(row, text=f"L{i}:",
                    font=self.body_font, fg=self.colors['text'],
                    bg='white', width=3).pack(side='left')
            
            trials_label = tk.Label(row, text="n=0",
                                   font=self.body_font,
                                   fg=self.colors['text_secondary'],
                                   bg='white')
            trials_label.pack(side='left', padx=(5, 0))
            
            rate_label = tk.Label(row, text="—%",
                                 font=self.body_font,
                                 fg=self.colors['reward_colors'][i],
                                 bg='white')
            rate_label.pack(side='right')
            
            self.reward_stats[i] = {
                'rate': rate_label,
                'trials': trials_label
            }
        
        # Notes (compact)
        notes_frame, notes_content = self.create_section(scrollable_frame, "Notes", "Session")
        notes_frame.pack(fill='x', padx=15, pady=(15, 0))
        
        self.notes_text = scrolledtext.ScrolledText(notes_content,
                                                    font=self.body_font,
                                                    relief='solid', borderwidth=1,
                                                    highlightbackground=self.colors['border'],
                                                    highlightcolor=self.colors['primary'],
                                                    highlightthickness=1,
                                                    wrap='word', height=3)
        self.notes_text.pack(fill='x')
        
        # Event Log (compact)
        tstamp_frame, tstamp_content = self.create_section(scrollable_frame, "Event Log", "Timestamped")
        tstamp_frame.pack(fill='x', padx=15, pady=(15, 15))
        
        note_input_frame = tk.Frame(tstamp_content, bg=self.colors['panel'])
        note_input_frame.pack(fill='x', pady=(0, 5))
        
        self.timestamped_note_entry = tk.Entry(note_input_frame, font=self.body_font,
                                               relief='solid', borderwidth=1,
                                               highlightbackground=self.colors['border'],
                                               highlightcolor=self.colors['primary'],
                                               highlightthickness=1)
        self.timestamped_note_entry.pack(side='left', fill='x', expand=True, ipady=6)
        self.timestamped_note_entry.bind('<Return>', lambda e: self.add_timestamped_note())
        
        add_note_btn = tk.Button(note_input_frame, text="LOG",
                                command=self.add_timestamped_note,
                                bg=self.colors['primary'], fg='white',
                                font=self.body_font, relief='flat',
                                padx=10, cursor='hand2',
                                activebackground=self.colors['secondary'])
        add_note_btn.pack(side='left', padx=(5, 0), ipady=6)
        
        self.timestamped_notes_display = scrolledtext.ScrolledText(tstamp_content,
                                                                   font=self.body_font,
                                                                   relief='solid', borderwidth=1,
                                                                   highlightbackground=self.colors['border'],
                                                                   highlightthickness=1,
                                                                   wrap='word', state='disabled',
                                                                   height=6)
        self.timestamped_notes_display.pack(fill='x')
    
    def set_recording_background(self, recording):
        """Change background"""
        bg = self.colors['bg_recording'] if recording else self.colors['bg']
        panel = self.colors['panel_recording'] if recording else self.colors['panel']
        
        self.root.config(bg=bg)
        for w in self.bg_widgets:
            try: w.config(bg=bg)
            except: pass
        for w in self.panel_widgets:
            try: w.config(bg=panel)
            except: pass
    
    def add_timestamped_note(self):
        """Add timestamped note"""
        text = self.timestamped_note_entry.get().strip()
        if not text:
            return
        
        timestamp = datetime.now().strftime('%H:%M:%S')
        trial = len(self.trials) if self.is_recording else "—"
        
        note = {
            'timestamp': datetime.now().isoformat(),
            'time_display': timestamp,
            'trial': trial,
            'note': text
        }
        
        self.timestamped_notes.append(note)
        
        self.timestamped_notes_display.config(state='normal')
        self.timestamped_notes_display.insert('1.0', f"[{timestamp}] T{trial}: {text}\n")
        self.timestamped_notes_display.config(state='disabled')
        
        self.timestamped_note_entry.delete(0, tk.END)
    
    def refresh_ports(self):
        """Refresh ports"""
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)
    
    def add_subject(self):
        """Add subject"""
        name = self.new_subject_entry.get().strip()
        if not name:
            return
        
        if name in self.subjects:
            messagebox.showwarning("Duplicate", "Subject exists.")
            return
        
        (self.data_dir / name).mkdir(exist_ok=True)
        
        self.subjects.append(name)
        self.subjects.sort()
        self.subject_combo['values'] = self.subjects
        self.subject_combo.set(name)
        self.new_subject_entry.delete(0, tk.END)
    
    def connect_serial(self):
        """Connect serial"""
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "Select port.")
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
            messagebox.showerror("Error", f"Connection failed:\n\n{str(e)}")
    
    def read_serial(self):
        """Read serial"""
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
                print(f"Serial error: {e}")
                break
    
    def check_queue(self):
        """Check queue"""
        try:
            while True:
                line = self.data_queue.get_nowait()
                if self.is_recording:
                    self.process_trial_data(line)
        except queue.Empty:
            pass
        
        self.root.after(100, self.check_queue)
    
    def process_trial_data(self, line):
        """Process trial - UPDATED to handle EARLY outcome"""
        parts = line.split(',')
        if len(parts) >= 5 and parts[0] == 'TRIAL':
            trial = {
                'timestamp': datetime.now().isoformat(),
                'trial_number': len(self.trials) + 1,
                'outcome': parts[1],  # Can be CORRECT, TIMEOUT, or EARLY
                'reaction_time': int(parts[2]),
                'encoder_position': int(parts[3]),
                'reward_level': int(parts[4])
            }
            
            self.trials.append(trial)
            self.update_stats(trial)
            self.update_reward_stats(trial)
            self.update_water_consumption(trial)
            self.update_rt_statistics(trial)
            self.update_trial_table(trial)
            self.check_autosave()
    
    def update_stats(self, trial):
        """Update stats - UPDATED to track early trials"""
        self.session_stats['total'] += 1
        
        # Track outcome-specific counters
        if trial['outcome'] == 'CORRECT':
            self.session_stats['correct'] += 1
        elif trial['outcome'] == 'EARLY':
            self.session_stats['early'] += 1
        elif trial['outcome'] == 'TIMEOUT':
            self.session_stats['timeout'] += 1
        
        # Streak tracking - UPDATED to handle early trials
        if trial['outcome'] == 'CORRECT':
            if self.session_stats['streak_type'] == 'correct':
                self.session_stats['current_streak'] += 1
            else:
                self.session_stats['current_streak'] = 1
                self.session_stats['streak_type'] = 'correct'
        else:
            # Both EARLY and TIMEOUT break correct streaks
            if self.session_stats['streak_type'] == 'incorrect':
                self.session_stats['current_streak'] += 1
            else:
                self.session_stats['current_streak'] = 1
                self.session_stats['streak_type'] = 'incorrect'
        
        rate = (self.session_stats['correct'] / self.session_stats['total']) * 100
        self.session_stats['success_rate'] = rate
        
        # Window rate
        window_size = self.params['window_size'].get()
        recent = self.trials[-window_size:]
        window_correct = sum(1 for t in recent if t['outcome'] == 'CORRECT')
        window_rate = (window_correct / len(recent)) * 100 if recent else 0
        
        # Update display labels
        self.total_label.config(text=str(self.session_stats['total']))
        self.correct_label.config(text=str(self.session_stats['correct']))
        self.early_label.config(text=str(self.session_stats['early']))  # NEW
        self.success_label.config(text=f"{rate:.1f}")
        self.window_label.config(text=f"{window_rate:.1f}")
        
        # Streak display with different symbol for early trials
        if self.session_stats['streak_type'] == 'correct':
            streak_text = f"{self.session_stats['current_streak']} ✓"
            streak_color = self.colors['success']
        else:
            # Use different symbol if last trial was early vs timeout
            symbol = '⚠' if trial['outcome'] == 'EARLY' else '✗'
            streak_text = f"{self.session_stats['current_streak']} {symbol}"
            streak_color = self.colors['warning'] if trial['outcome'] == 'EARLY' else self.colors['accent']
        
        self.streak_label.config(text=streak_text, fg=streak_color)
        
        self.performance_data.append({
            'trial': self.session_stats['total'],
            'success_rate': rate,
            'window_rate': window_rate
        })
        
        self.window_display.config(text=f"{window_size}-trial window")
        
        self.update_plots()
    
    def update_reward_stats(self, trial):
        """Update reward stats"""
        level = trial['reward_level']
        
        self.session_stats['by_reward'][level]['total'] += 1
        if trial['outcome'] == 'CORRECT':
            self.session_stats['by_reward'][level]['correct'] += 1
        
        for lv in range(3):
            stats = self.session_stats['by_reward'][lv]
            total = stats['total']
            correct = stats['correct']
            
            if total > 0:
                rate = (correct / total) * 100
                self.reward_stats[lv]['rate'].config(text=f"{rate:.1f}%")
                self.reward_stats[lv]['trials'].config(text=f"n={total}")
            else:
                self.reward_stats[lv]['rate'].config(text="—%")
                self.reward_stats[lv]['trials'].config(text=f"n=0")
    
    def update_water_consumption(self, trial):
        """Update water tracking - no water for EARLY trials"""
        if trial['outcome'] == 'CORRECT':
            level = trial['reward_level']
            water = self.water_volumes[level]
            self.session_stats['water_consumed'] += water
            
            # Update total
            self.water_total_label.config(text=str(self.session_stats['water_consumed']))
            
            # Update breakdown
            for lv in range(3):
                lv_correct = sum(1 for t in self.trials 
                                if t['reward_level'] == lv and t['outcome'] == 'CORRECT')
                lv_water = lv_correct * self.water_volumes[lv]
                self.water_breakdown_labels[lv].config(text=f"{lv_water} µL")
    
    def update_rt_statistics(self, trial):
        """Update RT statistics - only for CORRECT trials"""
        if trial['outcome'] != 'CORRECT':
            return
        
        level = trial['reward_level']
        
        # Get all correct trials for this level
        lv_trials = [t for t in self.trials 
                    if t['reward_level'] == level and t['outcome'] == 'CORRECT']
        
        if lv_trials:
            rts = [t['reaction_time'] for t in lv_trials]
            
            mean_rt = np.mean(rts)
            std_rt = np.std(rts)
            
            self.rt_stat_labels[level]['mean'].config(text=f"{mean_rt:.0f}±{std_rt:.0f}")
    
    def update_trial_table(self, trial):
        """Update trial table - UPDATED with EARLY color coding"""
        time_str = datetime.fromisoformat(trial['timestamp']).strftime('%H:%M:%S')
        
        tag = f"level{trial['reward_level']}_{trial['outcome'].lower()}"
        self.trial_tree.insert('', 0, values=(
            trial['trial_number'],
            f"L{trial['reward_level']}",
            trial['outcome'],
            trial['reaction_time'],
            trial['encoder_position'],
            time_str
        ), tags=(tag,))
        
        # Color coding - UPDATED with EARLY colors
        for lv in range(3):
            self.trial_tree.tag_configure(f'level{lv}_correct', background='#F1F8F4')  # Green tint
            self.trial_tree.tag_configure(f'level{lv}_timeout', background='#FFF4F4')  # Red tint
            self.trial_tree.tag_configure(f'level{lv}_early', background='#FFF8E1')    # Orange/warning tint
        
        # Keep only last 50
        children = self.trial_tree.get_children()
        if len(children) > 50:
            self.trial_tree.delete(children[-1])
    
    def check_autosave(self):
        """Check autosave"""
        if not self.params['autosave_enabled'].get():
            return
        
        interval = self.params['autosave_interval'].get()
        trials_since = len(self.trials) - self.last_autosave_trial
        
        if trials_since >= interval:
            self.save_session(autosave=True)
            self.last_autosave_trial = len(self.trials)
    
    def update_plots(self):
        """Update plots"""
        self.update_performance_plot()
        self.update_rt_plot()
    
    def update_performance_plot(self):
        """Update performance"""
        if not self.performance_data:
            return
        
        window_size = self.params['window_size'].get()
        
        trials = [d['trial'] for d in self.performance_data[-100:]]
        cum = [d['success_rate'] for d in self.performance_data[-100:]]
        
        # Recalculate window
        window = []
        for i, _ in enumerate(self.performance_data[-100:]):
            idx = len(self.performance_data) - 100 + i
            start = max(0, idx - window_size + 1)
            recent = self.trials[start:idx+1]
            if recent:
                wc = sum(1 for t in recent if t['outcome'] == 'CORRECT')
                wr = (wc / len(recent)) * 100
                window.append(wr)
            else:
                window.append(0)
        
        self.ax_perf.clear()
        
        # Comparisons
        for comp in self.comparison_sessions:
            ct = [d['trial'] for d in comp['performance_data']]
            cr = [d['success_rate'] for d in comp['performance_data']]
            self.ax_perf.plot(ct, cr, 
                            color=self.colors['comparison_color'],
                            linewidth=1, alpha=0.4, linestyle='--',
                            label=f"Previous ({comp['date']})")
        
        # Current
        self.ax_perf.fill_between(trials, cum, alpha=0.15, 
                                 color=self.colors['chart_line'], label='Cumulative')
        self.ax_perf.plot(trials, cum, color=self.colors['chart_line'],
                         linewidth=1.5, alpha=0.5, linestyle='--')
        
        self.ax_perf.plot(trials, window, color=self.colors['secondary'],
                         linewidth=2.5, marker='o', markersize=4,
                         markerfacecolor='white', markeredgewidth=2,
                         markeredgecolor=self.colors['secondary'], 
                         label=f'{window_size}-Trial', zorder=3)
        
        self.ax_perf.set_xlabel('Trial', fontfamily='Helvetica', fontsize=10, 
                               color=self.colors['text'])
        self.ax_perf.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=10, 
                               color=self.colors['text'])
        self.ax_perf.set_ylim(0, 105)
        self.ax_perf.set_facecolor('white')
        self.ax_perf.legend(loc='best', framealpha=0.9, fontsize=8)
        self.ax_perf.grid(True, color=self.colors['grid'], linestyle='-', 
                         linewidth=0.5, alpha=1, zorder=0)
        self.ax_perf.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_perf.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_perf.spines[spine].set_color(self.colors['border'])
            self.ax_perf.spines[spine].set_linewidth(1)
        
        self.ax_perf.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_perf.draw()
    
    def update_rt_plot(self):
        """Update RT - only plots CORRECT trials"""
        correct = [t for t in self.trials if t['outcome'] == 'CORRECT']
        if not correct:
            return
        
        self.ax_rt.clear()
        
        for level in range(3):
            lv_trials = [t for t in correct if t['reward_level'] == level]
            if lv_trials:
                tn = [t['trial_number'] for t in lv_trials]
                rts = [t['reaction_time'] for t in lv_trials]
                
                self.ax_rt.scatter(tn, rts, 
                                  color=self.colors['reward_colors'][level],
                                  alpha=0.6, s=40, edgecolors='white', linewidth=1,
                                  label=f'L{level}', zorder=3)
                
                if len(tn) > 5:
                    z = np.polyfit(tn, rts, 1)
                    p = np.poly1d(z)
                    self.ax_rt.plot(tn, p(tn),
                                   color=self.colors['reward_colors'][level],
                                   linestyle='--', linewidth=1.5, alpha=0.8)
        
        self.ax_rt.set_xlabel('Trial', fontfamily='Helvetica', fontsize=10, 
                             color=self.colors['text'])
        self.ax_rt.set_ylabel('RT (ms)', fontfamily='Helvetica', fontsize=10, 
                             color=self.colors['text'])
        self.ax_rt.set_facecolor('white')
        self.ax_rt.legend(loc='best', framealpha=0.9, fontsize=8)
        self.ax_rt.grid(True, color=self.colors['grid'], linestyle='-', 
                       linewidth=0.5, alpha=1, zorder=0)
        self.ax_rt.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_rt.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_rt.spines[spine].set_color(self.colors['border'])
            self.ax_rt.spines[spine].set_linewidth(1)
        
        self.ax_rt.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_rt.draw()
    
    def load_comparison_sessions(self):
        """Load sessions"""
        subject = self.subject_var.get()
        if not subject:
            messagebox.showwarning("No Subject", "Select subject.")
            return
        
        subject_dir = self.data_dir / subject
        if not subject_dir.exists():
            messagebox.showinfo("No Data", "No sessions found.")
            return
        
        files = filedialog.askopenfilenames(
            title="Select sessions",
            initialdir=subject_dir,
            filetypes=[("CSV files", "*.csv")]
        )
        
        if not files:
            return
        
        for file in files:
            session = self.load_session_file(file)
            if session:
                self.comparison_sessions.append(session)
                self.comparison_list.insert(tk.END, session['date'])
        
        self.update_plots()
    
    def load_session_file(self, path):
        """Load session - UPDATED to handle EARLY outcome"""
        try:
            trials = []
            with open(path, 'r') as f:
                reader = csv.reader(f)
                header_found = False
                
                for row in reader:
                    if not row or row[0].startswith('#'):
                        continue
                    
                    if not header_found:
                        if row[0] == 'Trial':
                            header_found = True
                        continue
                    
                    if len(row) >= 6:
                        trials.append({
                            'trial_number': int(row[0]),
                            'timestamp': row[1],
                            'reward_level': int(row[2]),
                            'outcome': row[3],  # Can be CORRECT, TIMEOUT, or EARLY
                            'reaction_time': int(row[4]),
                            'encoder_position': int(row[5])
                        })
            
            if not trials:
                return None
            
            perf = []
            correct = 0
            for i, t in enumerate(trials):
                if t['outcome'] == 'CORRECT':
                    correct += 1
                rate = (correct / (i + 1)) * 100
                perf.append({'trial': t['trial_number'], 'success_rate': rate})
            
            filename = Path(path).stem
            date = filename.split('_', 1)[1] if '_' in filename else filename
            
            return {'file_path': path, 'date': date, 'trials': trials, 'performance_data': perf}
        
        except Exception as e:
            print(f"Load error: {e}")
            return None
    
    def clear_comparison(self):
        """Clear comparisons"""
        self.comparison_sessions = []
        self.comparison_list.delete(0, tk.END)
        self.update_plots()
    
    def load_multi_subjects(self):
        """Load multi subjects"""
        selected = []
        for subject in self.subjects:
            result = messagebox.askyesno("Load Subject", f"Include {subject}?")
            if result:
                selected.append(subject)
        
        if not selected:
            return
        
        self.subject_sessions = {}
        for subject in selected:
            subject_dir = self.data_dir / subject
            if subject_dir.exists():
                sessions = []
                for csv_file in subject_dir.glob("*.csv"):
                    session = self.load_session_file(csv_file)
                    if session:
                        sessions.append(session)
                if sessions:
                    self.subject_sessions[subject] = sessions
        
        self.refresh_dashboard()
    
    def refresh_dashboard(self):
        """Refresh dashboard"""
        for item in self.subject_tree.get_children():
            self.subject_tree.delete(item)
        
        if not self.subject_sessions:
            return
        
        for subject, sessions in self.subject_sessions.items():
            all_trials = []
            for session in sessions:
                all_trials.extend(session['trials'])
            
            if not all_trials:
                continue
            
            total = len(all_trials)
            correct = sum(1 for t in all_trials if t['outcome'] == 'CORRECT')
            rate = (correct / total) * 100 if total > 0 else 0
            
            correct_trials = [t for t in all_trials if t['outcome'] == 'CORRECT']
            avg_rt = np.mean([t['reaction_time'] for t in correct_trials]) if correct_trials else 0
            
            best_rate = 0
            best_session = ""
            for session in sessions:
                sess_trials = session['trials']
                sess_correct = sum(1 for t in sess_trials if t['outcome'] == 'CORRECT')
                sess_rate = (sess_correct / len(sess_trials)) * 100 if sess_trials else 0
                if sess_rate > best_rate:
                    best_rate = sess_rate
                    best_session = session['date']
            
            self.subject_tree.insert('', tk.END, values=(
                subject,
                len(sessions),
                total,
                f"{rate:.1f}%",
                f"{avg_rt:.0f}ms" if avg_rt > 0 else "—",
                best_session[:10] if best_session else "—"
            ))
        
        self.ax_multi.clear()
        
        colors = self.colors['subject_colors']
        for i, (subject, sessions) in enumerate(self.subject_sessions.items()):
            color = colors[i % len(colors)]
            
            all_perf = []
            for session in sessions:
                all_perf.extend(session['performance_data'])
            
            if all_perf:
                trials = [d['trial'] for d in all_perf]
                rates = [d['success_rate'] for d in all_perf]
                
                self.ax_multi.plot(trials, rates, color=color,
                                  linewidth=2, alpha=0.7, label=subject)
        
        self.ax_multi.set_xlabel('Trial', fontfamily='Helvetica', fontsize=10, 
                                color=self.colors['text'])
        self.ax_multi.set_ylabel('Success Rate (%)', fontfamily='Helvetica', fontsize=10, 
                                color=self.colors['text'])
        self.ax_multi.set_ylim(0, 105)
        self.ax_multi.set_facecolor('white')
        self.ax_multi.legend(loc='best', framealpha=0.9, fontsize=9)
        self.ax_multi.grid(True, color=self.colors['grid'], linestyle='-', 
                          linewidth=0.5, alpha=1, zorder=0)
        self.ax_multi.set_axisbelow(True)
        
        for spine in ['top', 'right']:
            self.ax_multi.spines[spine].set_visible(False)
        for spine in ['left', 'bottom']:
            self.ax_multi.spines[spine].set_color(self.colors['border'])
            self.ax_multi.spines[spine].set_linewidth(1)
        
        self.ax_multi.tick_params(colors=self.colors['text_secondary'], labelsize=9)
        
        self.canvas_multi.draw()
    
    def clear_dashboard(self):
        """Clear dashboard"""
        self.subject_sessions = {}
        for item in self.subject_tree.get_children():
            self.subject_tree.delete(item)
        self.ax_multi.clear()
        self.canvas_multi.draw()
    
    def start_session(self):
        """Start session - UPDATED to reset early counter"""
        if not self.subject_var.get():
            messagebox.showerror("Error", "Select subject.")
            return
        
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showerror("Error", "Not connected.")
            return
        
        self.trials = []
        self.timestamped_notes = []
        self.session_stats = {
            'total': 0, 'correct': 0, 'early': 0, 'timeout': 0, 'success_rate': 0.0,
            'by_reward': {0: {'total': 0, 'correct': 0}, 
                         1: {'total': 0, 'correct': 0},
                         2: {'total': 0, 'correct': 0}},
            'current_streak': 0,
            'streak_type': None,
            'water_consumed': 0
        }
        self.performance_data = []
        self.last_autosave_trial = 0
        
        # Clear displays
        self.timestamped_notes_display.config(state='normal')
        self.timestamped_notes_display.delete('1.0', tk.END)
        self.timestamped_notes_display.config(state='disabled')
        
        for lv in range(3):
            self.reward_stats[lv]['rate'].config(text="—%")
            self.reward_stats[lv]['trials'].config(text="n=0")
            self.rt_stat_labels[lv]['mean'].config(text="—")
            self.water_breakdown_labels[lv].config(text="0 µL")
        
        self.water_total_label.config(text="0")
        self.streak_label.config(text="0")
        
        for item in self.trial_tree.get_children():
            self.trial_tree.delete(item)
        
        self.ax_perf.clear()
        self.ax_rt.clear()
        self.canvas_perf.draw()
        self.canvas_rt.draw()
        
        self.total_label.config(text="0")
        self.correct_label.config(text="0")
        self.early_label.config(text="0")  # NEW
        self.success_label.config(text="0.0")
        self.window_label.config(text="0.0")
        
        self.current_session_date = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
        
        self.is_recording = True
        self.set_recording_background(True)
        self.recording_label.config(text=f"● REC\n{self.subject_var.get()}",
                                   foreground=self.colors['accent'])
        self.header_status.config(text="RECORDING", fg=self.colors['accent'])
        
        self.start_btn.config(state='disabled', bg='#BDBDBD')
        self.stop_btn.config(state='normal', bg=self.colors['accent'])
        self.save_btn.config(state='disabled')
        self.clear_btn.config(state='disabled')
    
    def stop_session(self):
        """Stop session"""
        self.is_recording = False
        self.set_recording_background(False)
        self.recording_label.config(text="")
        self.header_status.config(text="CONNECTED", fg=self.colors['success'])
        self.start_btn.config(state='normal', bg=self.colors['success'])
        self.stop_btn.config(state='disabled', bg='#BDBDBD')
        self.save_btn.config(state='normal', bg=self.colors['text'])
        self.clear_btn.config(state='normal', bg=self.colors['text_secondary'])
    
    def save_session(self, autosave=False):
        """Save session - UPDATED to include early trial stats"""
        if not self.trials:
            if not autosave:
                messagebox.showwarning("No Data", "No trials.")
            return
        
        subject = self.subject_var.get()
        weight = self.weight_entry.get().strip()
        subject_dir = self.data_dir / subject
        filename = subject_dir / f"{subject}_{self.current_session_date}.csv"
        
        try:
            with open(filename, 'w', newline='') as f:
                writer = csv.writer(f)
                
                writer.writerow(['Trial', 'Timestamp', 'RewardLevel', 'Outcome', 'ReactionTime_ms', 'EncoderPosition'])
                
                for t in self.trials:
                    writer.writerow([
                        t['trial_number'], t['timestamp'], t['reward_level'],
                        t['outcome'], t['reaction_time'], t['encoder_position']
                    ])
                
                writer.writerow([])
                writer.writerow(['# Metadata'])
                writer.writerow([f'# Subject: {subject}'])
                writer.writerow([f'# Weight: {weight if weight else "—"} g'])
                writer.writerow([f'# Date: {self.current_session_date}'])
                writer.writerow([f'# Trials: {self.session_stats["total"]}'])
                writer.writerow([f'# Correct: {self.session_stats["correct"]}'])
                writer.writerow([f'# Early: {self.session_stats["early"]}'])  # NEW
                writer.writerow([f'# Timeout: {self.session_stats["timeout"]}'])  # NEW
                writer.writerow([f'# Success: {self.session_stats["success_rate"]:.1f}%'])
                writer.writerow([f'# Water Consumed: {self.session_stats["water_consumed"]} µL'])
                
                writer.writerow([])
                writer.writerow(['# Reward Performance'])
                for lv in range(3):
                    stats = self.session_stats['by_reward'][lv]
                    if stats['total'] > 0:
                        rate = (stats['correct'] / stats['total']) * 100
                        lv_trials = [t for t in self.trials 
                                    if t['reward_level'] == lv and t['outcome'] == 'CORRECT']
                        avg_rt = np.mean([t['reaction_time'] for t in lv_trials]) if lv_trials else 0
                        lv_water = stats['correct'] * self.water_volumes[lv]
                        writer.writerow([f'# L{lv}: {stats["correct"]}/{stats["total"]} ({rate:.1f}%), RT={avg_rt:.0f}ms, Water={lv_water}µL'])
                
                notes = self.notes_text.get("1.0", tk.END).strip()
                if notes:
                    writer.writerow([])
                    writer.writerow(['# Notes'])
                    for line in notes.split('\n'):
                        writer.writerow([f'# {line}'])
                
                if self.timestamped_notes:
                    writer.writerow([])
                    writer.writerow(['# Event Log'])
                    writer.writerow(['Timestamp', 'Trial', 'Note'])
                    for note in self.timestamped_notes:
                        writer.writerow([note['timestamp'], note['trial'], note['note']])
            
            if not autosave:
                summary = f"Saved: {filename}\n\n"
                summary += f"Trials: {self.session_stats['total']}\n"
                summary += f"Correct: {self.session_stats['correct']}\n"
                summary += f"Early: {self.session_stats['early']}\n"  # NEW
                summary += f"Timeout: {self.session_stats['timeout']}\n"  # NEW
                summary += f"Success: {self.session_stats['success_rate']:.1f}%\n"
                summary += f"Water: {self.session_stats['water_consumed']} µL\n\n"
                for lv in range(3):
                    stats = self.session_stats['by_reward'][lv]
                    if stats['total'] > 0:
                        rate = (stats['correct'] / stats['total']) * 100
                        summary += f"L{lv}: {rate:.1f}%\n"
                
                messagebox.showinfo("Saved", summary)
            
        except Exception as e:
            messagebox.showerror("Error", f"Save failed:\n\n{str(e)}")
    
    def clear_session(self):
        """Clear session"""
        if self.is_recording:
            messagebox.showwarning("Recording", "Stop recording first.")
            return
        
        if self.trials:
            resp = messagebox.askyesnocancel(
                "Clear Session",
                "Unsaved data.\n\nYes = Save & clear\nNo = Clear\nCancel = Don't clear"
            )
            
            if resp is None:
                return
            elif resp:
                self.save_session()
        
        self.trials = []
        self.timestamped_notes = []
        self.session_stats = {
            'total': 0, 'correct': 0, 'early': 0, 'timeout': 0, 'success_rate': 0.0,
            'by_reward': {0: {'total': 0, 'correct': 0}, 
                         1: {'total': 0, 'correct': 0},
                         2: {'total': 0, 'correct': 0}},
            'current_streak': 0,
            'streak_type': None,
            'water_consumed': 0
        }
        self.performance_data = []
        
        self.total_label.config(text="0")
        self.correct_label.config(text="0")
        self.early_label.config(text="0")  # NEW
        self.success_label.config(text="0.0")
        self.window_label.config(text="0.0")
        
        for lv in range(3):
            self.reward_stats[lv]['rate'].config(text="—%")
            self.reward_stats[lv]['trials'].config(text="n=0")
            self.rt_stat_labels[lv]['mean'].config(text="—")
            self.water_breakdown_labels[lv].config(text="0 µL")
        
        self.water_total_label.config(text="0")
        self.streak_label.config(text="0")
        
        self.timestamped_notes_display.config(state='normal')
        self.timestamped_notes_display.delete('1.0', tk.END)
        self.timestamped_notes_display.config(state='disabled')
        
        self.notes_text.delete('1.0', tk.END)
        
        for item in self.trial_tree.get_children():
            self.trial_tree.delete(item)
        
        self.ax_perf.clear()
        self.ax_rt.clear()
        self.canvas_perf.draw()
        self.canvas_rt.draw()
        
        self.save_btn.config(state='disabled')
        self.clear_btn.config(state='disabled')
        
        messagebox.showinfo("Cleared", "Ready for new session.")
    
    def __del__(self):
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()

if __name__ == "__main__":
    root = tk.Tk()
    app = BehavioralDataLogger(root)
    root.mainloop()