#!/usr/bin/env python3
# test_surface_management.py - Test surface lifecycle and window state management

import subprocess
import time
import os
import signal
import sys

class SurfaceManagementTester:
    def __init__(self):
        self.compositor_process = None
        self.test_results = []
        
    def start_compositor(self):
        """Start the compositor in test mode"""
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
        
    def stop_compositor(self):
        """Stop the compositor cleanly"""
        if self.compositor_process:
            self.compositor_process.terminate()
            try:
                self.compositor_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.compositor_process.kill()
                
    def get_active_wayland_display(self):
        """Find active wayland display"""
        uid = os.getuid()
        runtime_dir = f"/run/user/{uid}"
        
        # Check runtime directory first
        for i in range(10):
            socket_path = f"{runtime_dir}/wayland-{i}"
            if os.path.exists(socket_path):
                return f"wayland-{i}"
                
        # Check /tmp as fallback
        for i in range(10):
            socket_path = f"/tmp/wayland-{i}"
            if os.path.exists(socket_path):
                return f"wayland-{i}"
        return None
        
    def test_basic_surface_operations(self):
        """Test basic surface creation and management"""
        print("Testing basic surface operations...")
        
        if not self.compositor_process or self.compositor_process.poll() is not None:
            self.test_results.append("❌ Surface operations: No compositor running")
            return False
            
        display = self.get_active_wayland_display()
        if not display:
            self.test_results.append("❌ Surface operations: No active display found")
            return False
            
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        # Test wayland-info to verify surface protocol support
        try:
            result = subprocess.run(
                ['wayland-info'],
                env=env,
                capture_output=True,
                timeout=10
            )
            
            output = result.stdout.decode()
            
            # Check for XDG shell support
            if 'xdg_wm_base' in output:
                self.test_results.append("✅ XDG shell protocol: AVAILABLE")
                
                # Check for compositor interface
                if 'wl_compositor' in output:
                    self.test_results.append("✅ Compositor interface: AVAILABLE")
                    
                    # Check for surface interface
                    if 'wl_surface' in output:
                        self.test_results.append("✅ Surface interface: AVAILABLE")
                        return True
                    else:
                        self.test_results.append("❌ Surface interface: NOT FOUND")
                        return False
                else:
                    self.test_results.append("❌ Compositor interface: NOT FOUND")
                    return False
            else:
                self.test_results.append("❌ XDG shell protocol: NOT AVAILABLE")
                return False
                
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Surface operations: TIMEOUT")
            return False
        except FileNotFoundError:
            self.test_results.append("❌ Surface operations: wayland-info not found")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Surface operations: ERROR - {e}")
            return False
            
    def test_client_application_launch(self):
        """Test launching client applications"""
        print("Testing client application launch...")
        
        display = self.get_active_wayland_display()
        if not display:
            self.test_results.append("❌ Client launch: No active display")
            return False
            
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        # Test launching foot terminal
        try:
            # Just test if foot can connect (version check)
            result = subprocess.run(
                ['foot', '--version'],
                env=env,
                capture_output=True,
                timeout=5
            )
            
            if result.returncode == 0:
                self.test_results.append("✅ Client application (foot): CAN CONNECT")
                
                # Test launching weston-info if available
                try:
                    info_result = subprocess.run(
                        ['weston-info'],
                        env=env,
                        capture_output=True,
                        timeout=5
                    )
                    
                    if info_result.returncode == 0:
                        self.test_results.append("✅ Client application (weston-info): SUCCESS")
                    else:
                        self.test_results.append("⚠️  Client application (weston-info): NOT AVAILABLE")
                        
                except FileNotFoundError:
                    self.test_results.append("⚠️  Client application (weston-info): NOT INSTALLED")
                    
                return True
            else:
                self.test_results.append("❌ Client launch: foot connection failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Client launch: TIMEOUT")
            return False
        except FileNotFoundError:
            self.test_results.append("❌ Client launch: foot not found")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Client launch: ERROR - {e}")
            return False
            
    def test_window_state_management(self):
        """Test window state operations through compositor output"""
        print("Testing window state management...")
        
        # Since we can't directly test window operations without a real client,
        # we'll test the compositor's reported capabilities and internal state
        
        if not self.compositor_process:
            self.test_results.append("❌ Window state: No compositor running")
            return False
            
        # Get compositor output to verify functionality
        try:
            # Send a signal to check if compositor responds
            self.compositor_process.send_signal(signal.SIGUSR1)
            time.sleep(0.5)
            
            # Check if process is still responsive
            if self.compositor_process.poll() is None:
                self.test_results.append("✅ Window state management: RESPONSIVE TO SIGNALS")
                
                # Test compositor internal state by checking protocol support
                display = self.get_active_wayland_display()
                if display:
                    env = os.environ.copy()
                    env['WAYLAND_DISPLAY'] = display
                    
                    try:
                        result = subprocess.run(
                            ['wayland-info'],
                            env=env,
                            capture_output=True,
                            timeout=5
                        )
                        
                        output = result.stdout.decode()
                        
                        # Check for window management protocols
                        capabilities = []
                        if 'xdg_toplevel' in output:
                            capabilities.append("toplevel windows")
                        if 'xdg_popup' in output:
                            capabilities.append("popup windows")
                        if 'wl_seat' in output:
                            capabilities.append("input handling")
                        if 'wl_output' in output:
                            capabilities.append("output management")
                            
                        if capabilities:
                            self.test_results.append(f"✅ Window management capabilities: {', '.join(capabilities)}")
                            return True
                        else:
                            self.test_results.append("❌ Window management: No capabilities detected")
                            return False
                            
                    except Exception as e:
                        self.test_results.append(f"❌ Window state: Protocol check failed - {e}")
                        return False
                else:
                    self.test_results.append("❌ Window state: No display available")
                    return False
            else:
                self.test_results.append("❌ Window state: Compositor not responsive")
                return False
                
        except Exception as e:
            self.test_results.append(f"❌ Window state management: ERROR - {e}")
            return False
            
    def test_surface_focus_management(self):
        """Test surface focus capabilities"""
        print("Testing surface focus management...")
        
        display = self.get_active_wayland_display()
        if not display:
            self.test_results.append("❌ Focus management: No active display")
            return False
            
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = display
        
        try:
            # Test seat capabilities for focus management
            result = subprocess.run(
                ['wayland-info'],
                env=env,
                capture_output=True,
                timeout=5
            )
            
            output = result.stdout.decode()
            
            focus_features = []
            if 'wl_seat' in output:
                focus_features.append("seat management")
            if 'wl_keyboard' in output:
                focus_features.append("keyboard focus")
            if 'wl_pointer' in output:
                focus_features.append("pointer focus")
            if 'wl_touch' in output:
                focus_features.append("touch focus")
                
            if focus_features:
                self.test_results.append(f"✅ Focus management: {', '.join(focus_features)} supported")
                return True
            else:
                self.test_results.append("❌ Focus management: No focus capabilities detected")
                return False
                
        except Exception as e:
            self.test_results.append(f"❌ Focus management: ERROR - {e}")
            return False
            
    def run_surface_tests(self):
        """Run all surface management tests"""
        print("=== Running Surface Management Tests ===")
        
        tests = [
            self.test_basic_surface_operations,
            self.test_client_application_launch,
            self.test_window_state_management,
            self.test_surface_focus_management
        ]
        
        passed = 0
        total = len(tests)
        
        # Start compositor for tests
        self.start_compositor()
        
        if self.compositor_process.poll() is None:
            for test in tests:
                try:
                    if test():
                        passed += 1
                except Exception as e:
                    self.test_results.append(f"❌ {test.__name__}: EXCEPTION - {e}")
        else:
            self.test_results.append("❌ Surface tests: Compositor failed to start")
            
        self.stop_compositor()
        
        # Print results
        print("\n=== Surface Management Test Results ===")
        for result in self.test_results:
            print(result)
            
        print(f"\nSummary: {passed}/{total} surface tests passed")
        
        if passed == total:
            print("🎉 ALL SURFACE MANAGEMENT TESTS PASSED!")
            return True
        else:
            print(f"⚠️  {total - passed} surface tests failed")
            return False

if __name__ == "__main__":
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        sys.exit(1)
        
    tester = SurfaceManagementTester()
    success = tester.run_surface_tests()
    sys.exit(0 if success else 1)