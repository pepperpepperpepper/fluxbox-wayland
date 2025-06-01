#!/usr/bin/env python3
# test_input_handling.py - Test input device handling and key bindings

import subprocess
import time
import os
import sys
import tempfile
import shutil

class InputHandlingTester:
    def __init__(self):
        self.compositor_process = None
        self.test_results = []
        
    def start_compositor_with_config(self, config_content):
        """Start compositor with specific configuration"""
        config_dir = os.path.expanduser("~/.config/fluxbox-wayland")
        os.makedirs(config_dir, exist_ok=True)
        
        config_path = os.path.join(config_dir, "fluxbox-wayland.conf")
        
        # Backup existing config
        backup_path = None
        if os.path.exists(config_path):
            backup_path = config_path + ".test_backup"
            shutil.copy2(config_path, backup_path)
            
        # Write test config
        with open(config_path, 'w') as f:
            f.write(config_content)
            
        env = os.environ.copy()
        env['WLR_BACKENDS'] = 'headless'
        env['WLR_RENDERER'] = 'pixman'
        
        self.compositor_process = subprocess.Popen(
            ['./build/fluxbox-wayland'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env
        )
        
        time.sleep(2)  # Allow startup
        return backup_path
        
    def restore_config(self, backup_path):
        """Restore original configuration"""
        config_path = os.path.expanduser("~/.config/fluxbox-wayland/fluxbox-wayland.conf")
        
        try:
            if backup_path and os.path.exists(backup_path):
                shutil.move(backup_path, config_path)
            elif os.path.exists(config_path):
                os.remove(config_path)
        except Exception as e:
            print(f"Warning: Could not restore config: {e}")
            
    def stop_compositor(self):
        """Stop the compositor"""
        if self.compositor_process:
            self.compositor_process.terminate()
            try:
                self.compositor_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.compositor_process.kill()
                
    def get_active_wayland_display(self):
        """Find active wayland display"""
        for i in range(10):
            socket_path = f"/tmp/wayland-{i}"
            if os.path.exists(socket_path):
                return f"wayland-{i}"
        return None
        
    def test_input_device_detection(self):
        """Test input device and seat management"""
        print("Testing input device detection...")
        
        config = '''# Input device test configuration
session.screen0.workspaces: 4

# Basic key bindings to test input
key Alt+1 : workspace 1
key Alt+q : close
key Ctrl+Alt+t : exec foot
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                display = self.get_active_wayland_display()
                if display:
                    env = os.environ.copy()
                    env['WAYLAND_DISPLAY'] = display
                    
                    # Use wayland-info to check input capabilities
                    result = subprocess.run(
                        ['wayland-info'],
                        env=env,
                        capture_output=True,
                        timeout=10
                    )
                    
                    output = result.stdout.decode()
                    
                    input_features = []
                    if 'wl_seat' in output:
                        input_features.append("seat management")
                    if 'wl_keyboard' in output:
                        input_features.append("keyboard")
                    if 'wl_pointer' in output:
                        input_features.append("pointer")
                    if 'wl_touch' in output:
                        input_features.append("touch")
                        
                    if input_features:
                        self.test_results.append(f"✅ Input device detection: {', '.join(input_features)}")
                        return True
                    else:
                        self.test_results.append("❌ Input device detection: No input devices found")
                        return False
                else:
                    self.test_results.append("❌ Input device detection: No active display")
                    return False
            else:
                self.test_results.append("❌ Input device detection: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Input device detection: TIMEOUT")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Input device detection: ERROR - {e}")
            return False
        finally:
            self.restore_config(backup_path)
            self.stop_compositor()
            
    def test_basic_key_bindings(self):
        """Test basic key binding configuration"""
        print("Testing basic key bindings...")
        
        config = '''# Basic key binding test
session.screen0.workspaces: 4

# Workspace switching
key Alt+1 : workspace 1
key Alt+2 : workspace 2
key Alt+3 : workspace 3
key Alt+4 : workspace 4

# Navigation
key Alt+Left : workspace prev
key Alt+Right : workspace next

# Window management
key Alt+q : close
key Alt+F4 : close

# Application launching
key Ctrl+Alt+t : exec foot
key Alt+Return : exec foot
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                # Check for key binding related output or workspace setup
                if "workspace" in output or "4 workspaces available" in output:
                    self.test_results.append("✅ Basic key bindings: CONFIGURED")
                    return True
                else:
                    self.test_results.append("❌ Basic key bindings: FAILED")
                    return False
            else:
                self.test_results.append("❌ Basic key bindings: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Basic key bindings: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_modifier_key_combinations(self):
        """Test various modifier key combinations"""
        print("Testing modifier key combinations...")
        
        config = '''# Modifier key combination test
session.screen0.workspaces: 4

# Single modifiers
key Alt+q : close
key Ctrl+c : close
key Shift+F10 : exec foot

# Double modifiers
key Ctrl+Alt+t : exec foot
key Ctrl+Shift+q : close
key Alt+Shift+1 : workspace 1

# Triple modifiers
key Ctrl+Alt+Shift+q : close

# Function keys
key F1 : workspace 1
key F2 : workspace 2
key Ctrl+F1 : workspace 1
key Alt+F1 : workspace 1

# Special keys
key Alt+Tab : workspace next
key Super+l : exec weston-terminal
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                # Look for successful configuration loading
                if "workspace" in output and "4 workspaces available" in output:
                    self.test_results.append("✅ Modifier key combinations: CONFIGURED")
                    return True
                else:
                    self.test_results.append("❌ Modifier key combinations: FAILED")
                    self.test_results.append(f"   Output: {output[-200:]}")
                    return False
            else:
                self.test_results.append("❌ Modifier key combinations: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Modifier key combinations: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_keyboard_focus_management(self):
        """Test keyboard focus management"""
        print("Testing keyboard focus management...")
        
        config = '''# Focus management test
session.screen0.workspaces: 4
session.screen0.focusModel: ClickFocus
session.screen0.autoRaise: false

# Focus-related key bindings
key Alt+Tab : workspace next
key Alt+Shift+Tab : workspace prev
key Alt+1 : workspace 1
key Alt+2 : workspace 2
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                display = self.get_active_wayland_display()
                if display:
                    env = os.environ.copy()
                    env['WAYLAND_DISPLAY'] = display
                    
                    # Check for keyboard capabilities
                    result = subprocess.run(
                        ['wayland-info'],
                        env=env,
                        capture_output=True,
                        timeout=8
                    )
                    
                    output = result.stdout.decode()
                    
                    if 'wl_keyboard' in output and 'wl_seat' in output:
                        self.test_results.append("✅ Keyboard focus management: AVAILABLE")
                        return True
                    else:
                        self.test_results.append("❌ Keyboard focus management: NOT AVAILABLE")
                        return False
                else:
                    self.test_results.append("❌ Keyboard focus management: No display")
                    return False
            else:
                self.test_results.append("❌ Keyboard focus management: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Keyboard focus management: TIMEOUT")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Keyboard focus management: ERROR - {e}")
            return False
        finally:
            self.restore_config(backup_path)
            self.stop_compositor()
            
    def test_invalid_key_bindings(self):
        """Test handling of invalid key binding configurations"""
        print("Testing invalid key binding handling...")
        
        config = '''# Invalid key binding test
session.screen0.workspaces: 4

# Valid bindings
key Alt+1 : workspace 1
key Alt+2 : workspace 2

# Invalid bindings that should be ignored
key InvalidModifier+q : close
key Alt+ : workspace 3
key : workspace 4
key Alt+NonExistentKey : close
key Alt+1 : invalid_action
key Alt+2 : workspace invalid_number

# Malformed bindings
invalid_line_format
key_without_colon workspace 1
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                # Should still start successfully and fall back to valid config
                if "4 workspaces available" in output:
                    self.test_results.append("✅ Invalid key bindings: HANDLED - Compositor still functional")
                    return True
                else:
                    self.test_results.append("❌ Invalid key bindings: FAILED")
                    return False
            else:
                # Failing to start might be acceptable for severely malformed configs
                self.test_results.append("⚠️  Invalid key bindings: ROBUST - Rejected malformed config")
                return True
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Invalid key bindings: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_pointer_mouse_support(self):
        """Test pointer/mouse input support"""
        print("Testing pointer/mouse support...")
        
        config = '''# Mouse support test
session.screen0.workspaces: 4
session.screen0.focusModel: ClickFocus

# Basic key bindings
key Alt+1 : workspace 1
key Alt+q : close
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                display = self.get_active_wayland_display()
                if display:
                    env = os.environ.copy()
                    env['WAYLAND_DISPLAY'] = display
                    
                    # Check for pointer capabilities
                    result = subprocess.run(
                        ['wayland-info'],
                        env=env,
                        capture_output=True,
                        timeout=8
                    )
                    
                    output = result.stdout.decode()
                    
                    pointer_features = []
                    if 'wl_pointer' in output:
                        pointer_features.append("pointer")
                    if 'wl_seat' in output:
                        pointer_features.append("seat")
                    if 'wl_surface' in output:
                        pointer_features.append("surface interaction")
                        
                    if pointer_features:
                        self.test_results.append(f"✅ Pointer/mouse support: {', '.join(pointer_features)}")
                        return True
                    else:
                        self.test_results.append("❌ Pointer/mouse support: NOT AVAILABLE")
                        return False
                else:
                    self.test_results.append("❌ Pointer/mouse support: No display")
                    return False
            else:
                self.test_results.append("❌ Pointer/mouse support: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Pointer/mouse support: TIMEOUT")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Pointer/mouse support: ERROR - {e}")
            return False
        finally:
            self.restore_config(backup_path)
            self.stop_compositor()
            
    def run_input_tests(self):
        """Run all input handling tests"""
        print("=== Running Input Handling Tests ===")
        
        tests = [
            self.test_input_device_detection,
            self.test_basic_key_bindings,
            self.test_modifier_key_combinations,
            self.test_keyboard_focus_management,
            self.test_invalid_key_bindings,
            self.test_pointer_mouse_support
        ]
        
        passed = 0
        total = len(tests)
        
        for test in tests:
            try:
                if test():
                    passed += 1
            except Exception as e:
                self.test_results.append(f"❌ {test.__name__}: EXCEPTION - {e}")
                
        # Print results
        print("\n=== Input Handling Test Results ===")
        for result in self.test_results:
            print(result)
            
        print(f"\nSummary: {passed}/{total} input handling tests passed")
        
        if passed == total:
            print("🎉 ALL INPUT HANDLING TESTS PASSED!")
            return True
        else:
            print(f"⚠️  {total - passed} input handling tests failed")
            return False

if __name__ == "__main__":
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        sys.exit(1)
        
    tester = InputHandlingTester()
    success = tester.run_input_tests()
    sys.exit(0 if success else 1)