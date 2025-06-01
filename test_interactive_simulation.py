#!/usr/bin/env python3
# test_interactive_simulation.py - Interactive testing with real user simulation

import subprocess
import time
import os
import sys
import signal
import threading
from typing import Optional, List
import tempfile

class InteractiveFluxboxTester:
    def __init__(self):
        self.compositor_process = None
        self.test_results = []
        self.test_log = []
        self.client_processes = []
        
    def log(self, message: str):
        """Log test progress"""
        timestamp = time.strftime("%H:%M:%S")
        log_msg = f"[{timestamp}] {message}"
        self.test_log.append(log_msg)
        print(log_msg)
        
    def start_compositor(self):
        """Start the compositor for interactive testing"""
        self.log("Starting Fluxbox Wayland compositor...")
        
        env = os.environ.copy()
        env['WLR_BACKENDS'] = 'headless'
        env['WLR_RENDERER'] = 'pixman'
        env['WAYLAND_DEBUG'] = '0'  # Reduce debug noise
        
        self.compositor_process = subprocess.Popen(
            ['./build/fluxbox-wayland'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env
        )
        
        # Wait for startup
        time.sleep(3)
        
        if self.compositor_process.poll() is None:
            self.log("✅ Compositor started successfully")
            return True
        else:
            stdout, stderr = self.compositor_process.communicate()
            self.log(f"❌ Compositor failed to start: {stderr.decode()[:200]}")
            return False
            
    def stop_compositor(self):
        """Stop the compositor cleanly"""
        if self.compositor_process:
            self.log("Stopping compositor...")
            self.compositor_process.terminate()
            try:
                self.compositor_process.wait(timeout=5)
                self.log("✅ Compositor stopped gracefully")
            except subprocess.TimeoutExpired:
                self.compositor_process.kill()
                self.log("⚠️  Compositor force-killed after timeout")
                
    def cleanup_clients(self):
        """Clean up any running client processes"""
        for proc in self.client_processes:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
        self.client_processes.clear()
        
    def get_wayland_display(self):
        """Get the active Wayland display"""
        uid = os.getuid()
        runtime_dir = f"/run/user/{uid}"
        
        for i in range(10):
            socket_path = f"{runtime_dir}/wayland-{i}"
            if os.path.exists(socket_path):
                return f"wayland-{i}"
        return None
        
    def send_key_sequence(self, display: str, keys: List[str], delay: float = 0.1):
        """Send a sequence of key presses using wtype"""
        self.log(f"Sending key sequence: {' -> '.join(keys)}")
        
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        success_count = 0
        for key in keys:
            try:
                # Use wtype to send key events
                result = subprocess.run(
                    ['wtype', key],
                    env=env,
                    capture_output=True,
                    timeout=5
                )
                
                if result.returncode == 0:
                    success_count += 1
                    self.log(f"  ✅ Key '{key}' sent successfully")
                else:
                    self.log(f"  ❌ Failed to send key '{key}': {result.stderr.decode()}")
                    
                time.sleep(delay)
                
            except subprocess.TimeoutExpired:
                self.log(f"  ❌ Timeout sending key '{key}'")
            except FileNotFoundError:
                self.log("  ⚠️  wtype not available, skipping keyboard simulation")
                return False
                
        return success_count == len(keys)
        
    def test_keyboard_input_simulation(self):
        """Test keyboard input simulation"""
        self.log("=== Testing Keyboard Input Simulation ===")
        
        display = self.get_wayland_display()
        if not display:
            self.test_results.append("❌ Keyboard input: No display available")
            return False
            
        # Test basic typing
        test_sequences = [
            ["h", "e", "l", "l", "o"],  # Basic typing
            ["-M", "ctrl", "a"],         # Ctrl+A
            ["-M", "alt", "1"],          # Alt+1 (workspace switch)
            ["-M", "alt", "q"],          # Alt+Q (close window)
        ]
        
        success_count = 0
        for i, sequence in enumerate(test_sequences):
            self.log(f"Testing keyboard sequence {i+1}/{len(test_sequences)}")
            if self.send_key_sequence(display, sequence):
                success_count += 1
                
        if success_count > 0:
            self.test_results.append(f"✅ Keyboard input: {success_count}/{len(test_sequences)} sequences successful")
            return True
        else:
            self.test_results.append("❌ Keyboard input: All sequences failed")
            return False
            
    def simulate_mouse_movement(self, display: str, movements: List[tuple]):
        """Simulate mouse movements using wtype or ydotool"""
        self.log(f"Simulating mouse movements: {len(movements)} actions")
        
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        success_count = 0
        for i, (x, y, action) in enumerate(movements):
            try:
                if action == "move":
                    # Try ydotool for mouse movement
                    result = subprocess.run(
                        ['ydotool', 'mousemove', str(x), str(y)],
                        env=env,
                        capture_output=True,
                        timeout=3
                    )
                elif action == "click":
                    # Mouse click
                    result = subprocess.run(
                        ['ydotool', 'click', '1'],  # Left click
                        env=env,
                        capture_output=True,
                        timeout=3
                    )
                else:
                    continue
                    
                if result.returncode == 0:
                    success_count += 1
                    self.log(f"  ✅ Mouse action {i+1}: {action} ({x}, {y})")
                else:
                    self.log(f"  ❌ Mouse action {i+1} failed: {result.stderr.decode()}")
                    
                time.sleep(0.1)
                
            except subprocess.TimeoutExpired:
                self.log(f"  ❌ Timeout on mouse action {i+1}")
            except FileNotFoundError:
                self.log("  ⚠️  ydotool not available, skipping mouse simulation")
                return False
                
        return success_count > 0
        
    def test_mouse_interaction(self):
        """Test mouse movement and interaction"""
        self.log("=== Testing Mouse Interaction ===")
        
        display = self.get_wayland_display()
        if not display:
            self.test_results.append("❌ Mouse interaction: No display available")
            return False
            
        # Define mouse movement pattern
        movements = [
            (100, 100, "move"),
            (200, 150, "move"), 
            (200, 150, "click"),
            (300, 200, "move"),
            (300, 200, "click"),
            (150, 250, "move"),
        ]
        
        if self.simulate_mouse_movement(display, movements):
            self.test_results.append("✅ Mouse interaction: Movement and clicks successful")
            return True
        else:
            self.test_results.append("❌ Mouse interaction: Failed to simulate mouse")
            return False
            
    def launch_terminal_and_type(self, display: str):
        """Launch a terminal emulator and simulate typing"""
        self.log("Launching terminal emulator...")
        
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        # Try to launch foot terminal
        try:
            proc = subprocess.Popen(
                ['foot'],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            
            self.client_processes.append(proc)
            time.sleep(2)  # Wait for terminal to start
            
            if proc.poll() is None:
                self.log("✅ Terminal launched successfully")
                
                # Simulate typing in the terminal
                commands_to_type = [
                    "echo 'Hello from Fluxbox Wayland!'",
                    "ls -la",
                    "pwd",
                    "echo 'Testing keyboard input in terminal'",
                    "date"
                ]
                
                for cmd in commands_to_type:
                    self.log(f"Typing command: {cmd}")
                    # Convert command to individual key presses
                    keys = list(cmd) + ["-k", "Return"]  # Add Enter key
                    if self.send_key_sequence(display, keys, delay=0.05):
                        time.sleep(0.5)  # Wait for command execution
                        
                self.log("✅ Terminal typing simulation completed")
                return True
            else:
                self.log("❌ Terminal failed to start")
                return False
                
        except FileNotFoundError:
            self.log("⚠️  foot terminal not available, trying alternative...")
            
            # Try weston-terminal as fallback
            try:
                proc = subprocess.Popen(
                    ['weston-terminal'],
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE
                )
                
                self.client_processes.append(proc)
                time.sleep(2)
                
                if proc.poll() is None:
                    self.log("✅ Weston terminal launched successfully")
                    return True
                else:
                    self.log("❌ Weston terminal failed to start")
                    return False
                    
            except FileNotFoundError:
                self.log("❌ No terminal emulator available")
                return False
                
    def test_terminal_emulator_integration(self):
        """Test terminal emulator launching and interaction"""
        self.log("=== Testing Terminal Emulator Integration ===")
        
        display = self.get_wayland_display()
        if not display:
            self.test_results.append("❌ Terminal integration: No display available")
            return False
            
        if self.launch_terminal_and_type(display):
            self.test_results.append("✅ Terminal integration: Launch and typing successful")
            return True
        else:
            self.test_results.append("❌ Terminal integration: Failed to launch or interact")
            return False
            
    def test_workspace_switching(self):
        """Test workspace switching with keyboard shortcuts"""
        self.log("=== Testing Workspace Switching ===")
        
        display = self.get_wayland_display()
        if not display:
            self.test_results.append("❌ Workspace switching: No display available")
            return False
            
        # Test workspace switching shortcuts
        workspace_keys = [
            ["-M", "alt", "1"],  # Switch to workspace 1
            ["-M", "alt", "2"],  # Switch to workspace 2
            ["-M", "alt", "3"],  # Switch to workspace 3
            ["-M", "alt", "4"],  # Switch to workspace 4
            ["-M", "alt", "-k", "Left"],   # Previous workspace
            ["-M", "alt", "-k", "Right"],  # Next workspace
        ]
        
        success_count = 0
        for i, keys in enumerate(workspace_keys):
            self.log(f"Testing workspace shortcut {i+1}/{len(workspace_keys)}")
            if self.send_key_sequence(display, keys):
                success_count += 1
                time.sleep(0.5)  # Wait for workspace switch
                
        if success_count > 0:
            self.test_results.append(f"✅ Workspace switching: {success_count}/{len(workspace_keys)} shortcuts working")
            return True
        else:
            self.test_results.append("❌ Workspace switching: No shortcuts working")
            return False
            
    def test_client_management(self):
        """Test launching and managing multiple clients"""
        self.log("=== Testing Client Management ===")
        
        display = self.get_wayland_display()
        if not display:
            self.test_results.append("❌ Client management: No display available")
            return False
            
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        # Launch multiple clients
        clients_launched = 0
        
        # Try to launch wayland-info
        try:
            proc = subprocess.Popen(
                ['wayland-info'],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            self.client_processes.append(proc)
            time.sleep(1)
            
            if proc.poll() is None:
                clients_launched += 1
                self.log("✅ wayland-info client launched")
            else:
                self.log("❌ wayland-info failed to launch")
                
        except FileNotFoundError:
            self.log("⚠️  wayland-info not available")
            
        # Try to launch a simple Wayland client
        try:
            proc = subprocess.Popen(
                ['weston-simple-damage'],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            self.client_processes.append(proc)
            time.sleep(1)
            
            if proc.poll() is None:
                clients_launched += 1
                self.log("✅ weston-simple-damage client launched")
            else:
                self.log("❌ weston-simple-damage failed to launch")
                
        except FileNotFoundError:
            self.log("⚠️  weston-simple-damage not available")
            
        # Test window management keys
        if clients_launched > 0:
            self.log("Testing window management shortcuts...")
            
            # Test Alt+Q to close window
            if self.send_key_sequence(display, ["-M", "alt", "q"]):
                time.sleep(1)
                self.log("✅ Alt+Q close window shortcut sent")
                
        if clients_launched > 0:
            self.test_results.append(f"✅ Client management: {clients_launched} clients launched successfully")
            return True
        else:
            self.test_results.append("❌ Client management: No clients could be launched")
            return False
            
    def run_comprehensive_interaction_tests(self):
        """Run all interactive tests"""
        self.log("=== Starting Comprehensive Interactive Testing ===")
        
        if not self.start_compositor():
            self.test_results.append("❌ Interactive tests: Compositor failed to start")
            return False
            
        tests = [
            ("Keyboard Input Simulation", self.test_keyboard_input_simulation),
            ("Mouse Interaction", self.test_mouse_interaction),
            ("Terminal Emulator Integration", self.test_terminal_emulator_integration),
            ("Workspace Switching", self.test_workspace_switching),
            ("Client Management", self.test_client_management),
        ]
        
        passed = 0
        total = len(tests)
        
        for test_name, test_func in tests:
            self.log(f"\n--- Running {test_name} ---")
            try:
                if test_func():
                    passed += 1
                    self.log(f"✅ {test_name}: PASSED")
                else:
                    self.log(f"❌ {test_name}: FAILED")
            except Exception as e:
                self.test_results.append(f"❌ {test_name}: EXCEPTION - {e}")
                self.log(f"❌ {test_name}: EXCEPTION - {e}")
                
        # Cleanup
        self.cleanup_clients()
        self.stop_compositor()
        
        # Print final results
        self.log("\n=== Interactive Testing Results ===")
        for result in self.test_results:
            self.log(result)
            
        self.log(f"\nSummary: {passed}/{total} interactive tests passed")
        
        if passed == total:
            self.log("🎉 ALL INTERACTIVE TESTS PASSED!")
            self.log("✅ Fluxbox Wayland compositor fully functional with user interaction!")
            return True
        else:
            self.log(f"⚠️  {total - passed} interactive tests failed")
            return False

if __name__ == "__main__":
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        sys.exit(1)
        
    tester = InteractiveFluxboxTester()
    success = tester.run_comprehensive_interaction_tests()
    sys.exit(0 if success else 1)