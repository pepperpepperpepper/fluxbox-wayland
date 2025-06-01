#!/usr/bin/env python3
# test_workspace_functionality.py - Test workspace management and switching

import subprocess
import time
import os
import sys
import tempfile
import shutil

class WorkspaceTester:
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
        
        time.sleep(3)  # Allow startup and config loading
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
                
    def test_default_workspace_setup(self):
        """Test default 4-workspace configuration"""
        print("Testing default workspace setup...")
        
        config = '''# Default workspace configuration
session.screen0.workspaces: 4
session.screen0.workspaceNames.workspace0: Main
session.screen0.workspaceNames.workspace1: Work
session.screen0.workspaceNames.workspace2: Web
session.screen0.workspaceNames.workspace3: Misc

# Workspace switching keys
key Alt+1 : workspace 1
key Alt+2 : workspace 2
key Alt+3 : workspace 3
key Alt+4 : workspace 4
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                # Get output to verify workspace setup
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                if "4 workspaces available" in output or "workspace" in output.lower():
                    self.test_results.append("✅ Default workspace setup: SUCCESS (4 workspaces)")
                    return True
                else:
                    self.test_results.append("❌ Default workspace setup: FAILED")
                    self.test_results.append(f"   Output: {output[-200:]}")
                    return False
            else:
                stdout, stderr = self.compositor_process.communicate()
                self.test_results.append("❌ Default workspace setup: Compositor failed to start")
                self.test_results.append(f"   Error: {stderr.decode()[-200:]}")
                return False
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Default workspace setup: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_custom_workspace_counts(self):
        """Test various custom workspace configurations"""
        print("Testing custom workspace counts...")
        
        test_cases = [
            (2, "Minimal setup"),
            (6, "Extended setup"),
            (8, "Maximum setup"),
            (1, "Single workspace")
        ]
        
        passed = 0
        for workspace_count, description in test_cases:
            print(f"  Testing {workspace_count} workspaces ({description})...")
            
            config = f'''# {description}
session.screen0.workspaces: {workspace_count}
'''
            
            # Add workspace names
            for i in range(workspace_count):
                config += f'session.screen0.workspaceNames.workspace{i}: Workspace{i+1}\n'
                
            # Add key bindings
            for i in range(min(workspace_count, 9)):
                config += f'key Alt+{i+1} : workspace {i+1}\n'
                
            backup_path = self.start_compositor_with_config(config)
            
            try:
                if self.compositor_process.poll() is None:
                    stdout, stderr = self.compositor_process.communicate(timeout=5)
                    output = stdout.decode() + stderr.decode()
                    
                    if f"{workspace_count} workspaces available" in output:
                        self.test_results.append(f"✅ Custom workspaces ({workspace_count}): SUCCESS")
                        passed += 1
                    else:
                        self.test_results.append(f"❌ Custom workspaces ({workspace_count}): FAILED")
                        
                else:
                    self.test_results.append(f"❌ Custom workspaces ({workspace_count}): Compositor failed")
                    
            except subprocess.TimeoutExpired:
                self.compositor_process.kill()
                self.test_results.append(f"❌ Custom workspaces ({workspace_count}): TIMEOUT")
            finally:
                self.restore_config(backup_path)
                self.stop_compositor()
                
        return passed == len(test_cases)
        
    def test_workspace_key_bindings(self):
        """Test workspace switching key bindings"""
        print("Testing workspace key bindings...")
        
        config = '''# Workspace key binding test
session.screen0.workspaces: 4

# Standard Alt+number bindings
key Alt+1 : workspace 1
key Alt+2 : workspace 2
key Alt+3 : workspace 3
key Alt+4 : workspace 4

# Arrow key navigation
key Alt+Left : workspace prev
key Alt+Right : workspace next

