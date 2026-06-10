import os
import sys
import time
import logging
import paramiko
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from PIL import Image, ImageTk
from scp import SCPClient
import time
import textwrap
import subprocess
import tempfile
import threading
import queue
from google.cloud import storage
from google.oauth2 import service_account


def resource_path(relative_path, subfolder=None):
    """
    Get absolute path to resource, works for dev and for PyInstaller
    """
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    
    if subfolder:
        return os.path.join(base_path, subfolder, relative_path)
    else:
        return os.path.join(base_path, relative_path)

class RouterConfigGUI:
    def __init__(self):
        self.setup_logging()
        self.setup_gui()
        self.load_logo()

        # Set default IPK path to None (will be populated during download)
        self.ipk_path_value = None


    def setup_logging(self):
        logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s', datefmt='%Y-%m-%d %H:%M:%S')
        self.logger = logging.getLogger(__name__)
    
    def setup_gui(self):
        self.root = tk.Tk()
        self.root.title("Frontop Geosonic Sensor - Router Configuration Tool")
        self.root.geometry("750x900")  # Increased height to accommodate VM settings
        
        # Create main frame with padding
        main_frame = ttk.Frame(self.root, padding="20")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        
        # Title at the top
        title_label = ttk.Label(main_frame, text="Vibration Monitor Configuration", 
                              font=("Arial", 14, "bold"))
        title_label.grid(row=0, column=0, columnspan=3, sticky=tk.W, pady=(0, 20))

        # Router Password
        password_frame = ttk.LabelFrame(main_frame, text="Router Settings", padding="10")
        password_frame.grid(row=1, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(password_frame, text="Router Password:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        self.password = ttk.Entry(password_frame, show="*", width=40)
        self.password.grid(row=0, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        # Monitoring Mode
        monitor_frame = ttk.LabelFrame(main_frame, text="Monitoring Settings", padding="10")
        monitor_frame.grid(row=2, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(monitor_frame, text="Monitoring Interval:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        self.monitoring_mode = ttk.Combobox(monitor_frame, width=37, state="readonly")
        self.monitoring_mode['values'] = (
            "Every 5 minutes", 
            "Every 10 minutes", 
            "Every 15 minutes (Default)"
        )
        self.monitoring_mode.current(2)  # Default to 15 minutes
        self.monitoring_mode.grid(row=0, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        # API key inputs entered directly in the GUI
        env_frame = ttk.LabelFrame(main_frame, text="API Key Settings", padding="10")
        env_frame.grid(row=3, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)

        ttk.Label(env_frame, text="Vibration API Key:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        self.vibration_api_key = ttk.Entry(env_frame, show="*", width=40)
        self.vibration_api_key.grid(row=0, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(env_frame, text="Sensor API Key:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=5)
        self.sensor_api_key = ttk.Entry(env_frame, show="*", width=40)
        self.sensor_api_key.grid(row=1, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(env_frame, text="IPK Bucket API Key:").grid(row=2, column=0, sticky=tk.W, padx=5, pady=5)
        self.ipk_bucket_api_key = ttk.Entry(env_frame, show="*", width=40)
        self.ipk_bucket_api_key.grid(row=2, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        # VM Settings
        vm_frame = ttk.LabelFrame(main_frame, text="VM Ingestion Settings", padding="10")
        vm_frame.grid(row=4, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(vm_frame, text="VM Endpoint:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        self.vm_endpoint = ttk.Entry(vm_frame, width=40)
        self.vm_endpoint.grid(row=0, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))
        
        ttk.Label(vm_frame, text="VM API Key:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=5)
        self.vm_api_key = ttk.Entry(vm_frame, show="*", width=40)
        self.vm_api_key.grid(row=1, column=1, padx=5, pady=5, sticky=(tk.W, tk.E))

        # Status Frame
        status_frame = ttk.LabelFrame(main_frame, text="Installation Status", padding="10")
        status_frame.grid(row=5, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=10)
        status_frame.columnconfigure(0, weight=1)
        status_frame.rowconfigure(0, weight=1)
        
        # Add scrollbar to status text
        status_scroll = ttk.Scrollbar(status_frame)
        status_scroll.grid(row=0, column=1, sticky=(tk.N, tk.S))
        
        # Status Text with scrollbar
        self.status_text = tk.Text(status_frame, height=10, width=60, wrap=tk.WORD,
                                 yscrollcommand=status_scroll.set)
        self.status_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), padx=(0, 5))
        status_scroll.config(command=self.status_text.yview)
        self.status_text.configure(state='disabled')
        
        # Configure the status frame to expand
        main_frame.columnconfigure(0, weight=1)
        main_frame.rowconfigure(5, weight=1)

        # Progress Bar
        progress_frame = ttk.Frame(main_frame)
        progress_frame.grid(row=6, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=10)
        
        ttk.Label(progress_frame, text="Progress:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.progress = ttk.Progressbar(progress_frame, length=400, mode='determinate')
        self.progress.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=5)
        progress_frame.columnconfigure(1, weight=1)

        # Button Frame
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=7, column=0, columnspan=3, pady=10)

        # Style configuration for buttons
        style = ttk.Style()
        style.configure("Install.TButton", font=("Arial", 11))

        # Install Router Button
        install_button = ttk.Button(button_frame, text="Install Router", 
                                command=self.run_installation,
                                style="Install.TButton",
                                width=15)
        install_button.pack(side=tk.LEFT, padx=10)

        # Sensor Setup Button with Note
        sensor_setup_button = ttk.Button(button_frame, text="Setup Machine", 
                                    command=self.run_sensor_setup,
                                    style="Install.TButton",
                                    width=15)
        sensor_setup_button.pack(side=tk.LEFT, padx=10)
    def run_sensor_setup(self):
        # Show a popup message about router configuration prerequisite
        messagebox.showinfo("Setup Sensor Prerequisites", 
            "IMPORTANT: You must first run the Router Installer before setting up the sensor.\n\n"
            "Please ensure:\n"
            "1. Router has been configured with 'Install Router'\n"
            "2. Serial cable is connected from router to the Geosonic sensor\n"
            "3. Port light will turn red when sensor setup is running")
        
        if not self.password.get():
            messagebox.showerror("Error", "Router password is required!")
            return
        
        # Confirm before proceeding
        confirm = messagebox.askyesno("Confirm Sensor Setup", 
            "Are you ready to proceed with sensor setup?")
        
        if confirm:
            installer = GeoSonicInstaller(self)
            installer.start_sensor_setup()
    
    def _run_sensor_setup_thread(self):
        """Sensor setup logic running in a separate thread"""
        try:
            # Capture return value or success/failure status
            success = False
            error_message = "Unknown error occurred"
            
            try:
                hostname, username, password = self.setup_ssh_config()
                
                # Connect to router via SSH
                ssh_client = self.test_ssh_connection(hostname, username, password)
                if not ssh_client:
                    raise Exception("SSH Connection Failed")
                
                # Run sensor setup utility
                success = self.install_and_run_sensor_setup(ssh_client)
                
                # Clean up and close connection
                ssh_client.close()
                
                if success:
                    error_message = "Sensor setup completed successfully"
            except Exception as e:
                error_message = str(e)
            
            # Put final status in queue
            self.log_queue.put(('final', success, error_message))
        
        except Exception as e:
            # Catch any unexpected errors
            self.log_queue.put(('final', False, str(e)))
        if not self.password.get():
            messagebox.showerror("Error", "Router password is required!")
            return
        
        # Confirm before proceeding
        confirm = messagebox.askyesno("Confirm Sensor Setup", 
            "Have you:\n1. Configured the router?\n2. Connected the serial port?\n3. Verified the port light is RED?")
        
        if confirm:
            installer = GeoSonicInstaller(self)
            installer.start_sensor_setup()

    def install_and_run_sensor_setup(self, client=None):
        """
        Method to maintain backwards compatibility with any existing code
        that might call this method directly
        """
        return self.run_sensor_setup()
    
    def download_latest_ipk(self):
        """Download the latest IPK file from Google Cloud Storage bucket using a service account"""
        try:
            credentials_path = resource_path('credentials.json','assets')
            bucket_name = "vibration-monitor-ipk"
            self.log_to_gui("Checking for the latest IPK package...", info=True)
            self.progress['value'] = 5
            
            # Create a temp directory
            temp_dir = tempfile.gettempdir()
            
            # First, download the text file containing the latest filename
            version_file = "latest-ipk-version.txt"  # Keep the required filename
            version_file_local = os.path.join(temp_dir, version_file)
            
            # Create a file lock handler function for safely handling the file
            def safely_handle_file(file_path, action_func):
                max_attempts = 3
                wait_time = 2  # seconds
                
                for attempt in range(max_attempts):
                    try:
                        # Try to perform the action
                        result = action_func(file_path)
                        return True, result
                    except IOError as e:
                        if max_attempts - 1:
                            self.log_to_gui(f"File access issue, retrying in {wait_time} seconds... ({attempt+1}/{max_attempts})", warning=True)
                            time.sleep(wait_time)
                        else:
                            self.log_to_gui(f"File access issue after {max_attempts} attempts: {str(e)}", error=True)
                            return False, None
            
            # Load credentials from the service account JSON key file
            credentials = service_account.Credentials.from_service_account_file(credentials_path)
            storage_client = storage.Client(credentials=credentials)
            bucket = storage_client.bucket(bucket_name)
            
            # Get the version file blob
            version_blob = bucket.blob(version_file)
            
            # First, check if the file exists and try to delete it if it does
            if os.path.exists(version_file_local):
                # Define the delete function
                def delete_file(path):
                    os.remove(path)
                    return None
                
                # Try to delete the existing file
                success, _ = safely_handle_file(version_file_local, delete_file)
                if not success:
                    self.log_to_gui("Warning: Could not clear existing version file, will try to continue", warning=True)
            
            # Download the version file content
            self.log_to_gui("Retrieving information about latest version...")
            
            try:
                version_blob.download_to_filename(version_file_local)
                with open(version_file_local, 'r') as f:
                    latest_ipk_filename = f.read().strip()
            except Exception as e:
                self.log_to_gui(f"Error retrieving latest version info: {str(e)}", error=True)
                self.log_to_gui("Falling back to default IPK filename", info=True)
                latest_ipk_filename = "vibration-monitor_latest.ipk"  # Fallback filename
            
            if not latest_ipk_filename.endswith('.ipk'):
                self.log_to_gui(f"Adding .ipk extension to filename", info=True) 
                latest_ipk_filename = latest_ipk_filename + ".ipk"
                
            self.log_to_gui(f"Downloading version: {latest_ipk_filename}", info=True)
            self.progress['value'] = 10
            
            # Now download the actual IPK file
            local_path = os.path.join(temp_dir, os.path.basename(latest_ipk_filename))
            ipk_blob = bucket.blob(latest_ipk_filename)
            
            # If the file already exists, try to delete it first
            if os.path.exists(local_path):
                def delete_ipk(path):
                    os.remove(path)
                    return None
                
                success, _ = safely_handle_file(local_path, delete_ipk)
                if not success:
                    # If we can't delete it, try to use a slightly different name
                    local_path = os.path.join(temp_dir, f"new_{os.path.basename(latest_ipk_filename)}")
                    self.log_to_gui(f"Using alternative file path: {local_path}", warning=True)
            
            # Download the IPK file
            self.log_to_gui("Downloading IPK package...")
            ipk_blob.download_to_filename(local_path)
            self.progress['value'] = 15
            
            # Verify the file was downloaded and isn't an error message
            if os.path.exists(local_path) and os.path.getsize(local_path) > 0:
                # Define a function to read the file header
                def read_file_header(path):
                    with open(path, 'rb') as f:
                        return f.read(100)  # Read first 100 bytes
                
                # Safely read the file header
                success, file_header = safely_handle_file(local_path, read_file_header)
                
                if not success:
                    self.log_to_gui("Could not verify downloaded file", error=True)
                    return False
                
                # Check if the downloaded file is actually an error XML
                if b'<?xml' in file_header and b'<Error>' in file_header:
                    self.log_to_gui("Authentication error downloading IPK file", error=True)
                    return False
                
                # Define a function to get file size
                def get_file_size(path):
                    return os.path.getsize(path)
                
                # Safely get the file size
                success, file_size = safely_handle_file(local_path, get_file_size)
                
                if success:
                    file_size = file_size / 1024  # Size in KB
                    self.log_to_gui(f"IPK file downloaded successfully ({file_size:.1f} KB)", info=True)
                else:
                    self.log_to_gui("IPK file downloaded successfully", info=True)
                
                # Store the IPK path for installation
                self.ipk_path_value = local_path  
                return True
            else:
                self.log_to_gui("Downloaded file is empty or doesn't exist", error=True)
                return False
                
        except Exception as e:
            self.log_to_gui(f"Error downloading IPK file: {str(e)}", error=True)
            return False

    def load_logo(self):
        """
        Load and display the Frontop logo with robust error handling.
        
        Attempts to load the logo from the specified path, resizes it,
        and displays it in the top-right corner of the main window.
        Logs any errors encountered during logo loading.
        """
        try:
            # Attempt to find the logo image
            image_path = resource_path('frontop_logo.jpg', 'images')
            
            # Check if the file exists before attempting to open
            if not os.path.exists(image_path):
                self.logger.warning(f"Logo file not found: {image_path}")
                return
            
            # Open and resize the image
            with Image.open(image_path) as img:
                # Maintain aspect ratio while resizing
                img.thumbnail((100, 100), Image.Resampling.LANCZOS)
                
                # Convert to PhotoImage
                self.logo = ImageTk.PhotoImage(img)

            # Create a label with the logo
            logo_label = tk.Label(self.root, image=self.logo, background=self.root.cget('background'))
            
            # Position the logo in the top-right corner
            logo_label.grid(row=0, column=0, sticky="ne", padx=20, pady=20)

        except FileNotFoundError:
            self.logger.error(f"Logo file not found: {image_path}")
        except Image.UnidentifiedImageError:
            self.logger.error(f"Unable to identify image file: {image_path}")
        except Exception as e:
            self.logger.error(f"Error loading logo: {str(e)}")

    def log_to_gui(self, message, error=False, info=False):
        self.status_text.configure(state='normal')  # Temporarily enable editing
        
        # Add timestamp
        timestamp = time.strftime("%H:%M:%S", time.localtime())
        formatted_message = f"[{timestamp}] {message}\n"
        
        self.status_text.insert(tk.END, formatted_message)
        self.status_text.see(tk.END)
        
        # Add tags for coloring
        last_line_start = self.status_text.index(f"end-{len(formatted_message)+1}c linestart")
        last_line_end = self.status_text.index(f"end-1c")
        if error:
            self.status_text.tag_add("error", last_line_start, last_line_end)
            self.status_text.tag_configure("error", foreground="#d32f2f")  # Dark red
        elif info:
            self.status_text.tag_add("info", last_line_start, last_line_end)
            self.status_text.tag_configure("info", foreground="#1976d2")  # Dark blue
        
        self.status_text.configure(state='disabled')  # Disable editing again
        self.root.update()
        
    def validate_inputs(self):
        if not self.password.get():
            messagebox.showerror("Error", "Router password is required!")
            return False
            
        # Check if API keys are entered in the GUI
        if not self.vibration_api_key.get().strip():
            messagebox.showerror("Error", "Vibration Data API Key is required!")
            return False
        if not self.sensor_api_key.get().strip():
            messagebox.showerror("Error", "Sensor List API Key is required!")
            return False
        if not self.ipk_bucket_api_key.get().strip():
            messagebox.showerror("Error", "IPK Bucket API Key is required!")
            return False

        # Mirror GUI values into the process environment for any code paths that still use os.getenv
        os.environ["VIBRATION_API_KEY"] = self.vibration_api_key.get().strip()
        os.environ["SENSOR_API_KEY"] = self.sensor_api_key.get().strip()
        os.environ["IPK_BUCKET_API_KEY"] = self.ipk_bucket_api_key.get().strip()
        
        # Download the latest IPK file before installation
        if not self.download_latest_ipk():
            messagebox.showerror("Error", "Failed to download the latest IPK file!")
            return False
            
        return True

    def run_installation(self):
        if not self.validate_inputs():
            return
        
        installer = GeoSonicInstaller(self)  # Pass the existing GUI instance
        installer.start_installation()

class GeoSonicInstaller:
    def __init__(self, gui):
        self.gui = gui  # Link to the GUI for logging
        self.log_queue = None

    def start_sensor_setup(self):
        """Start the sensor setup process in a separate thread"""
        # Create a queue for thread-safe logging
        self.log_queue = queue.Queue()
        
        # Disable buttons during setup
        self.disable_install_button()
        
        # Start a thread for the sensor setup process
        sensor_setup_thread = threading.Thread(target=self._run_sensor_setup_thread, daemon=True)
        sensor_setup_thread.start()
        
        # Start a periodic check to process log messages and update GUI
        self.gui.root.after(100, self._check_log_queue)

    def _run_sensor_setup_thread(self):
        """Sensor setup logic running in a separate thread"""
        try:
            # Capture return value or success/failure status
            success = False
            error_message = "Unknown error occurred"
            
            try:
                hostname, username, password = self.setup_ssh_config()
                
                # Connect to router via SSH
                ssh_client = self.test_ssh_connection(hostname, username, password)
                if not ssh_client:
                    raise Exception("SSH Connection Failed")
                
                # Run sensor setup utility
                success = self.install_and_run_sensor_setup(ssh_client)
                
                # Clean up and close connection
                ssh_client.close()
                
                if success:
                    error_message = "Sensor setup completed successfully"
            except Exception as e:
                error_message = str(e)
            
            # Put final status in queue
            self.log_queue.put(('final', success, error_message))
        
        except Exception as e:
            # Catch any unexpected errors
            self.log_queue.put(('final', False, str(e)))
        if not self.password.get():
            messagebox.showerror("Error", "Router password is required!")
            return
        
        # Confirm before proceeding
        confirm = messagebox.askyesno("Confirm Sensor Setup", 
            "Have you:\n1. Configured the router?\n2. Connected the serial port?\n3. Verified the port light is RED?")
        
        if confirm:
            self.start_sensor_setup()
    
    def start_installation(self):
        """Start the installation process in a separate thread"""
        # Create a queue for thread-safe logging
        self.log_queue = queue.Queue()
        
        # Disable the install button during installation
        self.disable_install_button()
        
        # Start a thread for the installation process
        installation_thread = threading.Thread(target=self._run_installation_thread, daemon=True)
        installation_thread.start()
        
        # Start a periodic check to process log messages and update GUI
        self.gui.root.after(100, self._check_log_queue)

    def _run_installation_thread(self):
        """Actual installation logic running in a separate thread"""
        try:
            # Capture return value or success/failure status
            success = False
            error_message = "Unknown error occurred"
            
            try:
                hostname, username, password = self.setup_ssh_config()
                
                # Connect to router via SSH
                ssh_client = self.test_ssh_connection(hostname, username, password)
                if not ssh_client:
                    raise Exception("SSH Connection Failed")
                
                # Transfer IPK file
                self.scp_transfer(ssh_client, self.gui.ipk_path_value, "/tmp/")
                
                # Create and transfer config file
                config_path = self.create_config_file(ssh_client)
                self.scp_transfer(ssh_client, config_path, "/etc/vibration.conf")
                
                # Install packages
                self.install_packages(ssh_client)
                
                # Install vibration monitor
                ipk_filename = os.path.basename(self.gui.ipk_path_value)
                package_name = "vibration-monitor"  # Assuming package name
                self.install_package(ssh_client, package_name, self.gui.ipk_path_value)
                
                # Set up cron jobs
                self.set_cron_jobs_on_router(ssh_client)

                #GPIO script setup
                #self.install_and_enable_gpio(ssh_client)
                # Clean up and close connection
                ssh_client.close()
                
                success = True
            except Exception as e:
                error_message = str(e)
            
            # Put final status in queue
            self.log_queue.put(('final', success, error_message))
        
        except Exception as e:
            # Catch any unexpected errors
            self.log_queue.put(('final', False, str(e)))

    def _check_log_queue(self):
        """Check the log queue and update GUI"""
        try:
            # Process all available log messages
            while True:
                try:
                    # Use a non-blocking get with a timeout
                    log_type, *log_args = self.log_queue.get(block=False)
                    
                    if log_type == 'log':
                        # Handle logging messages
                        message, error, info = log_args
                        self.gui.log_to_gui(message, error=error, info=info)
                    
                    elif log_type == 'progress':
                        # Handle progress updates
                        progress_value = log_args[0]
                        self.gui.root.after(0, lambda pv=progress_value: setattr(self.gui.progress, 'value', pv))
                    
                    elif log_type == 'final':
                        # Handle final installation status
                        success, error_message = log_args
                        
                        # Re-enable the install button
                        self.gui.root.after(0, self.enable_install_button)
                        
                        # Show final result
                        if success:
                            self.gui.root.after(0, lambda: messagebox.showinfo("Success", "Router configuration has been completed successfully!"))
                            self.gui.root.after(0, lambda: self.gui.progress.configure(value=100))
                        else:
                            self.gui.root.after(0, lambda: messagebox.showerror("Installation Failed", error_message))
                            self.gui.root.after(0, lambda: self.gui.progress.configure(value=0))
                    # Mark task as done
                    self.log_queue.task_done()
                
                except queue.Empty:
                    # No more items in the queue
                    break
        
        except Exception as e:
            print(f"Error in log queue processing: {e}")
        
        # Continue checking the queue periodically
        self.gui.root.after(100, self._check_log_queue)

    def disable_install_button(self):
        """Disable the install button during installation"""
        for widget in self.gui.root.winfo_children():
            if isinstance(widget, ttk.Button) and widget.cget('text') == 'Install':
                widget.config(state='disabled')
                break

    def enable_install_button(self):
        """Re-enable the install button after installation"""
        for widget in self.gui.root.winfo_children():
            if isinstance(widget, ttk.Button) and widget.cget('text') == 'Install':
                widget.config(state='normal')
                break

    def log_to_threadsafe(self, message, error=False, info=False):
        """Thread-safe logging method"""
        # Put the log message in the queue
        if self.log_queue:
            self.log_queue.put(('log', message, error, info))

    def setup_ssh_config(self):
        hostname = "192.168.8.1"  # Change to your router's IP or hostname
        username = "root"  # Router's SSH username
        password = self.gui.password.get()  # Get password from GUI
        return hostname, username, password

    def test_ssh_connection(self, hostname, username, password):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            self.log_to_threadsafe(f"Connecting to {hostname} via SSH...")
            client.connect(hostname, username=username, password=password, look_for_keys=False, allow_agent=False)
            self.log_queue.put(('progress', 20))
            self.log_to_threadsafe("SSH connection successful!")
            return client
        except paramiko.AuthenticationException:
            self.log_to_threadsafe("Authentication failed, please check if router password is entered correctly.", error=True)
        except paramiko.SSHException as e:
            self.log_to_threadsafe(f"SSH error: {e}", error=True)
        except Exception as e:
            self.log_to_threadsafe(f"Unexpected error: {e}", error=True)
        return None

    def scp_transfer(self, client, local_file, remote_path):
        try:
            # Check if the local file exists
            if not os.path.exists(local_file):
                self.log_to_threadsafe(f"Error: {local_file} does not exist!", error=True)
                return
            
            self.log_to_threadsafe(f"Transferring {local_file} to {remote_path} on the router...")
            
            # SCP transfer for a single file
            with SCPClient(client.get_transport()) as scp:
                scp.put(local_file, remote_path)
            
            self.log_to_threadsafe("File transferred successfully!")
            self.log_queue.put(('progress', 30))
        except Exception as e:
            self.log_to_threadsafe(f"SCP file transfer failed: {str(e)}", error=True)

    
    def get_mac_id(self, client, interface="eth1"):
        """Get the MAC address of the router for device identification"""
        # Updated command to match "HWaddr XX:XX:XX:XX:XX:XX" format
        mac_command = f"ifconfig {interface} | grep 'HWaddr' | awk '{{print $5}}'"
        
        mac_output = self.execute_ssh_command(client, mac_command)
        mac_address = mac_output.strip()
        
        if not mac_address:
            return None

        return mac_address

    def create_config_file(self,client):
        """Generates the vibration.conf file that will be uploaded onto the router based on user input"""
        # Get API keys from the GUI fields
        api_key = self.gui.vibration_api_key.get().strip()
        sensor_api_key = self.gui.sensor_api_key.get().strip()
        vm_endpoint = self.gui.vm_endpoint.get()
        vm_api_key = self.gui.vm_api_key.get()

        config_content = f"""\
# Vibration monitor configuration
tty_device=/dev/ttyUSB4
threshold=1.5
data_dir=/tmp/vibration_data
clear_events=true
cloud_function_url=https://insert-vibration-data-836427764358.northamerica-northeast2.run.app
api_key={api_key}
get_sensor_cloud_url=https://sensor-key-retrieval-836427764358.northamerica-northeast2.run.app
get_sensor_api_key={sensor_api_key}
vm_endpoint={vm_endpoint}
vm_api_key={vm_api_key}
device_id={self.get_mac_id(client)}
"""

        # Remove unwanted indentation using textwrap.dedent
        config_content = textwrap.dedent(config_content).strip() + "\n"

        config_path = os.path.join(os.getcwd(), "vibration.conf")

        with open(config_path, "w", newline="\n") as file:
            file.write(config_content)

        self.log_to_threadsafe(f"Configuration file created at {config_path}")
        self.log_queue.put(('progress', 40))
        return config_path

    def execute_ssh_command(self, client, command):
        """Execute a command via SSH and return the output"""
        try:
            stdin, stdout, stderr = client.exec_command(command)
            output = stdout.read().decode()
            return output
        except Exception as e:
            self.log_to_threadsafe(f"Error executing SSH command: {str(e)}", error=True)
            return None

    def install_packages(self, client):
        """Install the necessary dependencies and packages on the router"""
        commands = [
            "cd /tmp",
            "opkg update",
            "opkg install libstdcpp6",
            "opkg install kmod-usb-serial-pl2303",
            "opkg install kmod-usb-serial-ftdi"
        ]

        for command in commands:
            try:
                self.log_to_threadsafe(f"Executing command: {command}")
                stdin, stdout, stderr = client.exec_command(command)

                # Read output in real time
                while not stdout.channel.exit_status_ready():
                    time.sleep(0.1)  # Small delay to avoid high CPU usage
                    output_line = stdout.readline()
                    if output_line:
                        self.log_to_threadsafe(output_line.strip())

                # Capture any errors
                error = stderr.read().decode('utf-8').strip()
                if error:
                    self.log_to_threadsafe(f"Error: {error}", error=True)
                self.log_queue.put(('progress', 50))

            except Exception as e:
                self.log_to_threadsafe(f"Error executing command '{command}': {e}", error=True)

    def install_package(self, client, package_name, ipk_path):
        """Install a package and verify installation"""
        ipk_filename = os.path.basename(ipk_path)
        package_path = "/tmp/" + ipk_filename
        
        # Step 1: Install the package
        install_command = f"opkg install {package_path}"
        self.log_to_threadsafe(f"Executing command: {install_command}")
        self.log_to_threadsafe(f"Installing {package_name}...", info=True)
        output = self.execute_ssh_command(client, install_command)
        
        # Step 2: Check if installation was successful
        if output:
            if "Installing" in str(output) and "Configuring" in str(output):
                self.log_to_threadsafe(f"Installation successful: {package_name} installed and configured.")
            elif f"Package {package_name}" in str(output) and "installed in root is up to date" in str(output):
                # If the package is already up-to-date
                self.log_to_threadsafe(f"{package_name} is already up to date.", info=True)
            elif f"Configuring {package_name}" in str(output):
                # If configuring indicates successful installation
                self.log_to_threadsafe(f"{package_name} is being configured.", info=True)
            elif "Package already installed" in str(output):
                self.log_to_threadsafe(f"{package_name} is already installed.", info=True)
            elif "Cannot install package" in str(output):
                self.log_to_threadsafe(f"Error: Failed to install {package_name}.\n{output}", error=True)
            else:
                self.log_to_threadsafe(f"Unexpected output during installation:\n{output}", error=True)
        
        # Step 3: Verify installation with opkg
        verify_command = f"opkg list-installed | grep {package_name}"
        verify_output = self.execute_ssh_command(client, verify_command)
        if package_name in str(verify_output):
            self.log_to_threadsafe(f"Verification successful: {package_name} is installed.")
        else:
            self.log_to_threadsafe(f"Error: {package_name} is not installed.", error=True)
    def get_device_type(self,client):
                # Check for MIFI device
        mifi_check_cmd = r"cat /etc/config/system | grep -i 'mifi\|mobile\|lte'"
        mifi_output = self.execute_ssh_command(client, mifi_check_cmd)
        if "mifi" in str(mifi_output).lower():
            return "mifi"
        else:
            return "XE300"
    def install_and_enable_gpio(self, client):
        """
        Transfer and enable the GPIO init script on the OpenWrt router.
        
        Args:
            client: The SSH client connected to the router
        
        Returns:
            bool: True if the transfer and enable were successful, False otherwise
        """
        try:
            # 1. Transfer the GPIO init script from assets
            self.log_to_threadsafe("Setting up GPIO init script...", info=True)
            gpio_script_path = resource_path('gpio_setup', 'assets')  # Path to GPIO script in local assets
            remote_init_script_path = "/etc/init.d/gpio_setup"  # Remote path to copy the script to
            
            # Transfer the init script to the router
            self.log_to_threadsafe(f"Transferring GPIO init script to router...", info=True)
            self.scp_transfer(client, gpio_script_path, remote_init_script_path)
            
            # Make the script executable
            self.log_to_threadsafe("Setting up init script permissions...", info=True)
            self.execute_ssh_command(client, f"chmod +x {remote_init_script_path}")
            
            # Enable the init script at boot
            self.log_to_threadsafe("Enabling GPIO init script to run at boot...", info=True)
            self.execute_ssh_command(client, f"{remote_init_script_path} enable")
            
            # Confirm that the script is enabled correctly
            self.log_to_threadsafe("GPIO init script enabled successfully.", info=True)
            
            return True

        except Exception as e:
            self.log_to_threadsafe(f"Error in GPIO setup: {str(e)}", error=True)
            return False
    def update_rc_local(self, client):
        """
        Directly update rc.local with GPIO configuration using a more reliable method.
        """
        try:
            # Detect device type
            device_type = self.get_device_type(client)
            is_mifi = device_type == "mifi"
            gpio_pin = "8" if is_mifi else "15"
            
            self.log_to_threadsafe(f"Updating rc.local for {device_type} device using GPIO {gpio_pin}")
            
            # First check if rc.local exists
            check_cmd = "[ -f /etc/rc.local ] && echo exists || echo notexists"
            rc_exists = self.execute_ssh_command(client, check_cmd).strip()
            
            # Generate the GPIO configuration block
            if is_mifi:
                gpio_config = (
                    "# GPIO configuration for MIFI device\n"
                    "echo 8 > /sys/class/gpio/export\n"
                    "echo out > /sys/class/gpio/gpio8/direction\n"
                    "echo 1 > /sys/class/gpio/gpio8/value\n"
                )
            else:
                gpio_config = (
                    "# GPIO configuration for non-MIFI device\n"
                    "echo 15 > /sys/class/gpio/export\n"
                    "echo out > /sys/class/gpio/gpio15/direction\n"
                    "echo 1 > /sys/class/gpio/gpio15/value\n"
                )
                
            if rc_exists == "exists":
                # Read the current rc.local file
                read_cmd = "cat /etc/rc.local"
                current_content = self.execute_ssh_command(client, read_cmd)
                
                # Check if GPIO config already exists
                if f"echo {gpio_pin} > /sys/class/gpio/export" in current_content:
                    self.log_to_threadsafe("GPIO configuration already exists in rc.local")
                    return True
                    
                # Create a new file with our content
                # First, create a temporary file
                new_content = ""
                exit_found = False
                
                # Process the existing content line by line
                for line in current_content.splitlines():
                    if "exit 0" in line:
                        # Insert GPIO config before exit 0
                        new_content += gpio_config
                        new_content += line + "\n"
                        exit_found = True
                    else:
                        new_content += line + "\n"
                
                # If no exit 0 was found, append GPIO config and exit 0
                if not exit_found:
                    new_content += gpio_config
                    new_content += "exit 0\n"
                    
                # Escape special characters for the shell
                new_content = new_content.replace('"', '\\"').replace('$', '\\$').replace('`', '\\`')
                
                # Write the new content to a temporary file on the router
                temp_file = "/tmp/new_rc.local"
                write_cmd = f'echo -e "{new_content}" > {temp_file}'
                self.execute_ssh_command(client, write_cmd)
                
                # Replace the original file with our new version
                replace_cmd = f"cp {temp_file} /etc/rc.local && chmod +x /etc/rc.local"
                self.execute_ssh_command(client, replace_cmd)
                
            else:
                # Create a new rc.local file from scratch
                new_content = (
                    "#!/bin/sh\n\n"
                    "# Put your custom commands here that should be executed once\n"
                    "# the system init finished.\n\n"
                    f"{gpio_config}\n"
                    "exit 0\n"
                )
                
                # Escape special characters for the shell
                new_content = new_content.replace('"', '\\"').replace('$', '\\$').replace('`', '\\`')
                
                # Write the new file directly
                write_cmd = f'echo -e "{new_content}" > /etc/rc.local && chmod +x /etc/rc.local'
                self.execute_ssh_command(client, write_cmd)
            
            # Verify the file was updated
            verify_cmd = "cat /etc/rc.local | grep -q 'GPIO configuration' && echo success || echo failed"
            result = self.execute_ssh_command(client, verify_cmd).strip()
            
            if result == "success":
                self.log_to_threadsafe("Successfully updated rc.local with GPIO configuration", info=True)
                return True
            else:
                self.log_to_threadsafe("Failed to verify rc.local update", error=True)
                return False
                
        except Exception as e:
            self.log_to_threadsafe(f"Error updating rc.local: {str(e)}", error=True)
            return False

# Call this function from set_cron_jobs_on_router
    def set_cron_jobs_on_router(self, client):
        """Set cron jobs on the router and update rc.local for GPIO configuration"""
        monitoring_mode = self.gui.monitoring_mode.get()
        
        try:
            # Clear existing crontab
            self.execute_ssh_command(client, "crontab -r")
            self.log_to_threadsafe("Existing crontab cleared.")
            
            # Prepare cron jobs
            cron_content = ""
            
            # Try to detect device type and GPIO pin
            try:
                device_type = self.get_device_type(client)
                is_mifi = device_type == "mifi"
                gpio_pin = "8" if is_mifi else "15"
                self.log_to_threadsafe(f"Detected device type: {device_type}. Using GPIO {gpio_pin}")
            except Exception as gpio_error:
                gpio_pin = "15"
                is_mifi = False
                self.log_to_threadsafe(f"GPIO detection failed. Using default GPIO {gpio_pin}: {str(gpio_error)}")
            
            # Add vibration monitor cron schedule based on monitoring mode
             
            if monitoring_mode == "Every 5 minutes":
                cron_content += "0,5,10,15,20,25,30,35,40,45,50,55 * * * * vibration-monitor >> /tmp/cron_vibration.log 2>&1\n"
            elif monitoring_mode == "Every 10 minutes":
                cron_content += "0,10,20,30,40,50 * * * * vibration-monitor >> /tmp/cron_vibration.log 2>&1\n"
            else:  # Default to 15-minute interval
                cron_content += "2,17,32,47 * * * * vibration-monitor >> /tmp/cron_vibration.log 2>&1\n"
            
            # Add log cleanup cron job
            cron_content += "1 0 1 * * rm /tmp/cron_vibration.log\n"
            
            # Add GPIO control commands based on monitoring mode
            if monitoring_mode == "Every 5 minutes":
                cron_content += f"4,9,14,19,24,29,34,39,44,49,54,59 * * * * echo 1 > /sys/class/gpio/gpio{gpio_pin}/value\n"
                cron_content += f"2,7,12,17,22,27,32,37,42,47,52,57 * * * * echo 0 > /sys/class/gpio/gpio{gpio_pin}/value\n"
            elif monitoring_mode == "Every 10 minutes":
                cron_content += f"9,19,29,39,49,59 * * * * echo 1 > /sys/class/gpio/gpio{gpio_pin}/value\n"
                cron_content += f"2,12,22,32,42,52 * * * * echo 0 > /sys/class/gpio/gpio{gpio_pin}/value\n"
            else:  # 15 minute option
                cron_content += f"1,16,31,46 * * * * echo 1 > /sys/class/gpio/gpio{gpio_pin}/value\n"
                cron_content += f"4,18,35,50 * * * * echo 0 > /sys/class/gpio/gpio{gpio_pin}/value\n"
            
            # Escape single quotes in the content
            cron_content = cron_content.replace("'", "'\\''")
            
            # Set the new crontab directly using echo and crontab
            set_crontab_cmd = f"echo '{cron_content}' | crontab -"
            
            # Execute the command and log output
            stdin, stdout, stderr = client.exec_command(set_crontab_cmd)
            output = stdout.read().decode('utf-8').strip()
            error = stderr.read().decode('utf-8').strip()
            
            if output:
                self.log_to_threadsafe(f"Crontab output: {output}")
            if error:
                self.log_to_threadsafe(f"Crontab error: {error}", error=True)
            
            # Verify the crontab
            stdin, stdout, stderr = client.exec_command("crontab -l")
            verified_crontab = stdout.read().decode('utf-8').strip()
            self.log_to_threadsafe("Verified Crontab:")
            self.log_to_threadsafe(verified_crontab)
            
            # Update rc.local using our more direct method
            self.update_rc_local(client)
            
            # Export GPIO immediately to ensure it's available for this session
            gpio_export_cmd = f"[ ! -d /sys/class/gpio/gpio{gpio_pin} ] && echo {gpio_pin} > /sys/class/gpio/export || true; echo out > /sys/class/gpio/gpio{gpio_pin}/direction; echo 1 > /sys/class/gpio/gpio{gpio_pin}/value"
            self.execute_ssh_command(client, gpio_export_cmd)
            self.log_to_threadsafe(f"GPIO {gpio_pin} exported and configured immediately")
            
            # Restart cron service
            try:
                self.execute_ssh_command(client, "service cron restart")
                self.log_to_threadsafe("Cron service restarted")
            except Exception:
                self.log_to_threadsafe("Failed to restart cron service. Crontab was updated but service not restarted.", error=True)
            
            self.log_to_threadsafe(f"Monitoring interval set to: {monitoring_mode}")
            return True
        except Exception as e:
            self.log_to_threadsafe(f"Error setting cron jobs: {str(e)}", error=True)
            return False
            
    def install_and_run_sensor_setup(self, client):
        """
        Install and run the sensor-setup utility on the OpenWrt router.
        
        Args:
            client: The SSH client connected to the router
            ipk_path: Path to the sensor-setup IPK file on the local machine
        
        Returns:
            bool: True if installation and execution were successful, False otherwise
        """
        try:
            # Get the IPK filename without path
            ipk_path = resource_path('sensor-setup_1.0.1-2_mips_24kc.ipk', 'assets')
            ipk_filename = os.path.basename(ipk_path)
            remote_ipk_path = f"/tmp/{ipk_filename}"
            
            # 1. Transfer the IPK file to the router
            self.log_to_threadsafe(f"Transferring sensor-setup IPK to router...", info=True)
            self.scp_transfer(client, ipk_path, remote_ipk_path)
            
            # 2. Install the IPK package
            self.log_to_threadsafe("Installing sensor-setup package...", info=True)
            install_command = f"opkg install {remote_ipk_path}"
            output = self.execute_ssh_command(client, install_command)
            
            # Check if installation was successful
            if "Configuring sensor-setup" not in str(output) and "Package sensor-setup" not in str(output):
                self.log_to_threadsafe("Failed to install sensor-setup package.", error=True)
                self.log_to_threadsafe(str(output), error=True)
                return False
            
            self.log_to_threadsafe("sensor-setup package installed successfully.", info=True)
            # 2.5 Check device type and turn on port status
            if self.get_device_type(client) == "mifi":
                self.execute_ssh_command(client, "echo 1 > /sys/class/gpio/gpio8/value")
            else:
                self.execute_ssh_command(client, "echo 1 > /sys/class/gpio/gpio15/value")
            # 3. Run the sensor-setup utility
            time.sleep(1)
            self.log_to_threadsafe("Running sensor-setup utility...", info=True)
            run_command = "sensor-setup"
                        # Use exec_command directly to capture real-time output
            stdin, stdout, stderr = client.exec_command(run_command)
            
            # Monitor and log output in real-time
            while not stdout.channel.exit_status_ready():
                if stdout.channel.recv_ready():
                    output_line = stdout.readline()
                    if output_line:
                        self.log_to_threadsafe(output_line.strip())
                time.sleep(0.1)  # Small delay to prevent high CPU usage
            
            # Fully suppress all output
            stdin, stdout, stderr = client.exec_command(run_command + " > /dev/null 2>&1")

            # Wait for completion silently 
            stdout.channel.recv_exit_status()

            # Consume any buffers without displaying
            stdout.read()
            stderr.read()
            # Get exit status
            exit_status = stdout.channel.recv_exit_status()
            if exit_status != 0:
                self.log_to_threadsafe(f"sensor-setup exited with status {exit_status}", error=True)
                return False
            # turn off port GPIO 
            if self.get_device_type(client) == "mifi":
                self.execute_ssh_command(client, "echo 0 > /sys/class/gpio/gpio8/value")
            else:
                self.execute_ssh_command(client, "echo 0 > /sys/class/gpio/gpio15/value")            
            
            self.log_to_threadsafe("sensor-setup completed successfully!", info=True)
            
            # 4. Clean up the IPK file
            cleanup_command = f"rm {remote_ipk_path}"
            self.execute_ssh_command(client, cleanup_command)
            
            return True
            
        except Exception as e:
            self.log_to_threadsafe(f"Error in sensor-setup installation and execution: {str(e)}", error=True)
            return False

# Main application entry point
if __name__ == "__main__":
    app = RouterConfigGUI()
    app.root.mainloop()