#!/usr/bin/env python3
# test_basic_functionality.py

import subprocess
import time
import os
import signal
import sys

class FluxboxTester:
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
            
    def test_startup(self):
        """Test compositor startup"""
        print("Testing compositor startup...")
        self.start_compositor()
        
        # Check if process is running
        if self.compositor_process.poll() is None:
            self.test_results.append("✅ Compositor startup: SUCCESS")
            return True
        else:
            stdout, stderr = self.compositor_process.communicate()
            self.test_results.append("❌ Compositor startup: FAILED")
            self.test_results.append(f"   STDOUT: {stdout.decode()[:200]}")
            self.test_results.append(f"   STDERR: {stderr.decode()[:200]}")
            return False
            
    def test_wayland_socket(self):
        """Test Wayland socket creation"""
        print("Testing Wayland socket creation...")
        # Check for wayland socket in runtime directory
        import pwd
        uid = os.getuid()
        runtime_dir = f"/run/user/{uid}"
        
        wayland_sockets = []
        for i in range(10):
            socket_path = f"{runtime_dir}/wayland-{i}"
            if os.path.exists(socket_path):
                wayland_sockets.append(socket_path)
                
        # Also check /tmp for compatibility
        for i in range(10):
            socket_path = f"/tmp/wayland-{i}"
            if os.path.exists(socket_path):
                wayland_sockets.append(socket_path)
                
        if wayland_sockets:
            self.test_results.append(f"✅ Wayland socket creation: SUCCESS ({len(wayland_sockets)} sockets)")
            return True
        else:
            self.test_results.append("❌ Wayland socket creation: FAILED")
            return False
            
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
        
    def test_client_connection(self):
        """Test basic client connection"""
        print("Testing client connection...")
        try:
            # Find active wayland display
            display = self.get_active_wayland_display()
            if not display:
                self.test_results.append("❌ Client connection: No display found")
                return False
                
            env = os.environ.copy()
            env['WAYLAND_DISPLAY'] = display
            
            # Try wayland-info first
            result = subprocess.run(
                ['wayland-info'],
                env=env,
                capture_output=True,
                timeout=5
            )
            
            if result.returncode == 0:
                self.test_results.append("✅ Client connection (wayland-info): SUCCESS")
                
                # Try launching a simple client
                foot_result = subprocess.run(
                    ['foot', '--version'],
                    env=env,
                    capture_output=True,
                    timeout=3
                )
                
                if foot_result.returncode == 0:
                    self.test_results.append("✅ Client application (foot): AVAILABLE")
                else:
                    self.test_results.append("⚠️  Client application (foot): NOT AVAILABLE")
                
                return True
            else:
                self.test_results.append("❌ Client connection: wayland-info failed")
                return False
            
        except subprocess.TimeoutExpired:
            self.test_results.append("❌ Client connection: TIMEOUT")
            return False
        except FileNotFoundError:
            self.test_results.append("❌ Client connection: wayland-info not found")
            return False
        except Exception as e:
            self.test_results.append(f"❌ Client connection: ERROR - {e}")
            return False
            
    def test_config_loading(self):
        """Test configuration system loading"""
        print("Testing configuration loading...")
        
        if not self.compositor_process:
            self.test_results.append("❌ Config loading: No compositor running")
            return False
            
        # Get compositor output
        time.sleep(1)  # Allow config loading
        
        # Since process is still running, we can't get output easily
        # We'll check if config directory was created
        config_dir = os.path.expanduser("~/.config/fluxbox-wayland")
        
        if os.path.exists(config_dir):
            self.test_results.append("✅ Configuration directory: CREATED")
            return True
        else:
            self.test_results.append("❌ Configuration directory: NOT CREATED")
            return False
            
    def test_graceful_shutdown(self):
        """Test graceful shutdown"""
        print("Testing graceful shutdown...")
        
        if not self.compositor_process:
            self.test_results.append("❌ Graceful shutdown: No compositor running")
            return False
            
        # Send SIGTERM for graceful shutdown
        self.compositor_process.terminate()
        
        try:
            self.compositor_process.wait(timeout=5)
            self.test_results.append("✅ Graceful shutdown: SUCCESS")
            return True
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Graceful shutdown: TIMEOUT - forced kill")
            return False
            
    def run_basic_tests(self):
        """Run all basic functionality tests"""
        print("=== Running Basic Functionality Tests ===")
        
        tests = [
            self.test_startup,
            self.test_wayland_socket,
            self.test_client_connection,
            self.test_config_loading,
            self.test_graceful_shutdown
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
        print("\n=== Test Results ===")
        for result in self.test_results:
            print(result)
            
        print(f"\nSummary: {passed}/{total} tests passed")
        
        if passed == total:
            print("🎉 ALL BASIC TESTS PASSED!")
            return True
        else:
            print(f"⚠️  {total - passed} tests failed")
            return False

if __name__ == "__main__":
    # Change to project directory
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        print("Please run from project root with: python3 test_basic_functionality.py")
        sys.exit(1)
        
    tester = FluxboxTester()
    success = tester.run_basic_tests()
    sys.exit(0 if success else 1)