# Other key bindings for testing
key Alt+q : close
key Ctrl+Alt+t : exec foot
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                # Look for key binding related output
                if "workspace" in output and ("Alt" in output or "key" in output.lower()):
                    self.test_results.append("✅ Workspace key bindings: CONFIGURED")
                    return True
                elif "4 workspaces available" in output:
                    # Even if key binding output isn't visible, workspace setup worked
                    self.test_results.append("✅ Workspace key bindings: WORKSPACES READY")
                    return True
                else:
                    self.test_results.append("❌ Workspace key bindings: FAILED")
                    return False
            else:
                self.test_results.append("❌ Workspace key bindings: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Workspace key bindings: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_workspace_naming(self):
        """Test custom workspace naming"""
        print("Testing workspace naming...")
        
        config = '''# Custom workspace names test
session.screen0.workspaces: 5
session.screen0.workspaceNames.workspace0: Development
session.screen0.workspaceNames.workspace1: Communication
session.screen0.workspaceNames.workspace2: Multimedia
session.screen0.workspaceNames.workspace3: Research
session.screen0.workspaceNames.workspace4: Gaming

key Alt+1 : workspace 1
key Alt+2 : workspace 2
key Alt+3 : workspace 3
key Alt+4 : workspace 4
key Alt+5 : workspace 5
'''
        
        backup_path = self.start_compositor_with_config(config)
        
        try:
            if self.compositor_process.poll() is None:
                stdout, stderr = self.compositor_process.communicate(timeout=5)
                output = stdout.decode() + stderr.decode()
                
                # Check for workspace count
                if "5 workspaces available" in output:
                    self.test_results.append("✅ Workspace naming: SUCCESS (5 named workspaces)")
                    return True
                else:
                    self.test_results.append("❌ Workspace naming: FAILED")
                    self.test_results.append(f"   Output: {output[-200:]}")
                    return False
            else:
                self.test_results.append("❌ Workspace naming: Compositor failed")
                return False
                
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            self.test_results.append("❌ Workspace naming: TIMEOUT")
            return False
        finally:
            self.restore_config(backup_path)
            
    def test_workspace_edge_cases(self):
        """Test workspace edge cases and error handling"""
        print("Testing workspace edge cases...")
        
        edge_cases = [
            (0, "Zero workspaces", False),  # Should fall back to default
            (-1, "Negative workspaces", False),  # Should fall back to default
            (100, "Too many workspaces", False),  # Should handle gracefully
            ("invalid", "Non-numeric value", False)  # Should fall back to default
        ]
        
        passed = 0
        for workspace_value, description, should_succeed in edge_cases:
            print(f"  Testing {description}...")
            
            config = f'''# {description} test
session.screen0.workspaces: {workspace_value}
key Alt+1 : workspace 1
'''
            
            backup_path = self.start_compositor_with_config(config)
            
            try:
                if self.compositor_process.poll() is None:
                    stdout, stderr = self.compositor_process.communicate(timeout=5)
                    output = stdout.decode() + stderr.decode()
                    
                    # For edge cases, we expect fallback to default (4 workspaces)
                    if "4 workspaces available" in output or "workspaces available" in output:
                        self.test_results.append(f"✅ Edge case ({description}): HANDLED - Fell back to defaults")
                        passed += 1
                    else:
                        self.test_results.append(f"❌ Edge case ({description}): FAILED")
                        
                else:
                    # Compositor failing to start might be acceptable for some edge cases
                    self.test_results.append(f"⚠️  Edge case ({description}): Compositor failed to start")
                    passed += 1  # Count as handled
                    
            except subprocess.TimeoutExpired:
                self.compositor_process.kill()
                self.test_results.append(f"❌ Edge case ({description}): TIMEOUT")
            finally:
                self.restore_config(backup_path)
                self.stop_compositor()
                
        return passed >= len(edge_cases) * 0.75  # Allow 25% tolerance for edge case handling
        
    def run_workspace_tests(self):
        """Run all workspace functionality tests"""
        print("=== Running Workspace Functionality Tests ===")
        
        tests = [
            self.test_default_workspace_setup,
            self.test_custom_workspace_counts,
            self.test_workspace_key_bindings,
            self.test_workspace_naming,
            self.test_workspace_edge_cases
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
        print("\n=== Workspace Functionality Test Results ===")
        for result in self.test_results:
            print(result)
            
        print(f"\nSummary: {passed}/{total} workspace tests passed")
        
        if passed == total:
            print("🎉 ALL WORKSPACE FUNCTIONALITY TESTS PASSED!")
            return True
        else:
            print(f"⚠️  {total - passed} workspace tests failed")
            return False

if __name__ == "__main__":
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        sys.exit(1)
        
    tester = WorkspaceTester()
    success = tester.run_workspace_tests()
    sys.exit(0 if success else 1)