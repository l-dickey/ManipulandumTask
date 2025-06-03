import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, filedialog
import json
import csv
import os
import shutil
from datetime import datetime

class NotesLogger:
    def __init__(self, root):
        self.root = root
        self.root.title("JTF Lab Notes Logger")
        self.root.geometry("1000x800")
        
        # Modern color scheme
        self.colors = {
            'primary': '#2E3440',
            'secondary': '#3B4252',
            'accent': '#5E81AC',
            'success': '#A3BE8C',
            'warning': '#EBCB8B',
            'error': '#BF616A',
            'text': '#ECEFF4',
            'bg': '#2E3440',
            'card': '#3B4252'
        }
        
        # Configure style
        self.setup_styles()
        
        # Data directories / files
        self.base_data_dir = "data"
        self.subjects_file = "subjects.json"
        
        # Load existing subjects
        self.subjects = self.load_subjects()
        
        # Event counter and current session events list
        self.current_event_number = 1
        self.current_session_events = []
        
        # Notes format: "table" or "text"
        self.notes_format = tk.StringVar(value="table")
        
        # Build UI
        self.setup_menu()
        self.setup_ui()
    
    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        self.root.configure(bg=self.colors['bg'])
        
        style.configure('Card.TFrame', background=self.colors['card'], relief='flat', borderwidth=2)
        style.configure('Header.TLabel',
                        background=self.colors['card'],
                        foreground=self.colors['accent'],
                        font=('Segoe UI', 14, 'bold'))
        style.configure('Modern.TLabel',
                        background=self.colors['card'],
                        foreground=self.colors['text'],
                        font=('Segoe UI', 10))
        style.configure('Modern.TEntry',
                        fieldbackground=self.colors['secondary'],
                        foreground=self.colors['text'],
                        bordercolor=self.colors['accent'],
                        insertcolor=self.colors['text'])
        style.configure('Modern.TCombobox',
                        fieldbackground=self.colors['secondary'],
                        foreground=self.colors['text'],
                        bordercolor=self.colors['accent'])
        style.configure('Accent.TButton',
                        background=self.colors['accent'],
                        foreground=self.colors['text'],
                        font=('Segoe UI', 10, 'bold'),
                        borderwidth=0,
                        focuscolor='none')
        style.configure('Success.TButton',
                        background=self.colors['success'],
                        foreground=self.colors['primary'],
                        font=('Segoe UI', 10, 'bold'),
                        borderwidth=0,
                        focuscolor='none')
        style.configure('Warning.TButton',
                        background=self.colors['warning'],
                        foreground=self.colors['primary'],
                        font=('Segoe UI', 10, 'bold'),
                        borderwidth=0,
                        focuscolor='none')
        style.configure('Modern.Treeview',
                        background=self.colors['secondary'],
                        foreground=self.colors['text'],
                        fieldbackground=self.colors['secondary'],
                        borderwidth=0)
        style.configure('Modern.Treeview.Heading',
                        background=self.colors['accent'],
                        foreground=self.colors['text'],
                        font=('Segoe UI', 10, 'bold'))
    
    def setup_menu(self):
        menu = tk.Menu(self.root)
        self.root.config(menu=menu)
        
        file_menu = tk.Menu(menu, tearoff=0, bg=self.colors['bg'], fg=self.colors['text'])
        menu.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="New Subject", command=self.add_subject)
        file_menu.add_command(label="New Session", command=self.clear_form)
        file_menu.add_separator()
        file_menu.add_command(label="Open Session…", command=self.open_session)
        file_menu.add_command(label="Save Session", command=self.save_entry)
        file_menu.add_separator()
        file_menu.add_command(label="Export Subject Data", command=self.export_subject_data)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.quit)
    
    def load_subjects(self):
        if os.path.exists(self.subjects_file):
            try:
                with open(self.subjects_file, 'r') as f:
                    return json.load(f)
            except:
                return []
        return []
    
    def save_subjects(self):
        with open(self.subjects_file, 'w') as f:
            json.dump(self.subjects, f, indent=2)
    
    def setup_ui(self):
        # Main container
        main_container = tk.Frame(self.root, bg=self.colors['bg'])
        main_container.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # Title
        title_label = tk.Label(main_container,
                               text="🧠 JTF Lab Notes Logger",
                               font=('Segoe UI', 20, 'bold'),
                               bg=self.colors['bg'],
                               fg=self.colors['accent'])
        title_label.pack(pady=(0, 20))
        
        # Content frame
        content_frame = ttk.Frame(main_container, style='Card.TFrame', padding=20)
        content_frame.pack(fill=tk.BOTH, expand=True)
        
        content_frame.columnconfigure(1, weight=1)
        content_frame.rowconfigure(9, weight=1)
        
        # Subject section
        self.create_section_header(content_frame, "📋 Subject Information", 0)
        ttk.Label(content_frame, text="Subject ID:", style='Modern.TLabel').grid(
            row=1, column=0, sticky=tk.W, padx=(0, 10), pady=5)
        self.subject_var = tk.StringVar()
        self.subject_combo = ttk.Combobox(content_frame, textvariable=self.subject_var,
                                          width=25, style='Modern.TCombobox')
        self.subject_combo.grid(row=1, column=1, sticky=(tk.W, tk.E), padx=(0, 10), pady=5)
        self.update_subject_dropdown()
        ttk.Button(content_frame, text="➕ Add New",
                   command=self.add_subject, style='Accent.TButton').grid(
            row=1, column=2, padx=5, pady=5)
        
        # Weight
        ttk.Label(content_frame, text="Weight (g):", style='Modern.TLabel').grid(
            row=2, column=0, sticky=tk.W, padx=(0, 10), pady=5)
        self.weight_var = tk.StringVar()
        ttk.Entry(content_frame, textvariable=self.weight_var,
                  width=15, style='Modern.TEntry').grid(
            row=2, column=1, sticky=tk.W, pady=5)
        
        # Session info
        self.create_section_header(content_frame, "🕒 Session Information", 3)
        ttk.Label(content_frame, text="Date:", style='Modern.TLabel').grid(
            row=4, column=0, sticky=tk.W, padx=(0, 10), pady=5)
        self.date_var = tk.StringVar(value=datetime.now().strftime("%Y-%m-%d"))
        ttk.Entry(content_frame, textvariable=self.date_var,
                  width=15, style='Modern.TEntry').grid(
            row=4, column=1, sticky=tk.W, pady=5)
        
        # Time frame
        time_frame = ttk.Frame(content_frame, style='Card.TFrame')
        time_frame.grid(row=5, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        time_frame.columnconfigure((0, 2), weight=1)
        
        # Start time
        start_frame = ttk.Frame(time_frame, style='Card.TFrame')
        start_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), padx=10)
        ttk.Label(start_frame, text="⏱️ Start Time:", style='Modern.TLabel').pack(anchor=tk.W)
        time_start_container = ttk.Frame(start_frame, style='Card.TFrame')
        time_start_container.pack(fill=tk.X, pady=5)
        self.start_time_var = tk.StringVar()
        ttk.Entry(time_start_container, textvariable=self.start_time_var,
                  width=12, style='Modern.TEntry').pack(side=tk.LEFT)
        ttk.Button(time_start_container, text="Now",
                   command=self.set_start_time, style='Warning.TButton').pack(
            side=tk.LEFT, padx=(5, 0))
        
        # End time
        end_frame = ttk.Frame(time_frame, style='Card.TFrame')
        end_frame.grid(row=0, column=2, sticky=(tk.W, tk.E), padx=10)
        ttk.Label(end_frame, text="⏹️ End Time:", style='Modern.TLabel').pack(anchor=tk.W)
        time_end_container = ttk.Frame(end_frame, style='Card.TFrame')
        time_end_container.pack(fill=tk.X, pady=5)
        self.end_time_var = tk.StringVar()
        ttk.Entry(time_end_container, textvariable=self.end_time_var,
                  width=12, style='Modern.TEntry').pack(side=tk.LEFT)
        ttk.Button(time_end_container, text="Now",
                   command=self.set_end_time, style='Warning.TButton').pack(
            side=tk.LEFT, padx=(5, 0))
        
        # Notes format
        format_frame = ttk.Frame(content_frame, style='Card.TFrame')
        format_frame.grid(row=6, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        ttk.Label(format_frame, text="Notes Format:", style='Modern.TLabel').pack(side=tk.LEFT)
        ttk.Radiobutton(format_frame, text="📊 Table Format", variable=self.notes_format,
                       value="table", command=self.switch_notes_format).pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(format_frame, text="📝 Text Format", variable=self.notes_format,
                       value="text", command=self.switch_notes_format).pack(side=tk.LEFT, padx=10)
        
        # Notes & events section
        self.create_section_header(content_frame, "📝 Experimental Notes & Events", 7)
        self.notes_container = ttk.Frame(content_frame, style='Card.TFrame')
        self.notes_container.grid(row=8, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)
        self.notes_container.columnconfigure(0, weight=1)
        self.notes_container.rowconfigure(2, weight=1)
        self.setup_table_interface()
        self.setup_text_interface()
        self.switch_notes_format()
        
        # Session summary
        ttk.Label(content_frame, text="📋 Session Summary:", style='Modern.TLabel').grid(
            row=9, column=0, sticky=tk.W, pady=(10, 5))
        self.session_notes_text = scrolledtext.ScrolledText(
            content_frame, width=70, height=4, wrap=tk.WORD,
            bg=self.colors['secondary'], fg=self.colors['text'],
            insertbackground=self.colors['text'], font=('Segoe UI', 10)
        )
        self.session_notes_text.grid(row=10, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=5)
    
    def create_section_header(self, parent, text, row):
        header_frame = ttk.Frame(parent, style='Card.TFrame')
        header_frame.grid(row=row, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(15, 5))
        sep = tk.Frame(header_frame, height=2, bg=self.colors['accent'])
        sep.pack(fill=tk.X, pady=(0, 5))
        ttk.Label(header_frame, text=text, style='Header.TLabel').pack(anchor=tk.W)
    
    def update_subject_dropdown(self):
        self.subject_combo['values'] = self.subjects
    
    def add_subject(self):
        dialog = tk.Toplevel(self.root)
        dialog.title("Add New Subject")
        dialog.geometry("400x200")
        dialog.configure(bg=self.colors['bg'])
        dialog.grab_set()
        dialog.resizable(False, False)
        dialog.transient(self.root)
        
        main_frame = ttk.Frame(dialog, style='Card.TFrame', padding=20)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        ttk.Label(main_frame, text="🐭 Enter Subject ID:", style='Header.TLabel').pack(pady=(0, 15))
        subject_entry = ttk.Entry(main_frame, width=30, style='Modern.TEntry', font=('Segoe UI', 12))
        subject_entry.pack(pady=5)
        subject_entry.focus()
        
        def save_subject():
            sid = subject_entry.get().strip()
            if sid:
                if sid not in self.subjects:
                    self.subjects.append(sid)
                    self.save_subjects()
                    self.update_subject_dropdown()
                    self.subject_var.set(sid)
                    dialog.destroy()
                else:
                    messagebox.showwarning("Duplicate", "Subject ID already exists!")
            else:
                messagebox.showwarning("Invalid", "Please enter a subject ID.")
        
        subject_entry.bind('<Return>', lambda e: save_subject())
        btn_frame = ttk.Frame(main_frame, style='Card.TFrame')
        btn_frame.pack(pady=20)
        ttk.Button(btn_frame, text="💾 Save", command=save_subject, style='Success.TButton').pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="❌ Cancel", command=dialog.destroy, style='Warning.TButton').pack(side=tk.LEFT, padx=5)
    
    def set_start_time(self):
        self.start_time_var.set(datetime.now().strftime("%H:%M:%S"))
    
    def set_end_time(self):
        self.end_time_var.set(datetime.now().strftime("%H:%M:%S"))
    
    def setup_table_interface(self):
        # Container for table-format notes
        self.table_frame = ttk.Frame(self.notes_container, style='Card.TFrame')
        
        # Event entry row (no pop-up window)
        entry_row = ttk.Frame(self.table_frame, style='Card.TFrame')
        entry_row.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        entry_row.columnconfigure(3, weight=1)
        
        # Event Time
        ttk.Label(entry_row, text="Time:", style='Modern.TLabel').grid(row=0, column=0, sticky=tk.W, padx=(0,5))
        self.event_time_var = tk.StringVar(value=datetime.now().strftime("%H:%M:%S"))
        ttk.Entry(entry_row, textvariable=self.event_time_var, width=10, style='Modern.TEntry').grid(
            row=0, column=1, sticky=tk.W)
        ttk.Button(entry_row, text="Now", command=self.set_event_time_now, style='Warning.TButton').grid(
            row=0, column=2, padx=(5,15))
        
        # Event Type
        ttk.Label(entry_row, text="Type:", style='Modern.TLabel').grid(row=0, column=3, sticky=tk.W)
        self.event_type_var = tk.StringVar()
        ttk.Combobox(entry_row, textvariable=self.event_type_var, style='Modern.TCombobox',
                     values=['Stimulus','Response','Injection','Measurement','Behavior','Other'],
                     width=15).grid(row=0, column=4, sticky=tk.W, padx=(5,15))
        
        # Description
        ttk.Label(entry_row, text="Desc:", style='Modern.TLabel').grid(row=0, column=5, sticky=tk.W)
        self.event_desc_var = tk.StringVar()
        ttk.Entry(entry_row, textvariable=self.event_desc_var, width=20, style='Modern.TEntry').grid(
            row=0, column=6, sticky=tk.W, padx=(5,15))
        
        # Notes
        ttk.Label(entry_row, text="Notes:", style='Modern.TLabel').grid(row=0, column=7, sticky=tk.W)
        self.event_notes_var = tk.StringVar()
        ttk.Entry(entry_row, textvariable=self.event_notes_var, width=20, style='Modern.TEntry').grid(
            row=0, column=8, sticky=tk.W, padx=(5,15))
        
        # Add Event button
        ttk.Button(entry_row, text="➕ Add Event", command=self.add_table_event_inline, style='Success.TButton').grid(
            row=0, column=9, padx=(5,0))
        
        # Treeview for events
        tree_container = tk.Frame(self.table_frame, bg=self.colors['secondary'], relief='solid', bd=1)
        tree_container.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        cols = ('Event#','Time','Type','Description','Duration','Notes')
        self.events_tree = ttk.Treeview(tree_container, columns=cols, show='headings', style='Modern.Treeview')
        for c in cols:
            anchor = tk.CENTER if c in ('Event#','Time','Duration') else tk.W
            width = 50 if c=='Event#' else 80 if c=='Time' else 80 if c=='Duration' else 120 if c=='Type' else 200
            self.events_tree.heading(c, text=c)
            self.events_tree.column(c, width=width, anchor=anchor)
        
        v_scroll = ttk.Scrollbar(tree_container, orient=tk.VERTICAL, command=self.events_tree.yview)
        h_scroll = ttk.Scrollbar(tree_container, orient=tk.HORIZONTAL, command=self.events_tree.xview)
        self.events_tree.configure(yscrollcommand=v_scroll.set, xscrollcommand=h_scroll.set)
        self.events_tree.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        v_scroll.grid(row=0, column=1, sticky=(tk.N, tk.S))
        h_scroll.grid(row=1, column=0, sticky=(tk.W, tk.E))
        tree_container.columnconfigure(0, weight=1)
        tree_container.rowconfigure(0, weight=1)
        
        # Delete / Clear buttons
        btn_frame = ttk.Frame(self.table_frame, style='Card.TFrame')
        btn_frame.grid(row=2, column=0, pady=10, sticky=tk.E)
        ttk.Button(btn_frame, text="🗑️ Delete Selected", command=self.delete_table_event, style='Warning.TButton').pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="🔄 Clear All Events", command=self.clear_table_events, style='Warning.TButton').pack(side=tk.LEFT, padx=5)
        
        self.table_frame.columnconfigure(0, weight=1)
        self.table_frame.rowconfigure(1, weight=1)
    
    def setup_text_interface(self):
        self.text_frame = ttk.Frame(self.notes_container, style='Card.TFrame')
        controls = ttk.Frame(self.text_frame, style='Card.TFrame')
        controls.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        controls.columnconfigure(0, weight=1)
        btns = ttk.Frame(controls, style='Card.TFrame')
        btns.grid(row=0, column=1, sticky=tk.E)
        ttk.Button(btns, text="➕ Add Event Header", command=self.add_text_event, style='Accent.TButton').pack(side=tk.LEFT, padx=2)
        ttk.Button(btns, text="🔄 Reset Counter", command=self.reset_event_counter, style='Warning.TButton').pack(side=tk.LEFT, padx=2)
        
        container = tk.Frame(self.text_frame, bg=self.colors['secondary'], relief='solid', bd=1)
        container.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)
        self.notes_text = scrolledtext.ScrolledText(
            container, width=70, height=15, wrap=tk.WORD,
            bg=self.colors['secondary'], fg=self.colors['text'],
            insertbackground=self.colors['text'], selectbackground=self.colors['accent'],
            font=('Consolas', 11), relief='flat', bd=10
        )
        self.notes_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.text_frame.columnconfigure(0, weight=1)
        self.text_frame.rowconfigure(1, weight=1)
    
    def switch_notes_format(self):
        self.table_frame.grid_remove()
        self.text_frame.grid_remove()
        if self.notes_format.get() == "table":
            self.table_frame.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        else:
            self.text_frame.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
            if not self.notes_text.get("1.0", tk.END).strip():
                self.add_text_event()
    
    def set_event_time_now(self):
        self.event_time_var.set(datetime.now().strftime("%H:%M:%S"))
    
    def add_table_event_inline(self):
        # Read event fields
        time_str = self.event_time_var.get().strip()
        evt_type = self.event_type_var.get().strip()
        desc = self.event_desc_var.get().strip()
        notes = self.event_notes_var.get().strip()
        
        if not evt_type or not desc:
            messagebox.showwarning("Missing Data", "Please fill in Event Type and Description.")
            return
        
        # Parse current event time
        try:
            evt_time = datetime.strptime(time_str, "%H:%M:%S")
        except ValueError:
            messagebox.showerror("Invalid Time", "Event time must be HH:MM:SS.")
            return
        
        # Compute duration
        if self.current_session_events:
            prev_time = datetime.strptime(self.current_session_events[-1]['time'], "%H:%M:%S")
            duration = (evt_time - prev_time).total_seconds()
            if duration < 0:
                messagebox.showerror("Time Error", "Event time is earlier than previous event.")
                return
        else:
            duration = 0.0  # first event
        
        # Add to Treeview
        self.events_tree.insert('', 'end', values=(
            f"{self.current_event_number:02d}",
            time_str,
            evt_type,
            desc,
            f"{duration:.1f}",
            notes
        ))
        
        # Store in current_session_events
        self.current_session_events.append({
            'event_number': self.current_event_number,
            'time': time_str,
            'type': evt_type,
            'description': desc,
            'duration': f"{duration:.1f}",
            'notes': notes
        })
        self.current_event_number += 1
        
        # Clear the inline fields for next entry
        self.event_type_var.set('')
        self.event_desc_var.set('')
        self.event_notes_var.set('')
        self.set_event_time_now()
    
    def delete_table_event(self):
        selected = self.events_tree.selection()
        if selected:
            for item in selected:
                vals = self.events_tree.item(item, 'values')
                evt_num = int(vals[0])
                # Remove from list
                self.current_session_events = [
                    e for e in self.current_session_events if e['event_number'] != evt_num
                ]
                self.events_tree.delete(item)
            # Recompute event numbers and durations
            for idx, evt in enumerate(self.current_session_events, start=1):
                evt['event_number'] = idx
            self.refresh_table()
    
    def clear_table_events(self):
        if messagebox.askyesno("Clear Events", "Clear all events?"):
            self.events_tree.delete(*self.events_tree.get_children())
            self.current_session_events.clear()
            self.current_event_number = 1
    
    def add_text_event(self):
        now = datetime.now().strftime("%H:%M:%S")
        header = f"\n{'='*50}\nEVENT {self.current_event_number:02d} | {now}\n{'='*50}\n"
        self.notes_text.insert(tk.END, header)
        self.notes_text.mark_set(tk.INSERT, tk.END)
        self.notes_text.focus()
        self.current_event_number += 1
    
    def reset_event_counter(self):
        self.current_event_number = 1
        messagebox.showinfo("Reset", "Event counter reset to 1")
    
    def refresh_table(self):
        # Clear and re-populate Treeview from self.current_session_events
        self.events_tree.delete(*self.events_tree.get_children())
        for idx, evt in enumerate(self.current_session_events, start=1):
            # Recompute duration if not first
            if idx > 1:
                prev_time = datetime.strptime(self.current_session_events[idx-2]['time'], "%H:%M:%S")
                curr_time = datetime.strptime(evt['time'], "%H:%M:%S")
                duration = (curr_time - prev_time).total_seconds()
            else:
                duration = 0.0
            evt['event_number'] = idx
            evt['duration'] = f"{duration:.1f}"
            self.events_tree.insert('', 'end', values=(
                f"{idx:02d}",
                evt['time'],
                evt['type'],
                evt['description'],
                f"{duration:.1f}",
                evt['notes']
            ))
        self.current_event_number = len(self.current_session_events) + 1
    
    def clear_form(self):
        if not messagebox.askyesno("New Session", "Clear all fields and start a new session?"):
            return
        self.subject_var.set('')
        self.weight_var.set('')
        self.date_var.set(datetime.now().strftime("%Y-%m-%d"))
        self.start_time_var.set('')
        self.end_time_var.set('')
        self.session_notes_text.delete("1.0", tk.END)
        if self.notes_format.get() == "table":
            self.events_tree.delete(*self.events_tree.get_children())
            self.current_session_events.clear()
            self.current_event_number = 1
        else:
            self.notes_text.delete("1.0", tk.END)
            self.current_session_events.clear()
            self.current_event_number = 1
    
    def save_entry(self):
        subject_id = self.subject_var.get().strip()
        weight = self.weight_var.get().strip()
        date_str = self.date_var.get().strip()
        start_time = self.start_time_var.get().strip()
        end_time = self.end_time_var.get().strip()
        session_notes = self.session_notes_text.get("1.0", tk.END).strip()
        
        if not subject_id:
            messagebox.showwarning("Missing Data", "Select a Subject ID.")
            return
        if not weight:
            messagebox.showwarning("Missing Data", "Enter weight.")
            return
        if not date_str:
            messagebox.showwarning("Missing Data", "Enter date.")
            return
        if not start_time or not end_time:
            messagebox.showwarning("Missing Data", "Set start and end times.")
            return
        
        # Validate date format
        try:
            datetime.strptime(date_str, "%Y-%m-%d")
        except ValueError:
            messagebox.showerror("Invalid Date", "Date must be YYYY-MM-DD.")
            return
        
        # Ensure subject folder exists
        subj_dir = os.path.join(self.base_data_dir, subject_id)
        os.makedirs(subj_dir, exist_ok=True)
        
        # File path: data/{subject}/{date}.csv
        filename = f"{date_str}.csv"
        file_path = os.path.join(subj_dir, filename)
        
        # If file exists, confirm overwrite
        if os.path.exists(file_path):
            if not messagebox.askyesno("Overwrite?", f"A file for {subject_id} on {date_str} exists.\nOverwrite?"):
                return
        
        # Before writing, update last event's duration to (end_time - last_event_time)
        if self.current_session_events:
            last_evt = self.current_session_events[-1]
            try:
                last_time = datetime.strptime(last_evt['time'], "%H:%M:%S")
                end_dt = datetime.strptime(end_time, "%H:%M:%S")
                final_duration = (end_dt - last_time).total_seconds()
                if final_duration >= 0:
                    last_evt['duration'] = f"{final_duration:.1f}"
                else:
                    messagebox.showwarning("Time Warning", "End time is before last event time.")
            except ValueError:
                pass
        
        total_events = len(self.current_session_events)
        
        # Write entire CSV: header block + event table
        try:
            with open(file_path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                
                # Metadata header rows
                writer.writerow(["subject_id", subject_id])
                writer.writerow(["date", date_str])
                writer.writerow(["weight_grams", weight])
                writer.writerow(["start_time", start_time])
                writer.writerow(["end_time", end_time])
                writer.writerow(["session_notes", session_notes])
                writer.writerow(["total_events", total_events])
                
                # Blank line
                writer.writerow([])
                
                # Event table header
                writer.writerow(["event_number", "event_time", "event_type",
                                 "description", "duration_sec", "notes"])
                
                # Event rows
                for evt in self.current_session_events:
                    writer.writerow([
                        evt['event_number'],
                        evt['time'],
                        evt['type'],
                        evt['description'],
                        evt['duration'],
                        evt['notes']
                    ])
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save file:\n{e}")
            return
        
        messagebox.showinfo("Saved", f"Session saved to:\n{file_path}")
        self.clear_form()
    
    def open_session(self):
        # Let user pick a CSV to load
        path = filedialog.askopenfilename(
            title="Open Session CSV",
            filetypes=[("CSV Files", "*.csv")],
            initialdir=self.base_data_dir
        )
        if not path:
            return
        try:
            with open(path, 'r', encoding='utf-8') as f:
                reader = csv.reader(f)
                rows = list(reader)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to open file:\n{e}")
            return
        
        # Parse header: expect key,value per row until blank line
        meta = {}
        idx = 0
        while idx < len(rows):
            row = rows[idx]
            if not row:  # blank line
                idx += 1
                break
            if len(row) >= 2:
                key, val = row[0], row[1]
                meta[key] = val
            idx += 1
        
        # Verify required metadata
        required = ['subject_id','date','weight_grams','start_time','end_time','session_notes']
        if not all(k in meta for k in required):
            messagebox.showerror("Format Error", "CSV header is not in expected format.")
            return
        
        # Populate fields
        sid = meta['subject_id']
        if sid not in self.subjects:
            self.subjects.append(sid)
            self.save_subjects()
        self.subject_var.set(sid)
        self.update_subject_dropdown()
        
        self.date_var.set(meta['date'])
        self.weight_var.set(meta['weight_grams'])
        self.start_time_var.set(meta['start_time'])
        self.end_time_var.set(meta['end_time'])
        self.session_notes_text.delete("1.0", tk.END)
        self.session_notes_text.insert(tk.END, meta['session_notes'])
        
        # Clear existing events
        self.events_tree.delete(*self.events_tree.get_children())
        self.current_session_events.clear()
        self.current_event_number = 1
        
        # Parse event table
        # rows[idx] is header: ["event_number", ...]
        idx += 1
        while idx < len(rows):
            row = rows[idx]
            if len(row) < 6:
                idx += 1
                continue
            num, time_str, evt_type, desc, duration, notes = row[:6]
            try:
                num_int = int(num)
            except:
                idx += 1
                continue
            self.current_session_events.append({
                'event_number': num_int,
                'time': time_str,
                'type': evt_type,
                'description': desc,
                'duration': duration,
                'notes': notes
            })
            idx += 1
        
        # Re-populate Treeview
        self.refresh_table()
        messagebox.showinfo("Loaded", f"Session loaded from:\n{path}")
    
    def export_subject_data(self):
        subject_id = self.subject_var.get().strip()
        if not subject_id:
            messagebox.showwarning("Missing Subject", "Select a Subject ID first.")
            return
        subj_dir = os.path.join(self.base_data_dir, subject_id)
        if not os.path.exists(subj_dir):
            messagebox.showerror("No Data", f"No data folder for subject:\n{subject_id}")
            return
        
        dest = filedialog.askdirectory(title="Select export folder")
        if not dest:
            return
        try:
            shutil.copytree(subj_dir,
                            os.path.join(dest, subject_id),
                            dirs_exist_ok=True)
            messagebox.showinfo("Exported", f"Subject data copied to:\n{os.path.join(dest, subject_id)}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to export:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = NotesLogger(root)
    root.mainloop()
