#!/usr/bin/env python3
# test_configuration_system.py

import subprocess
import tempfile
import os
import shutil
import sys

class ConfigTester:
    def __init__(self):
        self.test_configs = []
        self.results = []
        
    def create_test_config(self, config_content, name):
        """Create a temporary test configuration"""
        config_dir = os.path.expanduser("~/.config/fluxbox-wayland")
        os.makedirs(config_dir, exist_ok=True)
        
        config_path = os.path.join(config_dir, "fluxbox-wayland.conf")
        
        # Backup existing config
        backup_path = config_path + ".backup"
        if os.path.exists(config_path):
            shutil.copy2(config_path, backup_path)
            
        # Write test config
        with open(config_path, 'w') as f:
            f.write(config_content)
            
        return config_path, backup_path
        
    def restore_config(self, config_path, backup_path):
        """Restore original configuration"""
        try:
            if os.path.exists(backup_path):
                shutil.move(backup_path, config_path)
            elif os.path.exists(config_path):
                os.remove(config_path)
        except Exception as e:
            print(f"Warning: Could not restore config: {e}")
            
    def test_config_scenario(self, config_content, name, expected_workspaces):
        """Test a specific configuration scenario"""
        print(f"Testing: {name}")
        
        config_path, backup_path = self.create_test_config(config_content, name)
        
        try:
            # Start compositor with test config
            env = os.environ.copy()
            env['WLR_BACKENDS'] = 'headless'
            env['WLR_RENDERER'] = 'pixman'
            
            process = subprocess.Popen(
                ['./build/fluxbox-wayland'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env
            )
            
            # Wait for startup and capture output
            try:
                stdout, stderr = process.communicate(timeout=6)
                output = stdout.decode() + stderr.decode()
                
                # Check for expected workspace count
                workspace_line = f"{expected_workspaces} workspaces available"
                config_loaded_line = f"Config loaded: {expected_workspaces} workspaces"
                
                if workspace_line in output or config_loaded_line in output:
                    self.results.append(f"✅ {name}: SUCCESS - {expected_workspaces} workspaces")
                    return True
                else:
                    self.results.append(f"❌ {name}: FAILED - Expected {expected_workspaces} workspaces")
                    self.results.append(f"   Output snippet: {output[-200:]}")
                    return False
                    
            except subprocess.TimeoutExpired:
                process.kill()
                self.results.append(f"❌ {name}: TIMEOUT")
                return False
                
        except Exception as e:
            self.results.append(f"❌ {name}: ERROR - {e}")
            return False
            
        finally:
            self.restore_config(config_path, backup_path)
            
    def test_invalid_config(self):
        """Test handling of invalid configuration"""
        print("Testing invalid config handling...")
        
        invalid_config = '''
# Invalid configuration
invalid.setting.format
session.screen0.workspaces: not_a_number
key : incomplete_binding
'''
        
        config_path, backup_path = self.create_test_config(invalid_config, "invalid")
        
        try:
            env = os.environ.copy()
            env['WLR_BACKENDS'] = 'headless'
            env['WLR_RENDERER'] = 'pixman'
            
            process = subprocess.Popen(
                ['./build/fluxbox-wayland'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env
            )
            
            stdout, stderr = process.communicate(timeout=6)
            output = stdout.decode() + stderr.decode()
            
            # Should fall back to defaults when config is invalid
            if "4 workspaces available" in output or "Loaded default configuration" in output:
                self.results.append("✅ Invalid config handling: SUCCESS - Fell back to defaults")
                return True
            else:
                self.results.append("❌ Invalid config handling: FAILED")
                return False
                
        except subprocess.TimeoutExpired:
            process.kill()
            self.results.append("❌ Invalid config handling: TIMEOUT")
            return False
        except Exception as e:
            self.results.append(f"❌ Invalid config handling: ERROR - {e}")
            return False
        finally:
            self.restore_config(config_path, backup_path)
            
    def run_config_tests(self):
        """Run comprehensive configuration tests"""
        print("=== Running Configuration System Tests ===")
        
        test_cases = [
            {
                'name': 'Default 4 Workspaces',
                'config': '''# Test config with default workspaces
session.screen0.workspaces: 4
session.screen0.workspaceNames.workspace0: Main
session.screen0.workspaceNames.workspace1: Work
session.screen0.workspaceNames.workspace2: Web
session.screen0.workspaceNames.workspace3: Misc

# Key bindings
key Alt+1 : workspace 1
key Alt+2 : workspace 2
key Alt+q : close
''',
                'expected': 4
            },
            {
                'name': 'Custom 6 Workspaces',
                'config': '''# Test config with 6 workspaces
session.screen0.workspaces: 6
session.screen0.workspaceNames.workspace0: Desktop1
session.screen0.workspaceNames.workspace1: Desktop2
session.screen0.workspaceNames.workspace2: Desktop3
session.screen0.workspaceNames.workspace3: Desktop4
session.screen0.workspaceNames.workspace4: Desktop5
session.screen0.workspaceNames.workspace5: Desktop6

# Window management
session.screen0.focusModel: ClickFocus
session.screen0.autoRaise: false
''',
                'expected': 6
            },
            {
                'name': 'Minimal 2 Workspaces',
                'config': '''# Test config with minimal workspaces
session.screen0.workspaces: 2
session.screen0.workspaceNames.workspace0: Primary
session.screen0.workspaceNames.workspace1: Secondary
''',
                'expected': 2
            },
            {
                'name': 'Single Workspace',
                'config': '''# Test config with single workspace
session.screen0.workspaces: 1
session.screen0.workspaceNames.workspace0: OnlyOne
''',
                'expected': 1
            }
        ]
        
        passed = 0
        for test_case in test_cases:
            if self.test_config_scenario(
                test_case['config'], 
                test_case['name'], 
                test_case['expected']
            ):
                passed += 1
                
        # Test invalid config handling
        if self.test_invalid_config():
            passed += 1
            
        # Test missing config fallback
        print("Testing missing config fallback...")
        config_dir = os.path.expanduser("~/.config/fluxbox-wayland")
        config_path = os.path.join(config_dir, "fluxbox-wayland.conf")
        
        # Temporarily remove config
        backup_exists = os.path.exists(config_path)
        if backup_exists:
            shutil.move(config_path, config_path + ".temp")
            
        try:
            env = os.environ.copy()
            env['WLR_BACKENDS'] = 'headless'
            env['WLR_RENDERER'] = 'pixman'
            
            process = subprocess.Popen(
                ['./build/fluxbox-wayland'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env
            )
            
            stdout, stderr = process.communicate(timeout=6)
            output = stdout.decode() + stderr.decode()
            
            if "Using default configuration" in output and "4 workspaces available" in output:
                self.results.append("✅ Missing config fallback: SUCCESS")
                passed += 1
            else:
                self.results.append("❌ Missing config fallback: FAILED")
                
        except subprocess.TimeoutExpired:
            process.kill()
            self.results.append("❌ Missing config fallback: TIMEOUT")
        except Exception as e:
            self.results.append(f"❌ Missing config fallback: ERROR - {e}")
            
        finally:
            if backup_exists:
                shutil.move(config_path + ".temp", config_path)
                
        # Print results
        print("\n=== Configuration Test Results ===")
        for result in self.results:
            print(result)
            
        total_tests = len(test_cases) + 2  # +2 for invalid config and missing config tests
        print(f"\nSummary: {passed}/{total_tests} configuration tests passed")
        
        if passed == total_tests:
            print("🎉 ALL CONFIGURATION TESTS PASSED!")
            return True
        else:
            print(f"⚠️  {total_tests - passed} configuration tests failed")
            return False

if __name__ == "__main__":
    # Change to project directory
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        print("Please run from project root with: python3 test_configuration_system.py")
        sys.exit(1)
        
    tester = ConfigTester()
    success = tester.run_config_tests()
    sys.exit(0 if success else 1)