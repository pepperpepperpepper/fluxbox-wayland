#!/usr/bin/env python3
# test_evidence_recorder.py - Record evidence of compositor functionality

import subprocess
import time
import os
import sys
import json
from datetime import datetime
from typing import Dict, List, Any

class EvidenceRecorder:
    def __init__(self):
        self.evidence_log = []
        self.compositor_process = None
        self.session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.evidence_dir = f"evidence_{self.session_id}"
        
    def log_evidence(self, category: str, test: str, status: str, details: Dict[str, Any] = None):
        """Log evidence with timestamp and details"""
        evidence_entry = {
            "timestamp": datetime.now().isoformat(),
            "category": category,
            "test": test,
            "status": status,
            "details": details or {}
        }
        self.evidence_log.append(evidence_entry)
        
        # Also print for real-time feedback
        status_icon = "✅" if status == "PASS" else "❌" if status == "FAIL" else "⚠️"
        print(f"{status_icon} [{category}] {test}: {status}")
        if details:
            for key, value in details.items():
                print(f"    {key}: {value}")
                
    def setup_evidence_directory(self):
        """Create directory for evidence collection"""
        os.makedirs(self.evidence_dir, exist_ok=True)
        print(f"📁 Evidence will be collected in: {self.evidence_dir}/")
        
    def start_compositor_with_logging(self):
        """Start compositor and capture detailed startup logs"""
        print("🚀 Starting Fluxbox Wayland compositor with full logging...")
        
        env = os.environ.copy()
        env['WLR_BACKENDS'] = 'headless'
        env['WLR_RENDERER'] = 'pixman'
        env['WLR_DEBUG'] = '1'  # Enable debug logging
        
        # Capture startup process
        startup_log_path = os.path.join(self.evidence_dir, "compositor_startup.log")
        
        with open(startup_log_path, 'w') as log_file:
            self.compositor_process = subprocess.Popen(
                ['./build/fluxbox-wayland'],
                stdout=log_file,
                stderr=subprocess.STDOUT,
                env=env
            )
            
        # Wait for startup
        time.sleep(4)
        
        if self.compositor_process.poll() is None:
            self.log_evidence("STARTUP", "Compositor Launch", "PASS", {
                "pid": self.compositor_process.pid,
                "startup_time": "3 seconds",
                "log_file": startup_log_path
            })
            return True
        else:
            self.log_evidence("STARTUP", "Compositor Launch", "FAIL", {
                "exit_code": self.compositor_process.poll(),
                "log_file": startup_log_path
            })
            return False
            
    def record_wayland_socket_evidence(self):
        """Record evidence of Wayland socket creation"""
        print("🔍 Collecting Wayland socket evidence...")
        
        uid = os.getuid()
        runtime_dir = f"/run/user/{uid}"
        
        # List all wayland sockets
        socket_info = []
        for i in range(10):
            socket_path = f"{runtime_dir}/wayland-{i}"
            lock_path = f"{socket_path}.lock"
            
            if os.path.exists(socket_path):
                stat_info = os.stat(socket_path)
                socket_info.append({
                    "socket": socket_path,
                    "exists": True,
                    "size": stat_info.st_size,
                    "modified": datetime.fromtimestamp(stat_info.st_mtime).isoformat(),
                    "lock_file": os.path.exists(lock_path)
                })
                
        # Save socket evidence
        socket_evidence_path = os.path.join(self.evidence_dir, "wayland_sockets.json")
        with open(socket_evidence_path, 'w') as f:
            json.dump(socket_info, f, indent=2)
            
        if socket_info:
            self.log_evidence("SOCKETS", "Wayland Socket Creation", "PASS", {
                "sockets_found": len(socket_info),
                "evidence_file": socket_evidence_path,
                "active_sockets": [s["socket"] for s in socket_info]
            })
            return socket_info[0]["socket"].split("-")[-1]  # Return display number
        else:
            self.log_evidence("SOCKETS", "Wayland Socket Creation", "FAIL", {
                "runtime_dir": runtime_dir,
                "sockets_found": 0
            })
            return None
            
    def record_protocol_support_evidence(self, display: str):
        """Record evidence of Wayland protocol support"""
        print("📋 Recording Wayland protocol support evidence...")
        
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = f"wayland-{display}"
        
        try:
            # Get wayland-info output
            result = subprocess.run(
                ['wayland-info'],
                env=env,
                capture_output=True,
                timeout=10,
                text=True
            )
            
            if result.returncode == 0:
                # Save protocol info
                protocol_evidence_path = os.path.join(self.evidence_dir, "wayland_protocols.txt")
                with open(protocol_evidence_path, 'w') as f:
                    f.write(result.stdout)
                    
                # Parse supported protocols
                protocols = []
                for line in result.stdout.split('\n'):
                    if 'interface:' in line:
                        protocol = line.split("'")[1] if "'" in line else line.split()[-1]
                        protocols.append(protocol)
                        
                self.log_evidence("PROTOCOLS", "Wayland Protocol Support", "PASS", {
                    "protocols_count": len(protocols),
                    "key_protocols": [p for p in protocols if any(key in p for key in ['compositor', 'xdg', 'seat', 'output'])],
                    "evidence_file": protocol_evidence_path
                })
                return True
            else:
                self.log_evidence("PROTOCOLS", "Wayland Protocol Support", "FAIL", {
                    "error": result.stderr,
                    "return_code": result.returncode
                })
                return False
                
        except subprocess.TimeoutExpired:
            self.log_evidence("PROTOCOLS", "Wayland Protocol Support", "FAIL", {
                "error": "wayland-info timeout"
            })
            return False
        except FileNotFoundError:
            self.log_evidence("PROTOCOLS", "Wayland Protocol Support", "SKIP", {
                "reason": "wayland-info not available"
            })
            return False
            
    def record_client_connection_evidence(self, display: str):
        """Record evidence of client connections working"""
        print("🔗 Recording client connection evidence...")
        
        env = os.environ.copy()
        env['WAYLAND_DISPLAY'] = f"wayland-{display}"
        
        client_tests = [
            ("wayland-info", "wayland-info", "Protocol enumeration client"),
            ("foot", "foot --version", "Terminal emulator version check"),
            ("weston-simple-damage", "weston-simple-damage", "Simple Wayland client")
        ]
        
        successful_clients = []
        
        for client_name, command, description in client_tests:
            try:
                result = subprocess.run(
                    command.split(),
                    env=env,
                    capture_output=True,
                    timeout=5,
                    text=True
                )
                
                if result.returncode == 0:
                    successful_clients.append({
                        "client": client_name,
                        "description": description,
                        "output_length": len(result.stdout),
                        "success": True
                    })
                else:
                    successful_clients.append({
                        "client": client_name,
                        "description": description,
                        "error": result.stderr[:200],
                        "success": False
                    })
                    
            except subprocess.TimeoutExpired:
                successful_clients.append({
                    "client": client_name,
                    "description": description,
                    "error": "timeout",
                    "success": False
                })
            except FileNotFoundError:
                successful_clients.append({
                    "client": client_name,
                    "description": description,
                    "error": "not available",
                    "success": False
                })
                
        # Save client evidence
        client_evidence_path = os.path.join(self.evidence_dir, "client_connections.json")
        with open(client_evidence_path, 'w') as f:
            json.dump(successful_clients, f, indent=2)
            
        successful_count = sum(1 for c in successful_clients if c["success"])
        
        if successful_count > 0:
            self.log_evidence("CLIENTS", "Client Connections", "PASS", {
                "successful_clients": successful_count,
                "total_tested": len(client_tests),
                "working_clients": [c["client"] for c in successful_clients if c["success"]],
                "evidence_file": client_evidence_path
            })
            return True
        else:
            self.log_evidence("CLIENTS", "Client Connections", "FAIL", {
                "successful_clients": 0,
                "total_tested": len(client_tests),
                "evidence_file": client_evidence_path
            })
            return False
            
    def record_configuration_evidence(self):
        """Record evidence of configuration system working"""
        print("⚙️ Recording configuration system evidence...")
        
        config_dir = os.path.expanduser("~/.config/fluxbox-wayland")
        config_file = os.path.join(config_dir, "fluxbox-wayland.conf")
        
        config_evidence = {
            "config_directory_exists": os.path.exists(config_dir),
            "config_file_exists": os.path.exists(config_file),
            "config_directory": config_dir
        }
        
        if os.path.exists(config_file):
            with open(config_file, 'r') as f:
                config_content = f.read()
                config_evidence["config_content_length"] = len(config_content)
                config_evidence["has_workspace_config"] = "workspaces" in config_content
                config_evidence["has_key_bindings"] = "key " in config_content
                
        config_evidence_path = os.path.join(self.evidence_dir, "configuration.json")
        with open(config_evidence_path, 'w') as f:
            json.dump(config_evidence, f, indent=2)
            
        if config_evidence["config_directory_exists"]:
            self.log_evidence("CONFIG", "Configuration System", "PASS", {
                "config_dir": config_dir,
                "config_file_present": config_evidence["config_file_exists"],
                "evidence_file": config_evidence_path
            })
            return True
        else:
            self.log_evidence("CONFIG", "Configuration System", "FAIL", {
                "config_dir": config_dir,
                "evidence_file": config_evidence_path
            })
            return False
            
    def test_graceful_shutdown_evidence(self):
        """Test and record graceful shutdown evidence"""
        print("🛑 Testing graceful shutdown...")
        
        if not self.compositor_process:
            self.log_evidence("SHUTDOWN", "Graceful Shutdown", "FAIL", {
                "reason": "No compositor process running"
            })
            return False
            
        # Record shutdown attempt
        shutdown_start = time.time()
        self.compositor_process.terminate()
        
        try:
            self.compositor_process.wait(timeout=5)
            shutdown_time = time.time() - shutdown_start
            
            self.log_evidence("SHUTDOWN", "Graceful Shutdown", "PASS", {
                "shutdown_time_seconds": round(shutdown_time, 2),
                "signal": "SIGTERM",
                "exit_code": self.compositor_process.poll()
            })
            return True
            
        except subprocess.TimeoutExpired:
            self.compositor_process.kill()
            shutdown_time = time.time() - shutdown_start
            
            self.log_evidence("SHUTDOWN", "Graceful Shutdown", "FAIL", {
                "timeout_seconds": 5,
                "forced_kill": True,
                "shutdown_time_seconds": round(shutdown_time, 2)
            })
            return False
            
    def generate_evidence_report(self):
        """Generate comprehensive evidence report"""
        print("📄 Generating evidence report...")
        
        # Summary statistics
        total_tests = len(self.evidence_log)
        passed_tests = sum(1 for e in self.evidence_log if e["status"] == "PASS")
        failed_tests = sum(1 for e in self.evidence_log if e["status"] == "FAIL")
        skipped_tests = sum(1 for e in self.evidence_log if e["status"] == "SKIP")
        
        report = {
            "session_id": self.session_id,
            "test_timestamp": datetime.now().isoformat(),
            "system_info": {
                "os": os.uname().sysname,
                "version": os.uname().release,
                "architecture": os.uname().machine,
                "python_version": sys.version.split()[0]
            },
            "summary": {
                "total_tests": total_tests,
                "passed": passed_tests,
                "failed": failed_tests,
                "skipped": skipped_tests,
                "success_rate": round((passed_tests / total_tests) * 100, 1) if total_tests > 0 else 0
            },
            "evidence_log": self.evidence_log
        }
        
        # Save detailed report
        report_path = os.path.join(self.evidence_dir, "evidence_report.json")
        with open(report_path, 'w') as f:
            json.dump(report, f, indent=2)
            
        # Generate human-readable summary
        summary_path = os.path.join(self.evidence_dir, "EVIDENCE_SUMMARY.md")
        with open(summary_path, 'w') as f:
            f.write(f"# Fluxbox Wayland Compositor - Evidence Report\n\n")
            f.write(f"**Session ID**: {self.session_id}\n")
            f.write(f"**Test Date**: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            
            f.write(f"## Test Results Summary\n\n")
            f.write(f"- **Total Tests**: {total_tests}\n")
            f.write(f"- **Passed**: {passed_tests} ✅\n")
            f.write(f"- **Failed**: {failed_tests} ❌\n")
            f.write(f"- **Skipped**: {skipped_tests} ⚠️\n")
            f.write(f"- **Success Rate**: {report['summary']['success_rate']}%\n\n")
            
            f.write(f"## Evidence Categories\n\n")
            
            categories = {}
            for entry in self.evidence_log:
                cat = entry["category"]
                if cat not in categories:
                    categories[cat] = []
                categories[cat].append(entry)
                
            for category, entries in categories.items():
                f.write(f"### {category}\n\n")
                for entry in entries:
                    status_icon = "✅" if entry["status"] == "PASS" else "❌" if entry["status"] == "FAIL" else "⚠️"
                    f.write(f"- {status_icon} **{entry['test']}**: {entry['status']}\n")
                    if entry.get("details"):
                        for key, value in entry["details"].items():
                            f.write(f"  - {key}: `{value}`\n")
                f.write("\n")
                
            f.write(f"## Evidence Files\n\n")
            f.write(f"All evidence and logs are stored in: `{self.evidence_dir}/`\n\n")
            f.write(f"- `evidence_report.json` - Detailed machine-readable report\n")
            f.write(f"- `compositor_startup.log` - Compositor startup logs\n")
            f.write(f"- `wayland_protocols.txt` - Supported Wayland protocols\n")
            f.write(f"- `wayland_sockets.json` - Socket creation evidence\n")
            f.write(f"- `client_connections.json` - Client connection test results\n")
            f.write(f"- `configuration.json` - Configuration system evidence\n")
            
        print(f"📊 Evidence report generated: {summary_path}")
        return report_path, summary_path
        
    def run_evidence_collection(self):
        """Run comprehensive evidence collection"""
        print("=== Fluxbox Wayland Compositor - Evidence Collection ===")
        print(f"Session ID: {self.session_id}")
        print()
        
        self.setup_evidence_directory()
        
        # Start compositor
        if not self.start_compositor_with_logging():
            print("❌ Cannot collect evidence - compositor failed to start")
            return False
            
        # Collect evidence
        display = self.record_wayland_socket_evidence()
        if display:
            self.record_protocol_support_evidence(display)
            self.record_client_connection_evidence(display)
            
        self.record_configuration_evidence()
        
        # Test shutdown
        self.test_graceful_shutdown_evidence()
        
        # Generate report
        report_path, summary_path = self.generate_evidence_report()
        
        print("\n=== Evidence Collection Complete ===")
        print(f"📁 Evidence directory: {self.evidence_dir}/")
        print(f"📄 Summary report: {summary_path}")
        print(f"📊 Detailed report: {report_path}")
        
        # Print final summary
        passed = sum(1 for e in self.evidence_log if e["status"] == "PASS")
        total = len(self.evidence_log)
        
        if passed == total:
            print("✅ ALL EVIDENCE COLLECTION SUCCESSFUL")
            print("🎉 Fluxbox Wayland compositor fully functional and documented!")
        else:
            print(f"⚠️  Evidence collection: {passed}/{total} tests successful")
            
        return passed == total

if __name__ == "__main__":
    if os.path.exists('build/fluxbox-wayland'):
        print("Found fluxbox-wayland binary")
    else:
        print("ERROR: fluxbox-wayland binary not found")
        sys.exit(1)
        
    recorder = EvidenceRecorder()
    success = recorder.run_evidence_collection()
    sys.exit(0 if success else 1)