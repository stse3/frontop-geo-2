# build_exe.py - Script to build the executable
# =============================================

import os
import sys
import shutil
import PyInstaller.__main__

def build_exe():
    # Get absolute paths to ensure files are found
    current_dir = os.path.abspath(os.path.dirname(__file__))
    
    # Create directories if they don't exist
    assets_dir = os.path.join(current_dir, "assets")
    images_dir = os.path.join(current_dir, "images")
    installer_gui_assets_dir = os.path.join(current_dir, "installer_GUI", "assets")
    
    if not os.path.exists(assets_dir):
        os.makedirs(assets_dir)
        print(f"Created assets directory at: {assets_dir}")
    
    if not os.path.exists(images_dir):
        os.makedirs(images_dir)
        print(f"Created images directory at: {images_dir}")
    
    if not os.path.exists(installer_gui_assets_dir):
        os.makedirs(installer_gui_assets_dir)
        print(f"Created installer_GUI/assets directory at: {installer_gui_assets_dir}")
    
    # Define source and destination paths
    image_source = os.path.join(images_dir, "frontop_logo.jpg")
    image_dest = "images"
    
    env_file_source = os.path.join(assets_dir, ".env")
    env_file_dest = "assets"
    
    credentials_file_source = os.path.join(assets_dir, "credentials.json")
    credentials_file_dest = "assets"
    
    sensor_setup_source = os.path.join(assets_dir, "sensor-setup_1.0.1-2_mips_24kc.ipk")
    sensor_setup_dest =  "assets"
    
    # Check if files exist and create placeholder files if needed
    if not os.path.exists(credentials_file_source):
        print(f"WARNING: credentials.json not found at: {credentials_file_source}")
        print("Creating a placeholder credentials.json file. Please replace with your actual file.")
        with open(credentials_file_source, 'w') as f:
            f.write('{"placeholder": "Replace with your actual credentials"}')
    
    if not os.path.exists(env_file_source):
        print(f"WARNING: .env file not found at: {env_file_source}")
        print("Creating a placeholder .env file. Please replace with your actual file.")
        with open(env_file_source, 'w') as f:
            f.write('# Replace with your actual .env content\nAPI_KEY=your_api_key_here')
    
    if not os.path.exists(image_source):
        print(f"WARNING: Image file not found at: {image_source}")
        print("You may need to add your logo image to the images folder.")
    
    if not os.path.exists(sensor_setup_source):
        print(f"WARNING: Sensor setup package not found at: {sensor_setup_source}")
        print("Please ensure the sensor setup package is in the installer_GUI/assets directory.")
        # Creating a placeholder directory structure to ensure the build doesn't fail
        os.makedirs(os.path.dirname(sensor_setup_source), exist_ok=True)
    
    # Create the platform-specific path separator
    separator = ';' if os.name == 'nt' else ':'
    
    # Clean previous build files
    for dir_to_clean in ['build', 'dist']:
        if os.path.exists(dir_to_clean):
            shutil.rmtree(dir_to_clean)
            print(f"Cleaned {dir_to_clean} directory")
    
    spec_file = 'RouterConfigGUI.spec'
    if os.path.exists(spec_file):
        os.remove(spec_file)
        print(f"Removed {spec_file}")
    
    # Build the command
    pyinstaller_args = [
        'router_config_gui.py',
        '--onefile',
        '--windowed',
        '--name=RouterConfigGUI',
        f'--add-data={image_source}{separator}{image_dest}',
        f'--add-data={env_file_source}{separator}{env_file_dest}',
        f'--add-data={credentials_file_source}{separator}{credentials_file_dest}',
    ]
    
    # Add the sensor setup package if it exists
    if os.path.exists(sensor_setup_source):
        pyinstaller_args.append(f'--add-data={sensor_setup_source}{separator}{sensor_setup_dest}')
    
    print("\nRunning PyInstaller with arguments:")
    for arg in pyinstaller_args:
        print(f"  {arg}")
    
    PyInstaller.__main__.run(pyinstaller_args)
    
    # Verify the built executable
    dist_dir = os.path.join(current_dir, 'dist')
    if os.name == 'nt':
        exe_path = os.path.join(dist_dir, 'RouterConfigGUI.exe')
    else:
        exe_path = os.path.join(dist_dir, 'RouterConfigGUI')
    
    if os.path.exists(exe_path):
        print(f"\nBuild successful: {exe_path}")
    else:
        print("\nBuild may have failed. Check the output above for errors.")

if __name__ == "__main__":
    build_exe()