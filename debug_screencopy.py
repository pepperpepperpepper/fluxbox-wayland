#!/usr/bin/env python3

import subprocess
import os
import sys
import time
import signal
from pathlib import Path

def test_screencopy_debug():
    """Test screencopy functionality and debug hanging issues"""
    print("=== Fluxbox Wayland Screencopy Debug ===")
    
    # Set up environment
    build_dir = Path.cwd() / "build"
    compositor_binary = build_dir / "fluxbox-wayland"
    
    if not compositor_binary.exists():
        print(f"ERROR: Compositor binary not found at {compositor_binary}")
        return False
    
    # Start Xvfb for nested X11 backend
    print("Starting Xvfb...")
    xvfb_proc = subprocess.Popen([
        "Xvfb", ":99", "-screen", "0", "1024x768x24", "-ac", "+extension", "GLX"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    time.sleep(2)
    
    # Set up environment for nested compositor
    env = os.environ.copy()
    env["DISPLAY"] = ":99"
    env["WLR_BACKENDS"] = "x11"
    env["WAYLAND_DISPLAY"] = ""  # Clear any existing setting
    
    # Start compositor
    print("Starting Fluxbox Wayland compositor...")
    try:
        compositor_proc = subprocess.Popen([
            str(compositor_binary)
        ], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        
        # Wait for compositor to start
        time.sleep(3)
        
        # Check if compositor is running
        if compositor_proc.poll() is not None:
            stdout, stderr = compositor_proc.communicate()
            print("Compositor failed to start:")
            print("STDOUT:", stdout.decode())
            print("STDERR:", stderr.decode())
            return False
        
        # Get the WAYLAND_DISPLAY value from compositor output
        wayland_display = None
        
        # Try to read initial output from compositor
        import select
        ready, _, _ = select.select([compositor_proc.stdout], [], [], 5)
        if ready:
            output = compositor_proc.stdout.read(1024).decode()
            print("Compositor output:", output)
            
            # Extract WAYLAND_DISPLAY from output
            for line in output.split('\n'):
                if 'WAYLAND_DISPLAY=' in line:
                    wayland_display = line.split('WAYLAND_DISPLAY=')[1].strip()
                    break
                elif 'running on' in line:
                    # Extract socket name from "running on wayland-X"
                    parts = line.split('running on ')
                    if len(parts) > 1:
                        wayland_display = parts[1].strip()
                        break
        
        if not wayland_display:
            # Try common socket names
            runtime_dir = f"/run/user/{os.getuid()}"
            for i in range(10):
                socket_path = f"{runtime_dir}/wayland-{i}"
                if os.path.exists(socket_path):
                    wayland_display = f"wayland-{i}"
                    break
        
        if not wayland_display:
            print("Could not determine WAYLAND_DISPLAY")
            return False
        
        print(f"Using WAYLAND_DISPLAY={wayland_display}")
        
        # Set environment for clients
        client_env = env.copy()
        client_env["WAYLAND_DISPLAY"] = wayland_display
        
        # Test protocol availability first
        print("\n=== Testing Protocol Availability ===")
        try:
            result = subprocess.run([
                "wayland-info"
            ], env=client_env, capture_output=True, text=True, timeout=10)
            
            if result.returncode == 0:
                print("wayland-info output:")
                output = result.stdout
                
                # Check for screencopy protocol
                if "zwlr_screencopy_manager_v1" in output:
                    print("✓ zwlr_screencopy_manager_v1 protocol found")
                else:
                    print("✗ zwlr_screencopy_manager_v1 protocol NOT found")
                
                # Check for other relevant protocols
                protocols = [
                    "zwlr_export_dmabuf_manager_v1",
                    "zxdg_output_manager_v1",
                    "wl_output",
                    "wl_shm"
                ]
                
                for protocol in protocols:
                    if protocol in output:
                        print(f"✓ {protocol} protocol found")
                    else:
                        print(f"✗ {protocol} protocol NOT found")
                        
                # Save full output for analysis
                with open("wayland-info-output.txt", "w") as f:
                    f.write(output)
                print("Full wayland-info output saved to wayland-info-output.txt")
                
            else:
                print(f"wayland-info failed: {result.stderr}")
                
        except subprocess.TimeoutExpired:
            print("wayland-info timed out")
        except Exception as e:
            print(f"Error running wayland-info: {e}")
        
        # Test basic client connectivity
        print("\n=== Testing Basic Client Connectivity ===")
        try:
            # Try running a simple wayland client that exits quickly
            result = subprocess.run([
                "wayland-info", "--version"
            ], env=client_env, capture_output=True, text=True, timeout=5)
            
            if result.returncode == 0:
                print("✓ Basic client connectivity works")
            else:
                print("✗ Basic client connectivity failed")
                
        except Exception as e:
            print(f"Client connectivity test failed: {e}")
        
        # Now test grim with debugging
        print("\n=== Testing Grim Screenshot ===")
        print("Running grim with timeout...")
        
        try:
            # Run grim with a timeout to see if it hangs
            grim_proc = subprocess.Popen([
                "grim", "-t", "png", "debug_screenshot.png"
            ], env=client_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            
            # Wait for grim with timeout
            try:
                stdout, stderr = grim_proc.communicate(timeout=15)
                
                if grim_proc.returncode == 0:
                    print("✓ Grim succeeded!")
                    if os.path.exists("debug_screenshot.png"):
                        stat = os.stat("debug_screenshot.png")
                        print(f"Screenshot file size: {stat.st_size} bytes")
                    else:
                        print("✗ Screenshot file not created")
                else:
                    print(f"✗ Grim failed with return code {grim_proc.returncode}")
                    print("STDOUT:", stdout.decode())
                    print("STDERR:", stderr.decode())
                    
            except subprocess.TimeoutExpired:
                print("✗ Grim hung - terminating...")
                grim_proc.terminate()
                try:
                    grim_proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    grim_proc.kill()
                    grim_proc.wait()
                
                print("Grim was hanging - this indicates a screencopy protocol issue")
                
        except Exception as e:
            print(f"Error testing grim: {e}")
        
        # Test alternative screenshot tools
        print("\n=== Testing Alternative Screenshot Tools ===")
        
        # Test wayshot if available
        try:
            result = subprocess.run([
                "wayshot", "--version"
            ], capture_output=True, text=True, timeout=5)
            
            if result.returncode == 0:
                print("Wayshot available, testing...")
                try:
                    wayshot_proc = subprocess.Popen([
                        "wayshot", "-f", "debug_wayshot.png"
                    ], env=client_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    
                    stdout, stderr = wayshot_proc.communicate(timeout=15)
                    
                    if wayshot_proc.returncode == 0:
                        print("✓ Wayshot succeeded!")
                    else:
                        print(f"✗ Wayshot failed: {stderr.decode()}")
                        
                except subprocess.TimeoutExpired:
                    print("✗ Wayshot also hung")
                    wayshot_proc.terminate()
                    wayshot_proc.wait()
                    
            else:
                print("Wayshot not available")
                
        except Exception:
            print("Wayshot not available")
    
    except Exception as e:
        print(f"Error during testing: {e}")
        return False
    
    finally:
        # Cleanup
        print("\nCleaning up...")
        if 'compositor_proc' in locals():
            compositor_proc.terminate()
            try:
                compositor_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                compositor_proc.kill()
                compositor_proc.wait()
        
        if 'xvfb_proc' in locals():
            xvfb_proc.terminate()
            try:
                xvfb_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                xvfb_proc.kill()
                xvfb_proc.wait()
    
    return True

if __name__ == "__main__":
    test_screencopy_debug()